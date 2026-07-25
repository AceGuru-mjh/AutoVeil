//! Zygote fork 监听器
//!
//! 监听 zygote 的 fork 事件，根据 hook.toml 与 denylist 决定是否注入。
//!
//! 实现策略（创新点）：
//! 1. attach 到 zygote 进程
//! 2. 设置 PTRACE_O_TRACEFORK | TRACEVFORK | TRACECLONE 选项
//! 3. zygote fork 子进程时，子进程自动 STOP 并附带 PTRACE_EVENT_FORK 事件
//! 4. 解析子进程的 cmdline 拿包名
//! 5. 检查 denylist，命中则 unshare 隔离
//! 6. 否则查找匹配的 hook 表，调用 dlopen 注入模块
//!
//! 注意：Android zygote 是特殊进程，attach 需要以下条件：
//! - root 权限（CAP_SYS_PTRACE）
//! - SELinux 域允许 ptrace zygote（需要 magiskpolicy 注入）
//! - yama.lsm_scope = 0 或 daemon 是 zygote 的祖先

use std::collections::HashMap;
use std::path::Path;
use std::time::Duration;

use crate::ptrace_injector::PtraceInjector;
use crate::hook_table::{HookTable, HookEntry};
use crate::companion::CompanionManager;
use crate::denylist::DenyList;
use crate::util;

/// Zygote 监听器
pub struct ZygoteWatcher {
    /// 已加载的 hook 模块表（module_id → (HookTable, so_path)）
    pub hook_modules: Vec<(String, HookTable, std::path::PathBuf)>,
    /// Companion 管理器
    pub companions: CompanionManager,
    /// DenyList
    pub denylist: DenyList,
    /// 是否正在运行
    running: std::sync::atomic::AtomicBool,
}

impl ZygoteWatcher {
    pub fn new(companion_socket_dir: impl Into<String>) -> Self {
        Self {
            hook_modules: Vec::new(),
            companions: CompanionManager::new(companion_socket_dir),
            denylist: DenyList::new(),
            running: std::sync::atomic::AtomicBool::new(false),
        }
    }

    /// 加载所有 hook 模块
    pub fn load_modules(&mut self, modules_dir: &str) -> std::io::Result<()> {
        self.hook_modules.clear();
        let path = Path::new(modules_dir);
        if !path.is_dir() {
            return Ok(());
        }
        for entry in std::fs::read_dir(path)? {
            let entry = entry?;
            if !entry.file_type()?.is_dir() {
                continue;
            }
            let module_id = entry.file_name().to_string_lossy().to_string();
            let hook_toml = entry.path().join("hook.toml");
            let so_path = entry.path().join("module.so");
            if !hook_toml.exists() || !so_path.exists() {
                continue;
            }
            match std::fs::read_to_string(&hook_toml) {
                Ok(content) => {
                    match HookTable::parse_toml(&content) {
                        Ok(table) => {
                            let warnings = table.validate();
                            if warnings.is_empty() {
                                let count = table.entries.len();
                                self.hook_modules.push((module_id.clone(), table, so_path));
                                eprintln!("[nexushook] loaded module {} ({} hooks)",
                                          module_id, count);
                            } else {
                                eprintln!("[nexushook] skip module {}: {} warnings",
                                          module_id, warnings.len());
                            }
                        }
                        Err(e) => eprintln!("[nexushook] parse error for {}: {}", module_id, e),
                    }
                }
                Err(e) => eprintln!("[nexushook] read error for {}: {}", module_id, e),
            }
        }
        Ok(())
    }

