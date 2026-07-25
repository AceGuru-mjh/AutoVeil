# NexusCore Daemon (nexusd)

Android 14-16 用户态模块运行时守护进程。详见 [Spec 01](../specs/spec-01-daemon.md)。

## 状态

**MVP 实现**：当前代码是骨架级实现，覆盖：
- ✅ Root 环境探测（Magisk / KSU / APatch）
- ✅ SELinux 策略注入（通过底层 root 的 policy tool）
- ✅ 模块清单（manifest.json）解析与 capabilities 校验
- ✅ OverlayFS / Bind Mount 文件系统拦截器（修复原 spec 中的 lowerdir/link bug）
- ✅ 脚本执行器（独立 mount namespace + Magisk 兼容 shim）
- ✅ UDS IPC server + SO_PEERCRED 凭证校验（修复 untrusted_app 前缀匹配 + socket 权限 catch-22）
- ✅ 事件总线（LOG_LINE / MODULE_LOADED / SCRIPT_DONE / SU_REQUEST / DAEMON_READY）
- ✅ DaemonCore 启动/停止流程（post-fs-data + boot_completed + safe mode）
- ⚠️ RPC handlers 大部分为占位（返回 dummy 数据），需要逐步对接 DaemonCore
- ⚠️ SU 代理模式仅本地策略持久化，未与底层 root 联动

## 构建

```bash
# 前置：NDK r26d+，CMake ≥ 3.22
export NDK=/path/to/android-ndk
cmake -B build-arm64 \
    -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-34 \
    -DANDROID_STL=c++_static
cmake --build build-arm64 -j

# 输出
# build-arm64/nexusd
# build-arm64/nexuscli
```

## 部署

通过 Magisk 模块包装：

```bash
# 推送到设备
adb push build-arm64/nexusd /data/local/tmp/nexusd
adb shell "chmod 755 /data/local/tmp/nexusd"
adb shell "cp /data/local/tmp/nexusd /data/adb/nexuscore/bin/nexusd"
adb shell "chmod 755 /data/adb/nexuscore/bin/nexusd"
adb reboot
```

详见 [Spec 01 §12](../specs/spec-01-daemon.md#12-部署与测试)。

## 测试

```bash
# 单元测试（host 编译时启用）
cmake -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j
cd build-host && ctest
```

## 目录结构

```
daemon/
├── CMakeLists.txt
├── include/nexus/
│   ├── types.h               # Result<T,E> + 基础类型
│   ├── log.h                 # 日志宏
│   ├── util.h                # 文件/字符串/进程工具
│   ├── env_detector.h        # Root 环境探测
│   ├── selinux_manager.h     # SELinux 策略管理
│   ├── module_loader.h       # 模块清单解析
│   ├── script_executor.h     # 脚本沙盒执行
│   ├── event_bus.h           # 事件总线
│   ├── daemon_core.h         # 核心组件容器
│   ├── fs/                   # 文件系统拦截器
│   │   ├── i_file_system_interceptor.h
│   │   ├── overlayfs_interceptor.h
│   │   ├── bind_mount_interceptor.h
│   │   ├── noop_interceptor.h
│   │   └── fs_detector.h
│   └── ipc/                  # IPC server
│       ├── codec.h
│       ├── credential_check.h
│       ├── manager_uid_resolver.h
│       ├── ipc_server.h
│       └── handlers.h
├── src/                      # 实现源文件（与 include 一一对应）
├── proto/nexus.proto         # 与 manager 共享（见 manager/app/src/main/proto/）
├── scripts/                  # 安装/调试脚本
└── tests/                    # 单元测试
```

## 已知限制

1. **RPC handlers 占位**：除 Ping/GetStatus 外，大部分 RPC 返回 dummy 数据，需要后续对接 DaemonCore 真实逻辑。
2. **JSON 解析简化**：手写极简 JSON parser，不支持完整 JSON 规范（如 Unicode 转义、科学计数法）。生产建议换 nlohmann/json。
3. **APK 签名校验未实现**：CredentialCheck::authorize 跳过签名校验，仅靠 UID + 包名 + SELinux 域。生产需补完。
4. **SU 代理模式未联动**：setSuPolicy 仅写本地 su_policy.json，未与底层 root 联动。
5. **超时机制简化**：ScriptExecutor 用 waitpid 阻塞，未实现精确超时（生产应 SIGCHLD + timer）。
6. **Reboot USERSPACE 依赖底层 root**：sys.powerctl 写权限由 SELinuxManager 注入，失败时回退 NORMAL。
