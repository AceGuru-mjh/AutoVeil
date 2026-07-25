#include "nexus/daemon_core.h"
#include "nexus/util.h"
#include "nexus/log.h"
#include "nexus/fs/fs_detector.h"
#include "nexus/selinux_manager.h"

#ifdef __ANDROID__
#include <cutils/properties.h>
#endif
#include <algorithm>
#include <chrono>
#include <dirent.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef NEXUS_VERSION
#define NEXUS_VERSION "1.0.0"
#endif

namespace nexus {

// ============ 安全工具：递归删除目录（不用 shell rm -rf） ============
// Phase 1.3 修复：原 uninstallModule 用 execCommand("rm -rf '" + path + "'")
// 存在 shell 注入风险（路径含单引号即可逃逸）。改用 nftw 风格的递归 unlink。
namespace {

bool removeRecursive(const std::string& path) {
    struct stat st{};
    if (::lstat(path.c_str(), &st) != 0) {
        return errno == ENOENT;   // 不存在视为成功
    }
    if (S_ISDIR(st.st_mode)) {
        DIR* d = ::opendir(path.c_str());
        if (!d) return false;
        struct dirent* e;
        while ((e = ::readdir(d))) {
            std::string name = e->d_name;
            if (name == "." || name == "..") continue;
            if (!removeRecursive(path + "/" + name)) {
                ::closedir(d);
                return false;
            }
        }
        ::closedir(d);
        return ::rmdir(path.c_str()) == 0;
    } else {
        return ::unlink(path.c_str()) == 0;
    }
}

} // anonymous namespace

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

    running_.store(true);
    NX_LOG_I("DaemonCore", "started");
    return {};
}

Result<void> DaemonCore::startReadOnly() {
    readOnly_ = true;
    NX_LOG_W("DaemonCore", "starting in READ-ONLY mode (no mount, no scripts)");
    fs_ = std::make_unique<NoopInterceptor>();
    running_.store(true);
    return {};
}

