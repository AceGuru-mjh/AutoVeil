//! DenyList - namespace 隔离实现
//!
//! 创新点：不修改 mount 表，改用 PID + net namespace 隔离目标 app。
//!
//! Magisk DenyList 的工作方式：
//! - 监听 zygote fork
//! - 在 fork 出的子进程内 unshare(CLONE_NEWNS) 创建新 mount namespace
//! - umount magisk 相关挂载点
//! - 缺点：mount table 痕迹可被反检测扫描
//!
//! NexusHook DenyList 的工作方式：
//! - 监听 zygote fork
//! - 在 fork 出的子进程内：
//!   1. unshare(CLONE_NEWPID | CLONE_NEWNET) 创建新 PID + net namespace
//!   2. 不再注入 hook
//!   3. 进程视角内看不到父进程的 hook companion socket
//! - 优点：
//!   - mount table 完全不变，反检测扫不到
//!   - 进程网络隔离，无法连到本地 daemon socket
//!   - PID namespace 隔离，看不到其他 hook 模块的 companion 进程

use std::collections::HashSet;

/// DenyList 配置
pub struct DenyList {
    /// 命中即隔离的包名（支持 glob，如 com.android.*）
    packages: HashSet<String>,
    /// 命中即隔离的 UID
    uids: HashSet<u32>,
}

impl DenyList {
    pub fn new() -> Self {
        Self {
            packages: HashSet::new(),
            uids: HashSet::new(),
        }
    }

    /// 添加包名（支持通配符，如 "com.example.*"）
    pub fn add_package(&mut self, pkg: &str) {
        self.packages.insert(pkg.to_string());
    }

    /// 添加 UID
    pub fn add_uid(&mut self, uid: u32) {
        self.uids.insert(uid);
    }

    /// 检查指定包名 + UID 是否应该隔离
    pub fn should_isolate(&self, package: &str, uid: u32) -> bool {
        if self.uids.contains(&uid) {
            return true;
        }
        // glob 匹配
        for pat in &self.packages {
            if glob_match(pat, package) {
                return true;
            }
        }
        false
    }

    /// 在当前进程内执行隔离（unshare + 配置）
    ///
    /// SAFETY: unshare 是 syscall，但修改了进程的 namespace 归属，调用后
    /// 该进程的视角会变化（看不到父 namespace 的资源）。仅在 zygote fork 后
    /// 的子进程内调用。
    pub unsafe fn isolate_current_process(&self) -> std::io::Result<()> {
        // SAFETY: caller 必须保证仅在新 fork 的子进程内调用
        const CLONE_NEWPID: i32 = 0x20000000;
        const CLONE_NEWNET: i32 = 0x40000000;
        let r = unsafe { libc_unshare(CLONE_NEWPID | CLONE_NEWNET) };
        if r < 0 {
            return Err(std::io::Error::last_os_error());
        }
        Ok(())
    }

    /// 移除包名
    pub fn remove_package(&mut self, pkg: &str) -> bool {
        self.packages.remove(pkg)
    }

    /// 移除 UID
    pub fn remove_uid(&mut self, uid: u32) -> bool {
        self.uids.remove(&uid)
    }

    /// 列出所有包名
    pub fn packages(&self) -> &HashSet<String> {
        &self.packages
    }

    /// 列出所有 UID
    pub fn uids(&self) -> &HashSet<u32> {
        &self.uids
    }
}

impl Default for DenyList {
    fn default() -> Self {
        Self::new()
    }
}

/// 简易 glob 匹配（支持 * 通配符）
fn glob_match(pattern: &str, text: &str) -> bool {
    let pat: Vec<char> = pattern.chars().collect();
    let txt: Vec<char> = text.chars().collect();
    glob_match_helper(&pat, 0, &txt, 0)
}

fn glob_match_helper(pat: &[char], pi: usize, txt: &[char], ti: usize) -> bool {
    if pi == pat.len() {
        return ti == txt.len();
    }
    if pat[pi] == '*' {
        // * 匹配 0 个或多个字符
        for k in ti..=txt.len() {
            if glob_match_helper(pat, pi + 1, txt, k) {
                return true;
            }
        }
        return false;
    }
    if ti < txt.len() && pat[pi] == txt[ti] {
        return glob_match_helper(pat, pi + 1, txt, ti + 1);
    }
    false
}

extern "C" {
    fn unshare(flags: i32) -> i32;
}

unsafe fn libc_unshare(flags: i32) -> i32 {
    unsafe { unshare(flags) }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_glob_exact_match() {
        assert!(glob_match("com.example.app", "com.example.app"));
        assert!(!glob_match("com.example.app", "com.example.other"));
    }

    #[test]
    fn test_glob_wildcard_suffix() {
        assert!(glob_match("com.example.*", "com.example.app"));
        assert!(glob_match("com.example.*", "com.example.foo.bar"));
        assert!(!glob_match("com.example.*", "com.other.app"));
    }

    #[test]
    fn test_glob_wildcard_middle() {
        assert!(glob_match("com.*.app", "com.example.app"));
        assert!(glob_match("com.*.app", "com.foo.app"));
        assert!(!glob_match("com.*.app", "com.example.other"));
    }

    #[test]
    fn test_glob_multiple_wildcards() {
        assert!(glob_match("*.*.*", "a.b.c"));
        assert!(glob_match("*.*.*", "com.example.app"));
        assert!(!glob_match("*.*.*", "a.b"));
    }

    #[test]
    fn test_denylist_basic() {
        let mut dl = DenyList::new();
        dl.add_package("com.example.app");
        dl.add_uid(10042);
        assert!(dl.should_isolate("com.example.app", 0));
        assert!(dl.should_isolate("anything", 10042));
        assert!(!dl.should_isolate("com.other", 0));
    }

    #[test]
    fn test_denylist_glob() {
        let mut dl = DenyList::new();
        dl.add_package("com.android.*");
        assert!(dl.should_isolate("com.android.systemui", 0));
        assert!(dl.should_isolate("com.android.settings", 0));
        assert!(!dl.should_isolate("com.example.app", 0));
    }

    #[test]
    fn test_denylist_remove() {
        let mut dl = DenyList::new();
        dl.add_package("foo");
        dl.add_uid(1);
        assert!(dl.remove_package("foo"));
        assert!(dl.remove_uid(1));
        assert!(!dl.should_isolate("foo", 0));
    }
}
