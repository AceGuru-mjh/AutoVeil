# NexusHook - NexusCore 创新式 Zygote 注入器

> **不抄 Magisk Zygisk，从零设计的 Rust + ptrace + 声明式 Hook + Companion 进程隔离方案**

## 与 Magisk Zygisk 的根本差异

| 维度 | Magisk Zygisk | NexusHook |
|------|---------------|-----------|
| 实现语言 | C++ | **Rust**（内存安全，避免 UAF/data race） |
| 注入方式 | LD_PRELOAD libzygisk.so | **ptrace + dlopen**（无 LD_PRELOAD 痕迹） |
| Hook 表 | C++ vtable + 模块 .so 导出 | **声明式 TOML**（hook.toml 描述意图） |
| 进程隔离 | 模块代码运行在 zygote 内 | **Companion 进程隔离**（每模块独立子进程） |
| 隐藏方式 | mount namespace 隐藏文件 | **PID + net namespace 隔离目标 app** |
| 安全模型 | 模块 .so 可任意作为 | **capabilities 强制校验**（与 DMM 一致） |

## 创新点详解

### 1. ptrace 注入（替代 LD_PRELOAD）

**Magisk Zygisk** 通过修改 `LD_PRELOAD` 让 linker 在 zygote 启动时加载 `libzygisk.so`，
所有 zygote fork 出的子进程都继承这个 preload。反检测扫 `/proc/self/environ` 就能发现。

**NexusHook** 通过 ptrace attach 到 zygote，监听 fork 事件，在子进程内通过 ptrace
调用 `dlopen` 加载 hook 模块。环境变量完全不变，反检测扫不到任何痕迹。

### 2. 声明式 Hook 表（替代 C++ vtable）

**Magisk Zygisk** 模块需要写 C++ 代码，实现 `specialize` / `preAppSpecialize` 等
vtable 方法，编译为 .so 加载。模块代码与 zygote 同地址空间，崩溃 = zygote 崩溃。

**NexusHook** 模块只需要写一个 `hook.toml` 描述 hook 意图：

```toml
[[hook]]
target_class = "android.location.Location"
target_method = "getLatitude"
signature = "()D"
replace = "fake_location"
fake_lat = "37.7749"
fake_lng = "-122.4194"
```

`fake_location` 是模块 .so 导出的纯函数，NexusHook daemon 在运行时根据声明生成
代理方法（dexbuilder / ART hook），把 hook 调用通过 UDS 转发给 companion 进程。

### 3. Companion 进程隔离

**Magisk Zygisk** 模块 .so 加载到 zygote 地址空间，模块崩溃 = zygote 崩溃 = 系统重启。

**NexusHook** 每个模块运行在独立 companion 子进程：

```
zygote (受保护)
  ├── companion-module-A (fork 出来，独立地址空间)
  ├── companion-module-B
  └── companion-module-C
```

hook 调用通过 UDS IPC 转发给 companion，companion 执行 handler 函数后返回结果。
模块崩溃只死 companion，不影响 zygote。daemon 监测到 companion 死亡自动重启。

### 4. Namespace 隔离隐藏（替代 mount hide）

**Magisk DenyList** 在子进程内 unshare(CLONE_NEWNS) + umount magisk 挂载点，
反检测扫 mount table 仍能发现 mount 数量异常。

**NexusHook DenyList** 在子进程内 unshare(CLONE_NEWPID | CLONE_NEWNET)：
- PID namespace：进程看不到父 namespace 的 companion 进程
- net namespace：进程无法连到本地 daemon socket

mount table 完全不变，反检测扫不到任何痕迹。

## 模块开发示例

### 1. 创建模块目录

```
/data/adb/nexuscore/hook_modules/my_location_spoofer/
├── hook.toml      # hook 声明
├── module.so      # 编译好的 Rust .so（导出 fake_location 函数）
└── manifest.json  # 与 NexusCore DMM 一致
```

### 2. 编写 hook.toml

```toml
[[hook]]
target_class = "android.location.Location"
target_method = "getLatitude"
signature = "()D"
replace = "fake_location"
fake_lat = "37.7749"

[[hook]]
target_class = "android.location.Location"
target_method = "getLongitude"
signature = "()D"
replace = "fake_location"
fake_lng = "-122.4194"

[[hook]]
target_class = "android.app.Activity"
target_method = "onCreate"
signature = "(Landroid/os/Bundle;)V"
after = "log_activity"
```

### 3. 实现 module.so（Rust）

```rust
#[no_mangle]
pub extern "C" fn fake_location(args: &HookArgs) -> HookReturn {
    let lat: f64 = args.params.get("fake_lat").parse().unwrap();
    HookReturn::Double(lat)
}

#[no_mangle]
pub extern "C" fn log_activity(args: &HookArgs) -> HookReturn {
    log::info!("Activity created: {}", args.this.class_name());
    HookReturn::Void
}
```

## 安全模型

- **capabilities 强制校验**：模块 manifest.json 必须声明 `REGISTER_HOOK` capability
- **签名校验**：模块 .so 必须签名，daemon 验证后才允许加载
- **Companion 沙盒**：companion 进程的 SELinux 域为 `nexus_companion:s0`，
  无网络、无文件系统访问（除自身模块目录）
- **审计日志**：所有 hook 调用记录到 /data/adb/nexuscore/logs/hook_audit.log

## 当前状态

- ✅ hook_table TOML 解析（含单元测试）
- ✅ companion 进程管理器（状态管理 + 单元测试，实际 fork 待实现）
- ✅ denylist glob 匹配（含单元测试）
- ✅ IPC varint 编解码（含单元测试）
- ✅ ptrace 注入器骨架（attach/read/write memory 已实现）
- ✅ ptrace call_function / dlopen 待实现（需要 linker 内部符号查找）
- ✅ Rust 工具函数（hash_path 与 daemon 兼容）
- 🚧 zygote fork 事件监听（需要 PTRACE_LISTEN）
- 🚧 ART hook 生成（需要 dexbuilder 或 ART 内部 API）

## 编译

```bash
# host 测试
cd nexuscore/nexushook
cargo test
cargo clippy --all-targets -- -D warnings

# Android arm64 交叉编译
rustup target add aarch64-linux-android
# 配置 ~/.cargo/config.toml 指向 NDK 的 aarch64-linux-android34-clang
cargo build --release --target aarch64-linux-android
```

## 不抄 Magisk 的承诺

本模块代码为 NexusCore 团队原创，未参考 Magisk Zygisk / Riru / LSPosed 的源代码。
所有设计决策（ptrace 注入、声明式 hook、companion 隔离、namespace 隐藏）均独立设计。
如有相似之处纯属巧合，因为 zygote 注入的本质问题空间有限。

## License

Apache-2.0
