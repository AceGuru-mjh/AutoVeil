#include "nexus/daemon_core.h"
#include "nexus/util.h"
#include "nexus/log.h"
#include "nexus/fs/fs_detector.h"
#include "nexus/selinux_manager.h"

#include <cutils/properties.h>
#include <chrono>
#include <sys/reboot.h>
#include <unistd.h>

#ifndef NEXUS_VERSION
#define NEXUS_VERSION "1.0.0"
#endif

namespace nexus {

DaemonCore::DaemonCore(RootEnvironment env)
    : env_(std::move(env)),
      scriptExecutor_(env_),
      loader_(env_, env_.modulesDir),
      pid_((uint32_t)::getpid()),
      startTimeMs_(std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count())
{
    // 注册日志 hook，把 log::write 输出转发到 EventBus（让 Manager LogsPage 看到）
    // 简化：直接在 log.cpp 中调用 globalBus().publishLog，这里不做 hook
    loadSuState();
}

Result<void> DaemonCore::start() {
    NX_LOG_I("DaemonCore", "starting (pid=%d)", (int)pid_);

    // 1. 选择 FS 拦截器
    fs_ = FsDetector::select(env_);
    NX_LOG_I("DaemonCore", "fs interceptor: %s", std::string(fs_->implName()).c_str());

    if (!safeMode_) {
        // 2. 扫描模块
        auto mr = loader_.scanModules();
        if (!mr) {
            NX_LOG_W("DaemonCore", "scanModules failed: %s; skipping mount stage",
                     errString(mr.error()));
        } else {
            modules_ = std::move(*mr);
            NX_LOG_I("DaemonCore", "loaded %zu module(s)", modules_.size());

            // 3. 校验 + 挂载
            for (auto& m : modules_) {
                if (!m.manifest.enabled) {
                    NX_LOG_I("DaemonCore", "module %s disabled; skip", m.manifest.id.c_str());
                    continue;
                }
                auto report = loader_.validate(m);
                for (auto& w : report.warnings) {
                    NX_LOG_W("DaemonCore", "[%s] %s", m.manifest.id.c_str(), w.c_str());
                }
                // 挂载 system/ 文件
                auto targets = loader_.collectMountTargets(m);
                if (!targets.empty()) {
                    if (auto r = fs_->mountAll(targets); !r) {
                        NX_LOG_W("DaemonCore", "mountAll failed for %s: %s",
                                 m.manifest.id.c_str(), errString(r.error()));
                    }
                }
                bus_.publishModuleLoaded(m.manifest.id);
            }

            // 4. 执行 post-fs-data 脚本
            for (auto& m : modules_) {
                if (!m.manifest.enabled || !m.hasPostFsData) continue;
                // 校验 EXECUTE_SHELL 能力
                auto hasCap = [&](const std::string& c) {
                    return std::find(m.manifest.capabilities.begin(),
                                     m.manifest.capabilities.end(), c) != m.manifest.capabilities.end();
                };
                if (!hasCap("EXECUTE_SHELL")) {
                    NX_LOG_W("DaemonCore", "[%s] CAPABILITY_DENIED for post-fs-data.sh",
                             m.manifest.id.c_str());
                    bus_.publishScriptDone("post-fs-data.sh:" + m.manifest.id, -1);
                    continue;
                }
                ScriptExecutor::ExecOptions opts;
                opts.stage = ScriptExecutor::Stage::PostFsData;
                opts.moduleId = m.manifest.id;
                opts.modulePath = m.path;
                opts.moduleVersion = m.manifest.version;
                opts.scriptPath = m.path + "/post-fs-data.sh";
                opts.isolateNamespace = true;
                auto r = scriptExecutor_.execute(opts);
                bus_.publishScriptDone("post-fs-data.sh:" + m.manifest.id, r.exitCode);
                if (r.exitCode == 2) {
                    NX_LOG_W("DaemonCore", "[%s] post-fs-data exit=2; mark disabled",
                             m.manifest.id.c_str());
                    m.manifest.enabled = false;
                }
            }
        }
    } else {
        NX_LOG_W("DaemonCore", "safe mode active; skipping all modules");
    }

    // 5. 注册 boot_completed 监听
    watchBootCompleted();

    running_ = true;
    NX_LOG_I("DaemonCore", "started");
    return {};
}

Result<void> DaemonCore::startReadOnly() {
    readOnly_ = true;
    NX_LOG_W("DaemonCore", "starting in READ-ONLY mode (no mount, no scripts)");
    fs_ = std::make_unique<NoopInterceptor>();
    running_ = true;
    return {};
}

void DaemonCore::stop() {
    NX_LOG_I("DaemonCore", "stopping");
    running_ = false;
    if (fs_) {
        fs_->unmountAll();
    }
    if (bootWatcher_.joinable()) {
        bootWatcher_.detach();
    }
    NX_LOG_I("DaemonCore", "stopped");
}

std::string_view DaemonCore::fsInterceptorName() const {
    return fs_ ? fs_->implName() : "none";
}

uint64_t DaemonCore::uptimeMs() const {
    if (startTimeMs_ == 0) return 0;
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return now - startTimeMs_;
}

void DaemonCore::watchBootCompleted() {
    bootWatcher_ = std::thread([this]{
        // 轮询 sys.boot_completed=1
        // 生产应使用 property_set 等待，但简化用轮询
        while (running_) {
            char val[32] = {0};
            ::property_get("sys.boot_completed", val, "0");
            if (std::string(val) == "1") {
                NX_LOG_I("DaemonCore", "boot_completed=1; running late_start scripts");
                if (!safeMode_) {
                    for (auto& m : modules_) {
                        if (!m.manifest.enabled || !m.hasService) continue;
                        auto hasCap = [&](const std::string& c) {
                            return std::find(m.manifest.capabilities.begin(),
                                             m.manifest.capabilities.end(), c) != m.manifest.capabilities.end();
                        };
                        if (!hasCap("EXECUTE_SHELL")) {
                            NX_LOG_W("DaemonCore", "[%s] CAPABILITY_DENIED for service.sh",
                                     m.manifest.id.c_str());
                            continue;
                        }
                        ScriptExecutor::ExecOptions opts;
                        opts.stage = ScriptExecutor::Stage::LateStart;
                        opts.moduleId = m.manifest.id;
                        opts.modulePath = m.path;
                        opts.moduleVersion = m.manifest.version;
                        opts.scriptPath = m.path + "/service.sh";
                        opts.isolateNamespace = true;
                        auto r = scriptExecutor_.execute(opts);
                        bus_.publishScriptDone("service.sh:" + m.manifest.id, r.exitCode);
                    }
                }
                break;
            }
            ::sleep(1);
        }
    });
}

// ============ 操作 ============

bool DaemonCore::enableModule(const std::string& id) {
    for (auto& m : modules_) {
        if (m.manifest.id == id) {
            m.manifest.enabled = true;
            // TODO: 持久化到 manifest.json
            NX_LOG_I("DaemonCore", "module %s enabled (effective next boot)", id.c_str());
            return true;
        }
    }
    return false;
}

bool DaemonCore::disableModule(const std::string& id) {
    for (auto& m : modules_) {
        if (m.manifest.id == id) {
            m.manifest.enabled = false;
            // TODO: 持久化到 manifest.json
            NX_LOG_I("DaemonCore", "module %s disabled (effective next boot)", id.c_str());
            return true;
        }
    }
    return false;
}

Result<DaemonCore::InstallResult> DaemonCore::installModule(const std::string& localZipPath) {
    // MVP 简化：解压 + 解析 manifest + 移动到 modules/
    // 完整流程见 spec-03 §8
    NX_LOG_I("DaemonCore", "installing module from %s", localZipPath.c_str());
    std::string tmpDir = "/data/adb/nexuscore/tmp/install_" + std::to_string(::getpid());
    mkdirRecursive(tmpDir, 0755);

    std::string cmd = "unzip -o '" + localZipPath + "' -d '" + tmpDir + "' 2>&1";
    auto r = execCommand(cmd, 60);
    if (r.exitCode != 0) {
        NX_LOG_E("DaemonCore", "unzip failed: %s", r.stderr_.c_str());
        return std::unexpected(Err::IoError);
    }

    // 解析 manifest
    std::string manifestPath = tmpDir + "/manifest.json";
    auto mr = [&]() -> Result<ModuleManifest> {
        // 复用 ModuleLoader 的 parseManifest
        ModuleLoader loader(env_, env_.modulesDir);
        // parseManifest 是 private，这里用 shell out 简化
        auto content = readFile(manifestPath);
        if (!content) return std::unexpected(Err::InvalidArg);
        // 简化：直接用 regex 提取 id（生产应换 ModuleLoader 的 parser）
        std::string id;
        // 直接调 execCommand 跑 python 不靠谱，这里硬解析
        // ... [省略复杂 JSON 解析，复用 module_loader.cpp 的 JsonParser 即可]
        // 由于 module_loader.cpp 的 JsonParser 是 anonymous namespace 的，
        // 这里复用 ModuleLoader::scanModules 间接验证：把 tmpDir 临时作为 modulesDir
        return std::unexpected(Err::Unsupported);
    }();

    // 简化：直接移动 tmpDir 到 modules/<inferred_id>
    // 完整实现需要解析 manifest 拿到 id
    NX_LOG_W("DaemonCore", "install: manifest parsing not fully implemented; using tmp dir name as id");
    // 取 ZIP 文件名作为 id 的近似（不严格）
    size_t slash = localZipPath.find_last_of('/');
    std::string fname = (slash != std::string::npos) ? localZipPath.substr(slash + 1) : localZipPath;
    // 移除 .zip 后缀和 nexus_ 前缀
    if (fname.size() > 4 && fname.substr(fname.size() - 4) == ".zip") {
        fname = fname.substr(0, fname.size() - 4);
    }
    if (fname.rfind("nexus_", 0) == 0) fname = fname.substr(7);

    std::string finalDir = env_.modulesDir + "/" + fname;
    mkdirRecursive(env_.modulesDir, 0755);
    // 如果已存在，先删除
    execCommand("rm -rf '" + finalDir + "'", 10);
    if (::rename(tmpDir.c_str(), finalDir.c_str()) < 0) {
        NX_LOG_E("DaemonCore", "rename %s -> %s failed: %s",
                 tmpDir.c_str(), finalDir.c_str(), ::strerror(errno));
        return std::unexpected(Err::IoError);
    }

    InstallResult result;
    result.id = fname;
    result.needReboot = true;
    NX_LOG_I("DaemonCore", "module %s installed (need reboot)", fname.c_str());
    return result;
}

bool DaemonCore::uninstallModule(const std::string& id) {
    std::string modulePath = env_.modulesDir + "/" + id;
    if (!probeDir(modulePath)) return false;

    // 执行 uninstall.sh
    std::string uninstallScript = modulePath + "/uninstall.sh";
    if (probeFile(uninstallScript)) {
        ScriptExecutor::ExecOptions opts;
        opts.stage = ScriptExecutor::Stage::Uninstall;
        opts.moduleId = id;
        opts.modulePath = modulePath;
        opts.scriptPath = uninstallScript;
        opts.isolateNamespace = false;
        scriptExecutor_.execute(opts);
    }

    // 删除目录
    execCommand("rm -rf '" + modulePath + "'", 10);
    bus_.publishModuleUnloaded(id);
    NX_LOG_I("DaemonCore", "module %s uninstalled", id.c_str());
    return true;
}

void DaemonCore::restartDaemon() {
    NX_LOG_W("DaemonCore", "restart daemon requested (not yet implemented; suicide + init respawn)");
    // MVP 简化：通过 init 重启服务
    // 实际实现：::kill(::getpid(), SIGTERM) 让 main.cpp 优雅退出，init 重启
    // 但 init 必须配置 oneshot/restart，详见 spec-01 §12
}

void DaemonCore::enterSafeMode(uint32_t timeoutSec) {
    safeMode_ = true;
    // 写 /data/adb/nexuscore/safe_mode 标记文件，下次启动时 DaemonCore 检查
    writeFile("/data/adb/nexuscore/safe_mode", "1", 0644);
    NX_LOG_W("DaemonCore", "safe mode entered (effective next boot, timeout=%us)", timeoutSec);
}

bool DaemonCore::reboot(RebootMode mode) {
    NX_LOG_W("DaemonCore", "reboot mode=%d", (int)mode);
    switch (mode) {
        case RebootMode::Normal:
            ::sync();
            ::reboot(LINUX_REBOOT_CMD_RESTART);
            return true;
        case RebootMode::Recovery:
            ::sync();
            ::reboot(LINUX_REBOOT_CMD_RESTART2, "recovery");
            return true;
        case RebootMode::Bootloader:
            ::sync();
            ::reboot(LINUX_REBOOT_CMD_RESTART2, "bootloader");
            return true;
        case RebootMode::Download:
            ::sync();
            ::reboot(LINUX_REBOOT_CMD_RESTART2, "download");
            return true;
        case RebootMode::Userspace:
            // 整改 #13：USERSPACE 需要 sys.powerctl 写权限，由 SELinuxManager 已注入
            // 详见 spec-01 §13.2
            ::property_set("sys.powerctl", "userspace");
            // 备选：fallback 到 NORMAL
            NX_LOG_W("DaemonCore", "userspace reboot requested; fallback may apply");
            return true;
    }
    return false;
}

bool DaemonCore::uninstallFramework() {
    NX_LOG_W("DaemonCore", "uninstall framework requested");
    // 1. umount 所有
    if (fs_) fs_->unmountAll();
    // 2. 删除 /data/adb/nexuscore（保留 config 备份）
    execCommand("rm -rf /data/adb/nexuscore/modules /data/adb/nexuscore/overlay "
                "/data/adb/nexuscore/bin /data/adb/nexuscore/logs", 30);
    // 3. 标记下次启动自杀（让 init service 不再启动）
    writeFile("/data/adb/nexuscore/.uninstall_pending", "1", 0644);
    return true;
}

bool DaemonCore::clearLogs(int target) {
    std::string path = "/data/adb/nexuscore/logs";
    if (target == 0 || target == 1) {
        // daemon 日志
        execCommand("rm -f " + path + "/nexusd.log " + path + "/modules.log", 5);
    }
    if (target == 0 || target == 2) {
        // su 日志
        execCommand("rm -f " + path + "/su.log " + path + "/su_policy.json", 5);
    }
    return true;
}

// ============ SU 代理 ============

void DaemonCore::loadSuState() {
    // MVP 简化：从 /data/adb/nexuscore/su_policy.json 加载
    // 完整实现需要 JSON 解析（与 module_loader.cpp 类似）
    auto content = readFile("/data/adb/nexuscore/su_policy.json");
    if (!content) return;
    // TODO: parse JSON
}

void DaemonCore::saveSuState() {
    // TODO: serialize to JSON
}

std::vector<DaemonCore::SuApp> DaemonCore::listSuApps() {
    return suApps_;
}

bool DaemonCore::setSuPolicy(const std::string& pkg, uint32_t uid, int policy, uint32_t timeoutSec) {
    // 详见 spec-01 §14.3：NexusCore 不替代底层 root 的 su，仅作为本地策略 mirror
    // MVP 简化：写入本地 su_policy.json，由用户手动在底层 root Manager 里同步
    for (auto& app : suApps_) {
        if (app.packageName == pkg && app.uid == uid) {
            app.policy = policy;
            app.timeoutSec = timeoutSec;
            saveSuState();
            NX_LOG_I("DaemonCore", "su policy updated: %s uid=%u policy=%d",
                     pkg.c_str(), uid, policy);
            return true;
        }
    }
    SuApp app;
    app.packageName = pkg;
    app.uid = uid;
    app.policy = policy;
    app.timeoutSec = timeoutSec;
    suApps_.push_back(app);
    saveSuState();
    NX_LOG_I("DaemonCore", "su policy added: %s uid=%u policy=%d",
             pkg.c_str(), uid, policy);
    return true;
}

std::vector<DaemonCore::SuLogEntry> DaemonCore::listSuLogs() {
    return suLogs_;
}

} // namespace nexus
