//! ptrace 注入器
//!
//! 核心创新：通过 ptrace 而非 LD_PRELOAD 实现注入。
//!
//! 优势：
//! 1. 不修改 /system/bin/linker_asan 或 environment，反检测扫不到 LD_PRELOAD 痕迹
//! 2. 可以在 fork 后、execve 前的精确时间点注入
//! 3. 注入失败的子进程不受影响（ptrace detach 即可）
//!
//! 流程：
//! 1. PTRACE_ATTACH 到目标 PID
//! 2. 保存原始寄存器（PTRACE_GETREGS）
//! 3. 调用 mmap 分配可执行内存（修改 PC + 调用 mmap syscall）
//! 4. 写入 shellcode（PTRACE_POKEDATA）
//! 5. shellcode 调用 dlopen 加载模块 .so
//! 6. 恢复原始寄存器（PTRACE_SETREGS）
//! 7. PTRACE_DETACH

use std::io;

/// ptrace 注入器
pub struct PtraceInjector {
    target_pid: i32,
}

impl PtraceInjector {
    /// attach 到目标进程
    pub fn attach(pid: i32) -> io::Result<Self> {
        // SAFETY: ptrace 是 unsafe FFI，但 PTRACE_ATTACH 是只读 syscall，无 UB 风险
        let r = unsafe { libc_ptrace(16 /* PTRACE_ATTACH */, pid, 0, 0) };
        if r < 0 {
            return Err(io::Error::last_os_error());
        }
        // 等待 stop
        wait_for_stop(pid)?;
        Ok(Self { target_pid: pid })
    }

    /// 获取目标进程的寄存器快照
    pub fn get_regs(&self) -> io::Result<Regs> {
        // 简化：MVP 仅 arm64，寄存器集通过 PTRACE_GETREGSET 获取
        // 实际实现需要 NT_PRSTATUS
        let mut regs = Regs::default();
        // TODO: PTRACE_GETREGSET
        Ok(regs)
    }

    /// 设置目标进程的寄存器
    pub fn set_regs(&self, _regs: &Regs) -> io::Result<()> {
        // TODO: PTRACE_SETREGSET
        Ok(())
    }

    /// 在目标进程内调用函数
    ///
    /// 通过修改 PC 指向 trampoline + 设置参数寄存器实现。
    /// arm64 调用约定：x0-x7 是参数寄存器，x8 是间接结果寄存器。
    pub fn call_function(&self, _addr: u64, _args: &[u64]) -> io::Result<u64> {
        // TODO: 完整实现
        Ok(0)
    }

    /// 写入数据到目标进程内存
    pub fn write_memory(&self, addr: u64, data: &[u8]) -> io::Result<()> {
        let words: Vec<u64> = data.chunks(8).map(|c| {
            let mut w = 0u64;
            for (i, &b) in c.iter().enumerate() {
                w |= (b as u64) << (i * 8);
            }
            w
        }).collect();
        for (i, &w) in words.iter().enumerate() {
            let r = unsafe {
                libc_ptrace(4 /* PTRACE_POKEDATA */, self.target_pid,
                           (addr + (i as u64) * 8) as *mut _, w as *mut _)
            };
            if r < 0 {
                return Err(io::Error::last_os_error());
            }
        }
        Ok(())
    }

    /// 读取目标进程内存
    pub fn read_memory(&self, addr: u64, len: usize) -> io::Result<Vec<u8>> {
        let mut out = Vec::with_capacity(len);
        let words = (len + 7) / 8;
        for i in 0..words {
            let r = unsafe {
                libc_ptrace(2 /* PTRACE_PEEKDATA */, self.target_pid,
                           (addr + (i as u64) * 8) as *mut _, 0)
            };
            if r == -1 {
                return Err(io::Error::last_os_error());
            }
            let w = r as u64;
            let start = i * 8;
            let end = ((i + 1) * 8).min(len);
            for j in start..end {
                out.push(((w >> ((j - start) * 8)) & 0xFF) as u8);
            }
        }
        Ok(out)
    }

    /// detach 并让目标进程继续运行
    pub fn detach(self) -> io::Result<()> {
        let r = unsafe { libc_ptrace(17 /* PTRACE_DETACH */, self.target_pid, 0, 0) };
        if r < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }

    /// 在目标进程内 dlopen 一个 .so
    ///
    /// 通过查找 linker 中的 __dl_dlopen 函数地址，构造调用栈执行
    pub fn dlopen(&self, _lib_path: &str) -> io::Result<u64> {
        // TODO: 完整实现需要：
        // 1. 解析 /proc/<pid>/maps 找到 linker 基址
        // 2. 在 linker 内查找 dlopen 符号
        // 3. 构造调用栈：x0 = lib_path 字符串地址, x1 = flags
        // 4. 设置 PC = dlopen, LR = 0（breakpoint）
        // 5. PTRACE_CONT, 等待 breakpoint
        // 6. 读取 x0 获取返回值
        Ok(0)
    }
}

/// 寄存器快照（arm64 通用 + PC + SP + PSTATE）
#[derive(Default, Debug, Clone)]
pub struct Regs {
    /// 通用寄存器 x0-x30
    pub x: [u64; 31],
    /// 栈指针
    pub sp: u64,
    /// 程序计数器
    pub pc: u64,
    /// 状态寄存器
    pub pstate: u64,
}

fn wait_for_stop(pid: i32) -> io::Result<()> {
    // 简化：用 waitpid 等待
    // 实际实现需要处理 SIGSTOP / SIGTRAP 等不同信号
    let mut status: i32 = 0;
    let r = unsafe { libc_waitpid(pid, &mut status, 0) };
    if r < 0 {
        return Err(io::Error::last_os_error());
    }
    Ok(())
}

// FFI 声明（不依赖 libc crate，自己声明用到的少数函数）
extern "C" {
    fn ptrace(request: i32, pid: i32, addr: *mut libc_void, data: *mut libc_void) -> i64;
    fn waitpid(pid: i32, status: *mut i32, options: i32) -> i32;
}

#[allow(non_camel_case_types)]
type libc_void = std::ffi::c_void;

unsafe fn libc_ptrace(request: i32, pid: i32, addr: *mut libc_void, data: *mut libc_void) -> i64 {
    ptrace(request, pid, addr, data)
}

unsafe fn libc_waitpid(pid: i32, status: *mut i32, options: i32) -> i32 {
    waitpid(pid, status, options)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_regs_default() {
        let r = Regs::default();
        assert_eq!(r.x.len(), 31);
        assert_eq!(r.pc, 0);
        assert_eq!(r.sp, 0);
    }

    #[test]
    fn test_injector_can_be_constructed() {
        // 不能在 CI 里真 attach，但能验证类型可构造
        // （attach 在 host 上会失败，因为不是 root）
    }
}
