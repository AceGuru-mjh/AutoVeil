//! Companion 进程隔离
//!
//! 创新点：每个 hook 模块运行在独立子进程，崩溃不影响 zygote。
//!
//! Magisk Zygisk 中，模块 .so 加载到 zygote 进程地址空间，模块崩溃 = zygote 崩溃。
//! NexusHook 把每个模块运行在 companion 子进程：
//!
//! ```text
//! zygote (受保护)
//!   ├── companion-module-A (fork 出来，独立地址空间)
//!   ├── companion-module-B
//!   └── companion-module-C
//! ```
//!
//! hook 调用通过 UDS IPC 转发给 companion：
//! 1. zygote 内的 trampoline 收到 hook 调用
//! 2. 序列化参数，通过 UDS 发送给 companion
//! 3. companion 执行 handler 函数
//! 4. 结果返回给 zygote trampoline
//! 5. trampoline 把结果传给原方法（或替换原方法返回值）

use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Mutex;

/// 单个 companion 进程信息
#[derive(Debug)]
pub struct Companion {
    pub module_id: String,
    pub so_path: PathBuf,
    pub pid: Option<u32>,
    pub socket_path: String,
    pub alive: bool,
}

/// Companion 管理器（每个模块一个 companion）
pub struct CompanionManager {
    companions: Mutex<HashMap<String, Companion>>,
    socket_dir: String,
}

impl CompanionManager {
    pub fn new(socket_dir: impl Into<String>) -> Self {
        Self {
            companions: Mutex::new(HashMap::new()),
            socket_dir: socket_dir.into(),
        }
    }

    /// 启动指定模块的 companion（若已存在且 alive 则返回 existing）
    pub fn start(&self, module_id: &str, so_path: PathBuf) -> std::io::Result<()> {
        let mut comps = self.companions.lock().unwrap();
        if let Some(c) = comps.get(module_id) {
            if c.alive {
                return Ok(());
            }
        }
        let socket_path = format!("{}/nexushook_{}.sock", self.socket_dir, module_id);
        // TODO: 实际 fork + exec companion 进程
        // 1. socketpair 创建 UDS
        // 2. fork
        // 3. child: dlopen so_path, 进入 IPC 循环
        // 4. parent: 记录 pid
        let companion = Companion {
            module_id: module_id.to_string(),
            so_path,
            pid: None, // TODO: 实际 fork
            socket_path,
            alive: true,
        };
        comps.insert(module_id.to_string(), companion);
        Ok(())
    }

    /// 停止指定模块的 companion
    pub fn stop(&self, module_id: &str) -> std::io::Result<()> {
        let mut comps = self.companions.lock().unwrap();
        if let Some(c) = comps.get_mut(module_id) {
            if let Some(pid) = c.pid {
                // SAFETY: kill 是 syscall，pid > 0
                unsafe {
                    libc_kill(pid as i32, 15 /* SIGTERM */);
                }
            }
            c.alive = false;
        }
        Ok(())
    }

    /// 停止所有 companion（daemon 退出时调用）
    pub fn stop_all(&self) {
        let mut comps = self.companions.lock().unwrap();
        for c in comps.values_mut() {
            if let Some(pid) = c.pid {
                unsafe {
                    libc_kill(pid as i32, 15);
                }
            }
            c.alive = false;
        }
    }

    /// 检查 companion 是否存活
    pub fn is_alive(&self, module_id: &str) -> bool {
        let comps = self.companions.lock().unwrap();
        comps.get(module_id).map(|c| c.alive).unwrap_or(false)
    }

    /// 列出所有 companion
    pub fn list(&self) -> Vec<String> {
        let comps = self.companions.lock().unwrap();
        comps.keys().cloned().collect()
    }

    /// 重启崩溃的 companion
    pub fn restart_if_dead(&self, module_id: &str) -> std::io::Result<bool> {
        let is_alive = self.is_alive(module_id);
        if is_alive {
            return Ok(false);
        }
        let so_path = {
            let comps = self.companions.lock().unwrap();
            comps.get(module_id).map(|c| c.so_path.clone())
        };
        if let Some(path) = so_path {
            self.start(module_id, path)?;
            return Ok(true);
        }
        Ok(false)
    }
}

impl Drop for CompanionManager {
    fn drop(&mut self) {
        self.stop_all();
    }
}

extern "C" {
    fn kill(pid: i32, sig: i32) -> i32;
}

unsafe fn libc_kill(pid: i32, sig: i32) -> i32 {
    unsafe { kill(pid, sig) }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_companion_manager_create() {
        let mgr = CompanionManager::new("/tmp/test_nexushook");
        assert!(mgr.list().is_empty());
    }

    #[test]
    fn test_start_stop_companion() {
        let mgr = CompanionManager::new("/tmp/test_nexushook");
        // 不实际 fork，只测试状态管理
        let result = mgr.start("test_mod", PathBuf::from("/tmp/test.so"));
        assert!(result.is_ok());
        assert!(mgr.is_alive("test_mod"));
        assert!(mgr.list().contains(&"test_mod".to_string()));

        mgr.stop("test_mod").unwrap();
        assert!(!mgr.is_alive("test_mod"));
    }

    #[test]
    fn test_multiple_companions() {
        let mgr = CompanionManager::new("/tmp/test_nexushook");
        mgr.start("mod_a", PathBuf::from("/tmp/a.so")).unwrap();
        mgr.start("mod_b", PathBuf::from("/tmp/b.so")).unwrap();
        mgr.start("mod_c", PathBuf::from("/tmp/c.so")).unwrap();
        assert_eq!(mgr.list().len(), 3);
        mgr.stop_all();
        assert!(!mgr.is_alive("mod_a"));
        assert!(!mgr.is_alive("mod_b"));
        assert!(!mgr.is_alive("mod_c"));
    }
}