    /// 启动 zygote 监听
    ///
    /// 1. 找到 zygote PID
    /// 2. attach
    /// 3. 设置 tracefork 选项
    /// 4. 主循环：监听 fork 事件
    pub fn start(&self, zygote_pid: i32) -> std::io::Result<()> {
        self.running.store(true, std::sync::atomic::Ordering::SeqCst);

        eprintln!("[nexushook] attaching to zygote pid={}", zygote_pid);
        let injector = PtraceInjector::attach(zygote_pid)?;

        // 设置 PTRACE_O_TRACEFORK | TRACEVFORK | TRACECLONE
        // 这样 zygote fork 子进程时，子进程自动 STOP
        set_trace_options(zygote_pid)?;

        // 主循环
        while self.running.load(std::sync::atomic::Ordering::SeqCst) {
            // 等待下一个事件
            match wait_for_any_child(zygote_pid) {
                Ok(Some(child_pid)) => {
                    self.handle_forked_child(child_pid)?;
                }
                Ok(None) => {
                    // 超时，继续
                    std::thread::sleep(Duration::from_millis(100));
                }
                Err(e) => {
                    eprintln!("[nexushook] wait error: {}", e);
                    break;
                }
            }
        }

        // detach
        injector.detach()?;
        eprintln!("[nexushook] detached from zygote");
        Ok(())
    }

    /// 停止监听
    pub fn stop(&self) {
        self.running.store(false, std::sync::atomic::Ordering::SeqCst);
    }

    /// 处理 fork 出的子进程
    fn handle_forked_child(&self, child_pid: i32) -> std::io::Result<()> {
        // 读取子进程的 cmdline 拿包名
        let cmdline_path = format!("/proc/{}/cmdline", child_pid);
        let cmdline = std::fs::read_to_string(&cmdline_path).unwrap_or_default();
        let package = cmdline.split('\0').next().unwrap_or("").to_string();

        if package.is_empty() {
            // 包名为空，可能是系统进程，跳过
            return Ok(());
        }

        eprintln!("[nexushook] forked child pid={} pkg={}", child_pid, package);

        // 读取子进程的 UID（从 /proc/<pid>/status）
        let uid = read_process_uid(child_pid).unwrap_or(0);

        // 检查 denylist
        if self.denylist.should_isolate(&package, uid) {
            eprintln!("[nexushook] isolating {} (uid={})", package, uid);
            // 注入隔离代码（unshare NEWPID + NEWNET）
            // 实际实现：通过 ptrace 在子进程内调用 unshare()
            return self.isolate_child(child_pid);
        }

        // 查找匹配的 hook 模块
        for (module_id, hook_table, so_path) in &self.hook_modules {
            // 模块加载到 zygote 的所有子进程（除非在 denylist）
            // 实际生产应支持模块过滤（只 hook 特定包名）
            eprintln!("[nexushook] injecting module {} into pid={} pkg={}",
                      module_id, child_pid, package);

            // 注入 .so 到子进程
            match self.inject_so_into_child(child_pid, so_path, module_id) {
                Ok(handle) => {
                    eprintln!("[nexushook] injected {} handle=0x{:x}", module_id, handle);
                    // 启动 companion 进程
                    if let Err(e) = self.companions.start(module_id, so_path.clone()) {
                        eprintln!("[nexushook] start companion for {} failed: {}",
                                  module_id, e);
                    }
                }
                Err(e) => {
                    eprintln!("[nexushook] inject {} failed: {}", module_id, e);
                }
            }
        }

        Ok(())
    }

    /// 通过 ptrace 在子进程内调用 dlopen 加载 .so
    fn inject_so_into_child(&self, child_pid: i32, so_path: &Path, _module_id: &str)
        -> std::io::Result<u64>
    {
        // 子进程已 STOP（被 PTRACE_O_TRACEFORK 触发）
        let injector = PtraceInjector::attach(child_pid)?;
        let so_path_str = so_path.to_string_lossy().to_string();
        let result = injector.dlopen(&so_path_str);
        // 注入后 detach，让子进程继续
        injector.detach()?;
        result
    }

    /// 在子进程内执行 namespace 隔离（unshare NEWPID + NEWNET）
    fn isolate_child(&self, child_pid: i32) -> std::io::Result<()> {
        // 通过 ptrace 在子进程内调用 unshare(CLONE_NEWPID | CLONE_NEWNET)
        let injector = PtraceInjector::attach(child_pid)?;

        // 找 unshift 函数地址（在 libc.so 中）
        let unshare_addr = find_unshare_address(child_pid)?;
        if unshare_addr == 0 {
            injector.detach()?;
            return Err(std::io::Error::new(
                std::io::ErrorKind::NotFound,
                "unshare symbol not found",
            ));
        }

        // CLONE_NEWPID = 0x20000000, CLONE_NEWNET = 0x40000000
        let flags: u64 = 0x20000000 | 0x40000000;
        let _ = injector.call_function(unshare_addr, &[flags])?;
        injector.detach()?;
        Ok(())
    }
}

