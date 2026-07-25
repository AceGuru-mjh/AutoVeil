//! NexusHook - NexusCore 创新式 Zygote 注入器
//!
//! ## 设计哲学（与 Magisk Zygisk 的根本差异）
//!
//! | 维度 | Magisk Zygisk | NexusHook |
//! |------|---------------|-----------|
//! | 实现语言 | C++ | **Rust**（内存安全） |
//! | 注入方式 | LD_PRELOAD libzygisk.so | **ptrace + dlopen**（更难被反检测发现） |
//! | Hook 表 | C++ vtable + 模块 .so 导出 | **声明式 TOML**（hook.toml 描述意图） |
//! | 进程隔离 | 模块代码运行在 zygote 进程内 | **Companion 进程隔离**（每模块独立子进程） |
//! | 隐藏方式 | mount namespace 隐藏文件 | **PID + net namespace 隔离目标 app** |
//! | 安全模型 | 模块 .so 可任意作为 | **capabilities 强制校验**（与 NexusCore DMM 一致） |
//!
//! ## 工作流程
//!
//! 1. **zygote 启动期**：NexusHook 作为 init service 启动，ptrace attach 到 zygote
//! 2. **fork 监听**：通过 ptrace 监听 zygote 的 fork 调用
//! 3. **目标匹配**：每个新 fork 的子进程，根据 hook.toml 判断是否需要注入
//! 4. **dlopen 注入**：通过 ptrace 在子进程内调用 dlopen 加载模块 .so
//! 5. **Companion 派生**：模块 .so 的 init 函数 fork 出 companion 子进程
//! 6. **IPC 通信**：子进程与 companion 通过 UDS 通信，hook 调用转发给 companion
//!
//! ## 创新点
//!
//! - **进程隔离**：Magisk 模块崩溃会拖死 zygote，NexusHook 模块崩溃只死 companion
//! - **声明式 Hook**：模块不需要写 C++ 代码，只需要 TOML 描述意图，daemon 生成代理
//! - **namespace 隐藏**：不修改 mount 表（更难被检测），改用 PID/net namespace 隔离
//! - **Rust 内存安全**：避免 Zygisk 常见的 use-after-free / data race

#![deny(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]
#![warn(clippy::all)]

pub mod art_hook_generator;
pub mod companion;
pub mod companion_process;
pub mod denylist;
pub mod hook_table;
pub mod ipc;
pub mod ptrace_injector;
pub mod selinux;
pub mod util;
pub mod zygote_watcher;

pub use art_hook_generator::ArtHookGenerator;
pub use companion::CompanionManager;
pub use denylist::DenyList;
pub use hook_table::{HookEntry, HookTable, HookTarget};
pub use ptrace_injector::PtraceInjector;
pub use zygote_watcher::ZygoteWatcher;
