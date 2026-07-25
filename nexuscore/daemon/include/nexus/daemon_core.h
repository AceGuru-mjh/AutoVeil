#pragma once

#include "nexus/types.h"
#include "nexus/event_bus.h"
#include "nexus/env_detector.h"
#include "nexus/module_loader.h"
#include "nexus/script_executor.h"
#include "nexus/fs/i_file_system_interceptor.h"
#include "nexus/fs/fs_detector.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace nexus {

// Daemon 核心组件容器
//
// main.cpp 创建 DaemonCore，注入到 IPC handlers。
// DaemonCore 持有所有运行时状态：env / modules / fs interceptor / script executor / event bus
class DaemonCore {
public:
    explicit DaemonCore(RootEnvironment env);

    // 启动流程：
    // 1. 初始化 EventBus
    // 2. patchSelfDomain（不致命）
    // 3. scanModules
    // 4. 选择 FS interceptor，mountAll 所有模块 system/
    // 5. 执行 post-fs-data 脚本
    // 6. 注册 boot_completed 监听，触发 late_start 脚本
    Result<void> start();

    // 进入只读模式（root env 探测失败时调用）
    // 仅启动 IPC + 状态查询，不挂载不执行脚本
    Result<void> startReadOnly();

    // 优雅停止：umount + 等待脚本完成
    void stop();

    // ============ 状态查询（IPC handlers 调用） ============

    const RootEnvironment& env() const { return env_; }
    bool isReadOnly() const { return readOnly_; }
    bool isSafeMode() const { return safeMode_; }
    void setSafeMode(bool s) { safeMode_ = s; }
    // Phase 1.4 修复：running_ 现在是 atomic，需要 .load()
    bool isDaemonRunning() const { return running_.load(); }
    uint32_t daemonPid() const { return pid_; }
    std::string_view fsInterceptorName() const;
    uint32_t moduleCount() const { return modules_.size(); }
    uint64_t uptimeMs() const;
    const std::vector<ModuleLoader::LoadedModule>& modules() const { return modules_; }

    EventBus& bus() { return bus_; }

    // ============ 操作 ============

    // 启用/禁用模块（写入 manifest.enabled，下次启动生效）
    bool enableModule(const std::string& id);
    bool disableModule(const std::string& id);

    // 安装模块（解压 ZIP → verify → customize → 移动到 modules/<id>）
    struct InstallResult {
        std::string id;
        bool needReboot = false;
    };
    Result<InstallResult> installModule(const std::string& localZipPath);

    // 卸载模块（执行 uninstall.sh → umount → 删除目录）
    bool uninstallModule(const std::string& id);

    // 重启 daemon
    void restartDaemon();

    // 进入安全模式（下次启动跳过所有模块挂载与脚本）
    void enterSafeMode(uint32_t timeoutSec);

    // 重启系统
    enum class RebootMode { Normal, Userspace, Recovery, Bootloader, Download };
    bool reboot(RebootMode mode);

    // 卸载整个框架
    bool uninstallFramework();

    // 清除日志
    bool clearLogs(int target);   // 0=all 1=daemon 2=su

    // SU 相关（代理模式，详见 spec-01 §14.3）
    struct SuApp {
        std::string packageName;
        uint32_t uid;
        int policy;       // 0=deny 1=allow 2=allow_once
        uint64_t lastRequestMs;
        uint32_t requestCount;
        uint32_t timeoutSec;
    };
    std::vector<SuApp> listSuApps();
    bool setSuPolicy(const std::string& pkg, uint32_t uid, int policy, uint32_t timeoutSec);

    struct SuLogEntry {
        uint64_t timestampMs;
        std::string packageName;
        uint32_t uid;
        bool granted;
        std::string command;
    };
    std::vector<SuLogEntry> listSuLogs();

private:
    RootEnvironment env_;
    EventBus bus_;
    std::unique_ptr<IFileSystemInterceptor> fs_;
    ScriptExecutor scriptExecutor_;
    ModuleLoader loader_;
    std::vector<ModuleLoader::LoadedModule> modules_;

    bool readOnly_ = false;
    bool safeMode_ = false;
    // Phase 1.4 修复：原 running_ 是普通 bool，跨线程读写有数据竞争。
    // 改为 std::atomic<bool>。
    std::atomic<bool> running_{false};
    uint32_t pid_ = 0;
    uint64_t startTimeMs_ = 0;

    // SU 策略持久化（/data/adb/nexuscore/su_policy.json）
    std::vector<SuApp> suApps_;
    std::vector<SuLogEntry> suLogs_;
    void loadSuState();
    void saveSuState();

    // boot_completed 监听线程
    std::thread bootWatcher_;
    void watchBootCompleted();
};

} // namespace nexus
