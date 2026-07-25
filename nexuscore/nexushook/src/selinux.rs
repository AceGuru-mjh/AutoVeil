//! SELinux 辅助函数
//!
//! 让 NexusHook 自身能运行在合适的 SELinux 域内。
//! 实际策略注入委托给 NexusCore daemon（spec-01 §4）。

use std::fs;
use std::io;

/// 读取当前进程的 SELinux context
pub fn current_context() -> io::Result<String> {
    fs::read_to_string("/proc/self/attr/current")
        .map(|s| s.trim().to_string())
}

/// 读取 /sys/fs/selinux/enforce 判断是否 enforcing
pub fn is_enforcing() -> bool {
    fs::read_to_string("/sys/fs/selinux/enforce")
        .map(|s| s.trim() == "1")
        .unwrap_or(true)
}

/// 设置当前进程的 SELinux context（需要权限）
pub fn set_context(ctx: &str) -> io::Result<()> {
    fs::write("/proc/self/attr/exec", ctx)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_is_enforcing_returns_bool() {
        // host 测试环境可能读不到 /sys/fs/selinux/enforce，应返回 true（保守）
        let _ = is_enforcing();
    }
}