/// 设置 ptrace trace 选项（TRACEFORK 等）
fn set_trace_options(pid: i32) -> std::io::Result<()> {
    // PTRACE_SETOPTIONS = 0x4200
    // PTRACE_O_TRACEFORK = 1 << 1 = 2
    // PTRACE_O_TRACEVFORK = 1 << 2 = 4
    // PTRACE_O_TRACECLONE = 1 << 3 = 8
    // PTRACE_O_TRACEEXEC = 1 << 4 = 16
    let options: u64 = 2 | 4 | 8 | 16;
    let r = unsafe {
        ptrace(0x4200 /* PTRACE_SETOPTIONS */, pid, 0, options as *mut _)
    };
    if r < 0 {
        return Err(std::io::Error::last_os_error());
    }
    Ok(())
}

/// 等待任意子进程停止
fn wait_for_any_child(pid: i32) -> std::io::Result<Option<i32>> {
    let mut status: i32 = 0;
    let r = unsafe { waitpid(-1, &mut status, 1 /* WNOHANG */) };
    if r < 0 {
        let err = std::io::Error::last_os_error();
        if err.kind() == std::io::ErrorKind::ChildProcessNotFound {
            return Ok(None);
        }
        return Err(err);
    }
    if r == 0 {
        return Ok(None);
    }
    if r == pid {
        // zygote 本身状态变化（异常）
        return Err(std::io::Error::new(
            std::io::ErrorKind::Other,
            "zygote process state changed unexpectedly",
        ));
    }
    // r 是新 fork 的子进程 PID
    Ok(Some(r))
}

/// 读取进程 UID
fn read_process_uid(pid: i32) -> std::io::Result<u32> {
    let status = std::fs::read_to_string(format!("/proc/{}/status", pid))?;
    for line in status.lines() {
        if line.starts_with("Uid:") {
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.len() >= 2 {
                return parts[1].parse().map_err(|_| std::io::Error::new(
                    std::io::ErrorKind::InvalidData,
                    "invalid uid",
                ));
            }
        }
    }
    Err(std::io::Error::new(std::io::ErrorKind::NotFound, "Uid line not found"))
}

/// 查找 unshare 符号地址（在 libc.so 中）
fn find_unshare_address(pid: i32) -> std::io::Result<u64> {
    // 复用 ptrace_injector 的 find_symbol_in_lib 逻辑
    // （为简化代码，这里直接调 ptrace_injector 的内部函数，需要把它公开）
    // 实际实现：把 find_symbol_in_lib 移到 util 或单独的 module 中
    crate::ptrace_injector::find_unshare_in_libc(pid)
}

// FFI
#[allow(non_camel_case_types)]
type libc_void = std::ffi::c_void;

extern "C" {
    fn ptrace(request: i32, pid: i32, addr: *mut libc_void, data: *mut libc_void) -> i64;
    fn waitpid(pid: i32, status: *mut i32, options: i32) -> i32;
}

unsafe fn ptrace(request: i32, pid: i32, addr: *mut libc_void, data: *mut libc_void) -> i64 {
    ptrace(request, pid, addr, data)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_zygote_watcher_create() {
        let w = ZygoteWatcher::new("/tmp/test_nexushook");
        assert!(w.hook_modules.is_empty());
        assert!(!w.running.load(std::sync::atomic::Ordering::SeqCst));
    }

    #[test]
    fn test_load_modules_nonexistent_dir() {
        let mut w = ZygoteWatcher::new("/tmp/test_nexushook");
        // 不存在的目录应返回 Ok（空模块列表）
        assert!(w.load_modules("/tmp/nonexistent_xyz").is_ok());
        assert!(w.hook_modules.is_empty());
    }

    #[test]
    fn test_read_process_uid_nonexistent() {
        let r = read_process_uid(999999);
        assert!(r.is_err());
    }
}
