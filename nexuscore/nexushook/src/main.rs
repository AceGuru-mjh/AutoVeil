//! NexusHook 主入口
//!
//! 作为 init service 启动，attach 到 zygote，监听 fork 事件，
//! 按需注入 hook 模块到 fork 出的子进程。

use nexushook::{DenyList, ZygoteWatcher};
use std::process;

const HOOK_MODULES_DIR: &str = "/data/adb/nexuscore/hook_modules";
const COMPANION_SOCKET_DIR: &str = "/data/adb/nexuscore/hook_sockets";

fn main() {
    eprintln!("nexushook {} starting", env!("CARGO_PKG_VERSION"));

    // 1. 加载 denylist
    let mut denylist = DenyList::new();
    if let Ok(content) = std::fs::read_to_string("/data/adb/nexuscore/denylist.conf") {
        for line in content.lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            denylist.add_package(line);
        }
        eprintln!("loaded {} denylist entries", denylist.packages().len());
    }

    // 2. 初始化 ZygoteWatcher
    let mut watcher = ZygoteWatcher::new(COMPANION_SOCKET_DIR);

    // 复制 denylist（DenyList 没有实现 Clone，重新构造）
    // 实际实现：让 ZygoteWatcher 持有 denylist 引用或移入
    // 简化：把 denylist 配置传给 watcher
    for pkg in denylist.packages() {
        watcher.denylist.add_package(pkg);
    }
    for &uid in denylist.uids() {
        watcher.denylist.add_uid(uid);
    }

    // 3. 加载所有 hook 模块
    match watcher.load_modules(HOOK_MODULES_DIR) {
        Ok(()) => {
            eprintln!("loaded {} hook module(s) total", watcher.hook_modules.len());
        }
        Err(e) => {
            eprintln!("error loading hook modules: {}", e);
        }
    }

    // 4. 查找 zygote PID
    let zygote_pid = match find_zygote_pid() {
        Some(pid) => {
            eprintln!("found zygote pid={}", pid);
            pid
        }
        None => {
            eprintln!("error: zygote process not found");
            // 不立即退出，进入 sleep 循环让 init 重启
            loop {
                std::thread::sleep(std::time::Duration::from_secs(60));
            }
            #[allow(unreachable_code)]
            process::exit(1);
        }
    };

    // 5. 启动 zygote 监听
    eprintln!("starting zygote watcher");
    if let Err(e) = watcher.start(zygote_pid) {
        eprintln!("error: zygote watcher failed: {}", e);
        process::exit(1);
    }

    // 永远到不了这里
    process::exit(0);
}

/// 查找 zygote 进程 PID
///
/// Android 有两个 zygote 进程：zygote (32-bit) 和 zygote64 (64-bit)
/// NexusHook 只注入 zygote64（arm64-v8a）
fn find_zygote_pid() -> Option<i32> {
    // 方法 1：扫描 /proc/*/comm 找 zygote64
    if let Ok(entries) = std::fs::read_dir("/proc") {
        for entry in entries.flatten() {
            let pid: i32 = entry.file_name().to_string_lossy().parse().ok()?;
            let comm = std::fs::read_to_string(format!("/proc/{}/comm", pid)).ok()?;
            let comm = comm.trim();
            if comm == "zygote64" || comm == "main" {
                // 进一步检查 cmdline
                let cmdline = std::fs::read_to_string(format!("/proc/{}/cmdline", pid)).ok()?;
                let name = cmdline.split('\0').next().unwrap_or("");
                if name.contains("zygote64") || name == "zygote64" {
                    return Some(pid);
                }
            }
        }
    }
    // 方法 2：用 pgrep（如果可用）
    let r = std::process::Command::new("pgrep")
        .args(&["-f", "zygote64"])
        .output();
    if let Ok(out) = r {
        if out.status.success() {
            let s = String::from_utf8_lossy(&out.stdout);
            if let Some(first_line) = s.lines().next() {
                if let Ok(pid) = first_line.parse() {
                    return Some(pid);
                }
            }
        }
    }
    None
}
