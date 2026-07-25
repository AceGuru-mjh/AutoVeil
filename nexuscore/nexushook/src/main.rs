//! NexusHook 主入口
//!
//! 作为 init service 启动，attach 到 zygote，监听 fork 事件，
//! 按需注入 hook 模块到 fork 出的子进程。

use nexushook::{PtraceInjector, HookTable, CompanionManager, DenyList};
use std::path::PathBuf;
use std::process;

const ZYGOTE_PID: i32 = 1;   // MVP 占位，实际应从 /proc 解析
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

    // 2. 加载所有 hook 模块
    let mut hook_tables: Vec<(String, HookTable, PathBuf)> = Vec::new();
    if let Ok(mods) = std::fs::read_dir(HOOK_MODULES_DIR) {
        for entry in mods.flatten() {
            let path = entry.path();
            if !path.is_dir() {
                continue;
            }
            let module_id = entry.file_name().to_string_lossy().to_string();
            let hook_toml = path.join("hook.toml");
            let so_path = path.join("module.so");
            if !hook_toml.exists() || !so_path.exists() {
                eprintln!("skip {}: missing hook.toml or module.so", module_id);
                continue;
            }
            match std::fs::read_to_string(&hook_toml) {
                Ok(content) => {
                    match HookTable::parse_toml(&content) {
                        Ok(table) => {
                            let warnings = table.validate();
                            for w in &warnings {
                                eprintln!("warning: [{}] {}", module_id, w);
                            }
                            if warnings.is_empty() {
                                hook_tables.push((module_id.clone(), table, so_path));
                                eprintln!("loaded hook module: {} ({} entries)",
                                          module_id, hook_tables.last().unwrap().1.entries.len());
                            }
                        }
                        Err(e) => eprintln!("error: parse hook.toml for {}: {}", module_id, e),
                    }
                }
                Err(e) => eprintln!("error: read hook.toml for {}: {}", module_id, e),
            }
        }
    }
    eprintln!("loaded {} hook module(s) total", hook_tables.len());

    // 3. 启动 companion 管理器
    let companions = CompanionManager::new(COMPANION_SOCKET_DIR);
    for (id, _, so_path) in &hook_tables {
        if let Err(e) = companions.start(id, so_path.clone()) {
            eprintln!("error: start companion for {}: {}", id, e);
        }
    }

    // 4. attach 到 zygote
    // MVP：暂不实际 attach（需要 root + 特定 SELinux 域）
    // 实际生产代码会调用 PtraceInjector::attach(ZYGOTE_PID)
    eprintln!("would attach to zygote pid={}", ZYGOTE_PID);
    let _injector: Option<PtraceInjector> = None;

    // 5. 主循环：监听 zygote fork
    // TODO: PTRACE_LISTEN + fork event 监听
    // 每当 zygote fork 出新进程：
    //   a. 读取子进程的 cmdline 拿包名
    //   b. 检查 denylist，命中则 unshare(NEWPID|NEWNET) 隔离
    //   c. 未命中 denylist 且有匹配的 hook，则通过 ptrace + dlopen 注入
    eprintln!("entering main loop");
    loop {
        std::thread::sleep(std::time::Duration::from_secs(60));
        // MVP 占位
    }

    // 永远到不了这里
    #[allow(unreachable_code)]
    {
        drop(companions);
        process::exit(0);
    }
}