void DaemonCore::stop() {
    NX_LOG_I("DaemonCore", "stopping");
    running_.store(false);
    if (fs_) {
        fs_->unmountAll();
    }
    // Phase 1.4 修复：原代码 detach 而非 join，导致 bootWatcher_ 线程在 DaemonCore
    // 析构后仍访问成员 → UAF。改为 join（waitBootCompleted 的 sleep(1) 让最多 1s 退出）
    if (bootWatcher_.joinable()) {
        bootWatcher_.join();
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
        while (running_.load()) {
            char val[32] = {0};
#ifdef __ANDROID__
            ::property_get("sys.boot_completed", val, "0");
#else
            // host 测试时直接返回（永远不进入 late_start 分支）
            std::snprintf(val, sizeof(val), "0");
#endif
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
    // Phase 1.3 修复：原实现用 ZIP 文件名作为模块 ID，且通过 execCommand("unzip '...")
    // 与 execCommand("rm -rf '...") 存在 shell 注入漏洞。
    //
    // 新实现：
    // 1. 校验 ZIP 路径不含单引号（基础防御）
    // 2. 用 mkdtemp 创建不可预测的临时目录（防 TOCTOU）
    // 3. 用 execCommand 安全调用 unzip（路径不含 ' 即可，因为单引号是唯一可逃逸字符）
    // 4. 复用 ModuleLoader::parseManifestPublic 解析 manifest 拿到合法 ID
    // 5. 校验 ID 通过 isValidIdStatic（防注入到后续路径构造）
    // 6. 用 ::rename 移动目录（C API，不走 shell）
    // 7. 清理临时目录用 removeRecursive（C API，不走 shell）

    NX_LOG_I("DaemonCore", "installing module from %s", localZipPath.c_str());

    // 基础防御：拒绝含单引号的路径（防 shell 注入）
    if (localZipPath.find('\'') != std::string::npos) {
        NX_LOG_E("DaemonCore", "reject zip path with single quote: %s", localZipPath.c_str());
        return {unexpect, Err::InvalidArg};
    }

    // 用 mkdtemp 创建不可预测的临时目录
    std::string tmpTemplate = "/data/adb/nexuscore/tmp/install_XXXXXX";
    mkdirRecursive("/data/adb/nexuscore/tmp", 0700);
    std::vector<char> tmpBuf(tmpTemplate.begin(), tmpTemplate.end());
    tmpBuf.push_back('\0');
    char* tmpDirC = ::mkdtemp(tmpBuf.data());
    if (!tmpDirC) {
        NX_LOG_E("DaemonCore", "mkdtemp failed: %s", ::strerror(errno));
        return {unexpect, Err::IoError};
    }
    std::string tmpDir = tmpDirC;

    // 安全调用 unzip（路径已校验无单引号）
    std::string cmd = "unzip -o '" + localZipPath + "' -d '" + tmpDir + "' 2>&1";
    auto r = execCommand(cmd, 60);
    if (r.exitCode != 0) {
        NX_LOG_E("DaemonCore", "unzip failed: %s", r.stderr_.c_str());
        removeRecursive(tmpDir);
        return {unexpect, Err::IoError};
    }

    // 复用 ModuleLoader 解析 manifest
    std::string manifestPath = tmpDir + "/manifest.json";
    ModuleLoader loader(env_, env_.modulesDir);
    auto mr = loader.parseManifest(manifestPath);
    if (!mr) {
        NX_LOG_E("DaemonCore", "manifest parse failed: %s", errString(mr.error()));
        removeRecursive(tmpDir);
        return {unexpect, mr.error()};
    }

    // 严格校验 ID
    if (!ModuleLoader::isValidIdStatic(mr->id)) {
        NX_LOG_E("DaemonCore", "manifest id invalid: %s", mr->id.c_str());
        removeRecursive(tmpDir);
        return {unexpect, Err::InvalidArg};
    }

    std::string finalDir = env_.modulesDir + "/" + mr->id;
    mkdirRecursive(env_.modulesDir, 0755);

    // 如果已存在，先递归删除（用 C API，不走 shell）
    if (probeDir(finalDir)) {
        NX_LOG_I("DaemonCore", "module %s already exists, removing old version", mr->id.c_str());
        if (!removeRecursive(finalDir)) {
            NX_LOG_W("DaemonCore", "remove old module dir failed; continue anyway");
        }
    }

    if (::rename(tmpDir.c_str(), finalDir.c_str()) < 0) {
        NX_LOG_E("DaemonCore", "rename %s -> %s failed: %s",
                 tmpDir.c_str(), finalDir.c_str(), ::strerror(errno));
        removeRecursive(tmpDir);
        return {unexpect, Err::IoError};
    }

    InstallResult result;
    result.id = mr->id;
    result.needReboot = true;
    NX_LOG_I("DaemonCore", "module %s v%s installed (need reboot)",
             mr->id.c_str(), mr->version.c_str());
    return result;
}

bool DaemonCore::uninstallModule(const std::string& id) {
    // Phase 1.3 修复：原用 execCommand("rm -rf '" + modulePath + "'") 存在 shell 注入。
    // 改用 removeRecursive（C API 递归 unlink），不依赖 shell。
    //
    // 同时校验 id 通过 isValidIdStatic，防止构造恶意路径。

    if (!ModuleLoader::isValidIdStatic(id)) {
        NX_LOG_E("DaemonCore", "reject uninstall: invalid module id: %s", id.c_str());
        return false;
    }

    std::string modulePath = env_.modulesDir + "/" + id;
    if (!probeDir(modulePath)) return false;

    // 执行 uninstall.sh（用 scriptExecutor 走 fork+execve，不走 shell -c）
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

    // 递归删除目录（C API，无 shell 注入）
    if (!removeRecursive(modulePath)) {
        NX_LOG_W("DaemonCore", "removeRecursive failed for %s; some files may remain",
                 modulePath.c_str());
    }
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
    // Phase 1.7 修复：
    // - 原 ::reboot(LINUX_REBOOT_CMD_RESTART2, "recovery") 是 2 参数调用，
    //   但 reboot(2) 的 4 参数签名是 reboot(magic1, magic2, cmd, arg)。
    //   Android NDK <sys/reboot.h> 同时提供 1 参与 4 参版本，2 参是错误的。
    // - 原代码 return true 在失败时也返回 true（::reboot 失败返回 -1）
    // 改为：用 __reboot 4 参数版本 + 检查返回值
    ::sync();
    int r = -1;
    switch (mode) {
        case RebootMode::Normal:
            r = ::reboot(LINUX_REBOOT_CMD_RESTART);
            break;
        case RebootMode::Recovery:
            // __reboot 是 Android bionic 提供的 4 参数版本
            r = syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
                       LINUX_REBOOT_CMD_RESTART2, "recovery");
            break;
        case RebootMode::Bootloader:
            r = syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
                       LINUX_REBOOT_CMD_RESTART2, "bootloader");
            break;
        case RebootMode::Download:
            r = syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
                       LINUX_REBOOT_CMD_RESTART2, "download");
            break;
        case RebootMode::Userspace:
            // 整改 #13：USERSPACE 需要 sys.powerctl 写权限，由 SELinuxManager 已注入
            // 详见 spec-01 §13.2
#ifdef __ANDROID__
            ::property_set("sys.powerctl", "userspace");
            NX_LOG_W("DaemonCore", "userspace reboot requested; fallback may apply");
            return true;
#else
            // host 测试时不实际 reboot
            return true;
#endif
    }
    if (r < 0) {
        NX_LOG_E("DaemonCore", "reboot failed: %s", ::strerror(errno));
        return false;
    }
    return true;   // 实际 reboot 成功不会到这
}

bool DaemonCore::uninstallFramework() {
    NX_LOG_W("DaemonCore", "uninstall framework requested");
    // 1. umount 所有
    if (fs_) fs_->unmountAll();
    // 2. 删除 /data/adb/nexuscore 子目录（用 C API，不走 shell）
    // Phase 1.3 修复：原用 execCommand("rm -rf ...") 存在路径硬编码虽无注入但有可靠性问题
    removeRecursive("/data/adb/nexuscore/modules");
    removeRecursive("/data/adb/nexuscore/overlay");
    removeRecursive("/data/adb/nexuscore/bin");
    removeRecursive("/data/adb/nexuscore/logs");
    // 3. 标记下次启动自杀（让 init service 不再启动）
    writeFile("/data/adb/nexuscore/.uninstall_pending", "1", 0644);
    return true;
}

bool DaemonCore::clearLogs(int target) {
    // Phase 1.3 修复：原用 execCommand("rm -f ...") 改用 ::unlink（C API）
    std::string path = "/data/adb/nexuscore/logs";
    if (target == 0 || target == 1) {
        ::unlink((path + "/nexusd.log").c_str());
        ::unlink((path + "/modules.log").c_str());
    }
    if (target == 0 || target == 2) {
        ::unlink((path + "/su.log").c_str());
        ::unlink((path + "/su_policy.json").c_str());
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
