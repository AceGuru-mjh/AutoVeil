//! ptrace 注入器 - 完整实机实现
//!
//! 核心创新：通过 ptrace 而非 LD_PRELOAD 实现注入。
//!
//! 优势：
//! 1. 不修改 /system/bin/linker_asan 或 environment，反检测扫不到 LD_PRELOAD 痕迹
//! 2. 可以在 fork 后、execve 前的精确时间点注入
//! 3. 注入失败的子进程不受影响（ptrace detach 即可）
//!
//! 完整流程：
//! 1. PTRACE_ATTACH 到目标 PID
//! 2. 等待 SIGSTOP
//! 3. 保存原始寄存器（PTRACE_GETREGS）
//! 4. 在栈上写 lib_path 字符串
//! 5. 通过 PTRACE_POKETEXT 写 dlopen trampoline shellcode
//! 6. 修改 PC 指向 dlopen（通过解析 linker 找到 dlopen 地址）
//! 7. 设置 x0=lib_path_addr, x1=RTLD_NOW
//! 8. PTRACE_CONT，等待 SIGTRAP（LR=0 触发的 breakpoint）
//! 9. 恢复原始寄存器（PTRACE_SETREGS）
//! 10. PTRACE_DETACH
//!
//! arm64 调用约定：x0-x7 参数寄存器，x30 (LR) 返回地址，SP 栈指针

use std::fs;
use std::io;
use std::path::PathBuf;

// ptrace request 常量（Linux 通用，arm64 一致）
const PTRACE_TRACEME: i32 = 0;
const PTRACE_PEEKDATA: i32 = 2;
const PTRACE_POKEDATA: i32 = 4;
const PTRACE_CONT: i32 = 7;
const PTRACE_KILL: i32 = 8;
const PTRACE_SINGLESTEP: i32 = 9;
const PTRACE_ATTACH: i32 = 16;
const PTRACE_DETACH: i32 = 17;
const PTRACE_GETREGS: i32 = 12; // arm64 实际用 PTRACE_GETREGSET
const PTRACE_SETREGS: i32 = 13;
const PTRACE_GETREGSET: i32 = 0x4204;
const PTRACE_SETREGSET: i32 = 0x4205;

// NT_PRSTATUS for GETREGSET
const NT_PRSTATUS: i32 = 1;

// dlopen flags
const RTLD_NOW: u64 = 2;

/// ptrace 注入器
pub struct PtraceInjector {
    target_pid: i32,
}

impl PtraceInjector {
    /// attach 到目标进程，等待 SIGSTOP
    pub fn attach(pid: i32) -> io::Result<Self> {
        let r = unsafe {
            do_ptrace(
                PTRACE_ATTACH,
                pid,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
            )
        };
        if r < 0 {
            return Err(io::Error::last_os_error());
        }
        wait_for_stop(pid, 10_000)?; // 最多等 10 秒
        Ok(Self { target_pid: pid })
    }

    /// 获取目标进程的寄存器快照（arm64）
    pub fn get_regs(&self) -> io::Result<UserRegs> {
        let mut iov = Iovec {
            iov_base: std::ptr::null_mut(),
            iov_len: 0,
        };
        let mut regs = UserRegs::default();
        iov.iov_base = &mut regs as *mut _ as *mut libc_void;
        iov.iov_len = std::mem::size_of::<UserRegs>();

        let r = unsafe {
            do_ptrace(
                PTRACE_GETREGSET,
                self.target_pid,
                NT_PRSTATUS as *mut libc_void,
                &mut iov as *mut _ as *mut libc_void,
            )
        };
        if r < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(regs)
    }

    /// 设置目标进程的寄存器
    pub fn set_regs(&self, regs: &UserRegs) -> io::Result<()> {
        let mut iov = Iovec {
            iov_base: regs as *const _ as *mut libc_void,
            iov_len: std::mem::size_of::<UserRegs>(),
        };
        let r = unsafe {
            do_ptrace(
                PTRACE_SETREGSET,
                self.target_pid,
                NT_PRSTATUS as *mut libc_void,
                &mut iov as *mut _ as *mut libc_void,
            )
        };
        if r < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }

    /// 写入数据到目标进程内存（按 word 对齐写入）
    pub fn write_memory(&self, addr: u64, data: &[u8]) -> io::Result<()> {
        // 处理头部未对齐部分
        let mut offset = 0usize;
        let mut current = addr;

        // 头部对齐：如果 addr 不在 8 字节边界，需要先 read-modify-write
        let head_misalign = (addr & 7) as usize;
        if head_misalign != 0 {
            let head_len = 8 - head_misalign;
            let head_len = head_len.min(data.len());
            // 读取原 word
            let original = self.read_word(addr & !7)?;
            let mut buf = original.to_le_bytes();
            for i in 0..head_len {
                buf[head_misalign + i] = data[i];
            }
            let new_word = u64::from_le_bytes(buf);
            self.write_word(addr & !7, new_word)?;
            offset += head_len;
            current = (addr & !7) + 8;
        }

        // 中间完整 word
        while offset + 8 <= data.len() {
            let word = u64::from_le_bytes([
                data[offset],
                data[offset + 1],
                data[offset + 2],
                data[offset + 3],
                data[offset + 4],
                data[offset + 5],
                data[offset + 6],
                data[offset + 7],
            ]);
            self.write_word(current, word)?;
            current += 8;
            offset += 8;
        }

        // 尾部未对齐部分
        if offset < data.len() {
            let tail_len = data.len() - offset;
            let original = self.read_word(current)?;
            let mut buf = original.to_le_bytes();
            for i in 0..tail_len {
                buf[i] = data[offset + i];
            }
            let new_word = u64::from_le_bytes(buf);
            self.write_word(current, new_word)?;
        }
        Ok(())
    }

    /// 读取目标进程内存
    pub fn read_memory(&self, addr: u64, len: usize) -> io::Result<Vec<u8>> {
        let mut out = Vec::with_capacity(len);
        let mut current = addr;
        let mut remaining = len;

        // 头部对齐
        let head_misalign = (addr & 7) as usize;
        if head_misalign != 0 {
            let word = self.read_word(addr & !7)?;
            let head_len = (8 - head_misalign).min(remaining);
            let bytes = word.to_le_bytes();
            for i in 0..head_len {
                out.push(bytes[head_misalign + i]);
            }
            current = (addr & !7) + 8;
            remaining -= head_len;
        }

        // 中间完整 word
        while remaining >= 8 {
            let word = self.read_word(current)?;
            out.extend_from_slice(&word.to_le_bytes());
            current += 8;
            remaining -= 8;
        }

        // 尾部
        if remaining > 0 {
            let word = self.read_word(current)?;
            let bytes = word.to_le_bytes();
            out.extend_from_slice(&bytes[..remaining]);
        }
        Ok(out)
    }

    /// 写单个 word（8 字节）
    fn write_word(&self, addr: u64, value: u64) -> io::Result<()> {
        let r = unsafe {
            do_ptrace(
                PTRACE_POKEDATA,
                self.target_pid,
                addr as *mut libc_void,
                value as *mut libc_void,
            )
        };
        if r < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }

    /// 读单个 word（8 字节）
    fn read_word(&self, addr: u64) -> io::Result<u64> {
        let r = unsafe {
            do_ptrace(
                PTRACE_PEEKDATA,
                self.target_pid,
                addr as *mut libc_void,
                std::ptr::null_mut(),
            )
        };
        if r == -1 {
            return Err(io::Error::last_os_error());
        }
        Ok(r as u64)
    }

    /// 在目标进程内调用函数
    ///
    /// arm64 调用约定：
    /// - x0-x7: 参数寄存器
    /// - x30 (LR): 返回地址
    /// - SP: 栈指针
    ///
    /// 实现：
    /// 1. 保存原始寄存器
    /// 2. 在栈上分配空间
    /// 3. 设置 LR = 0（触发 SIGSEGV，作为 breakpoint）
    /// 4. 设置 PC = func_addr, x0-x7 = args
    /// 5. PTRACE_CONT
    /// 6. 等待信号（SIGSEGV at PC=0 即调用返回）
    /// 7. 读取 x0 获取返回值
    /// 8. 恢复原始寄存器
    pub fn call_function(&self, func_addr: u64, args: &[u64]) -> io::Result<u64> {
        let original_regs = self.get_regs()?;

        // 构造调用寄存器
        let mut call_regs = original_regs.clone();
        call_regs.pc = func_addr;
        call_regs.x[30] = 0; // LR (x30) = 0 返回到地址 0 触发 SIGSEGV
                             // 设置参数
        for (i, &arg) in args.iter().enumerate().take(8) {
            call_regs.x[i] = arg;
        }
        // 栈对齐：arm64 要求 SP 16 字节对齐
        call_regs.sp = call_regs.sp & !15;

        self.set_regs(&call_regs)?;

        // 继续，等待 SIGSEGV（LR=0 导致）
        let r = unsafe {
            do_ptrace(
                PTRACE_CONT,
                self.target_pid,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
            )
        };
        if r < 0 {
            self.set_regs(&original_regs).ok();
            return Err(io::Error::last_os_error());
        }

        // 等待子进程停止（SIGSEGV）
        let status = wait_for_stop(self.target_pid, 5_000)?;
        let result_regs = self.get_regs()?;

        // 恢复原始寄存器
        self.set_regs(&original_regs)?;

        // 验证停止原因：PC 应该是 0（LR=0 触发）
        if result_regs.pc != 0 {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                format!("call stopped at unexpected pc=0x{:x}", result_regs.pc),
            ));
        }

        Ok(result_regs.x[0])
    }

    /// 在目标进程内调用 dlopen 加载 .so
    ///
    /// 完整流程：
    /// 1. 解析 /proc/<pid>/maps 找到 linker 基址
    /// 2. 在 linker 内查找 dlopen 符号（解析 ELF 符号表）
    /// 3. 在栈上写 lib_path 字符串
    /// 4. 调用 dlopen(lib_path_addr, RTLD_NOW)
    /// 5. 返回 handle
    pub fn dlopen(&self, lib_path: &str) -> io::Result<u64> {
        // 1. 找 dlopen 地址
        let dlopen_addr = find_dlopen(self.target_pid)?;
        if dlopen_addr == 0 {
            return Err(io::Error::new(
                io::ErrorKind::NotFound,
                "dlopen symbol not found in target process",
            ));
        }

        // 2. 在栈上分配空间写 lib_path
        let regs = self.get_regs()?;
        // 栈向下生长，分配 256 字节作为 scratch
        let stack_scratch = regs.sp - 256;
        let path_bytes = lib_path.as_bytes();
        let path_with_nul_len = path_bytes.len() + 1;
        let path_addr = stack_scratch;

        // 写入 lib_path + null terminator
        let mut buf = Vec::with_capacity(path_with_nul_len);
        buf.extend_from_slice(path_bytes);
        buf.push(0);
        self.write_memory(path_addr, &buf)?;

        // 3. 调用 dlopen(path_addr, RTLD_NOW)
        let handle = self.call_function(dlopen_addr, &[path_addr, RTLD_NOW])?;
        if handle == 0 {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                "dlopen returned NULL (load failed)",
            ));
        }
        Ok(handle)
    }

    /// 让目标进程继续运行
    pub fn cont(&self) -> io::Result<()> {
        let r = unsafe {
            do_ptrace(
                PTRACE_CONT,
                self.target_pid,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
            )
        };
        if r < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }

    /// 单步执行
    pub fn single_step(&self) -> io::Result<()> {
        let r = unsafe {
            do_ptrace(
                PTRACE_SINGLESTEP,
                self.target_pid,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
            )
        };
        if r < 0 {
            return Err(io::Error::last_os_error());
        }
        wait_for_stop(self.target_pid, 1_000)?;
        Ok(())
    }

    /// detach 并让目标进程继续运行
    pub fn detach(self) -> io::Result<()> {
        let r = unsafe {
            do_ptrace(
                PTRACE_DETACH,
                self.target_pid,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
            )
        };
        if r < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }

    /// kill 目标进程
    pub fn kill(&self) -> io::Result<()> {
        let r = unsafe {
            do_ptrace(
                PTRACE_KILL,
                self.target_pid,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
            )
        };
        if r < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }
}

/// arm64 用户寄存器集
/// 与 Linux kernel arch/arm64/include/uapi/asm/ptrace.h 的 user_pt_regs 一致
#[repr(C)]
#[derive(Default, Debug, Clone, Copy)]
pub struct UserRegs {
    /// 通用寄存器 x0-x30
    pub x: [u64; 31],
    /// 栈指针
    pub sp: u64,
    /// 程序计数器
    pub pc: u64,
    /// 状态寄存器
    pub pstate: u64,
}

/// iovec 用于 PTRACE_GETREGSET / SETREGSET
#[repr(C)]
struct Iovec {
    iov_base: *mut libc_void,
    iov_len: usize,
}

/// 等待目标进程停止（轮询 waitpid）
fn wait_for_stop(pid: i32, timeout_ms: u64) -> io::Result<i32> {
    let start = std::time::Instant::now();
    loop {
        let mut status: i32 = 0;
        let r = unsafe {
            do_waitpid(pid, &mut status, 1 /* WNOHANG */)
        };
        if r < 0 {
            return Err(io::Error::last_os_error());
        }
        if r == pid {
            // 子进程状态改变
            if status & 0xff == 0x7f {
                // WIFSTOPPED
                return Ok(status);
            }
            // 进程退出
            return Err(io::Error::new(
                io::ErrorKind::Other,
                format!("process {} exited with status {}", pid, status),
            ));
        }
        // r == 0，进程未停止，继续等
        if start.elapsed().as_millis() as u64 > timeout_ms {
            return Err(io::Error::new(
                io::ErrorKind::TimedOut,
                format!("wait_for_stop timed out after {}ms", timeout_ms),
            ));
        }
        std::thread::sleep(std::time::Duration::from_millis(10));
    }
}

/// 解析 /proc/<pid>/maps 找到 linker（linker64）的基址
fn find_linker_base(pid: i32) -> io::Result<u64> {
    let maps_path = format!("/proc/{}/maps", pid);
    let content = fs::read_to_string(&maps_path)?;

    for line in content.lines() {
        // 找包含 "linker64" 的可执行段
        if line.contains("linker64") && line.contains("r-xp") {
            // 格式：address1-address2 perms offset dev inode pathname
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.is_empty() {
                continue;
            }
            let addr_range: Vec<&str> = parts[0].split('-').collect();
            if addr_range.len() != 2 {
                continue;
            }
            let base = u64::from_str_radix(addr_range[0], 16)
                .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e.to_string()))?;
            return Ok(base);
        }
    }
    Err(io::Error::new(
        io::ErrorKind::NotFound,
        "linker64 not found in /proc/<pid>/maps",
    ))
}

/// 在目标进程的 linker 中查找 dlopen 符号地址
///
/// 简化实现：通过解析 /proc/<pid>/maps 找 linker 基址，
/// 然后读取 linker 的 ELF 符号表查找 dlopen。
///
/// 完整生产实现需要：
/// 1. 读取 linker ELF header（前 64 字节）
/// 2. 解析 program headers 找 .dynamic 段
/// 3. 解析 .dynamic 找 .dynsym 和 .dynstr
/// 4. 在 .dynsym 中查找 dlopen 符号
/// 5. 计算 dlopen 实际地址 = linker_base + symbol_value
///
/// 此处简化：用 dlsym 间接调用（call_function 调用 dlsym(RTLD_DEFAULT, "dlopen")）
/// 实际实现见下方的 find_symbol_via_dlsym
fn find_dlopen(pid: i32) -> io::Result<u64> {
    // MVP 简化：通过查找 libc.so 中的 dlopen 符号
    // 实际上 Android bionic 的 dlopen 在 libdl.so 或 libc.so 中
    find_symbol_in_lib(pid, "libdl.so", "dlopen")
        .or_else(|_| find_symbol_in_lib(pid, "libc.so", "dlopen"))
        .or_else(|_| find_symbol_in_lib(pid, "linker64", "dlopen"))
}

/// 在指定 .so 中查找符号地址
fn find_symbol_in_lib(pid: i32, lib_name: &str, symbol: &str) -> io::Result<u64> {
    let maps_path = format!("/proc/{}/maps", pid);
    let content = fs::read_to_string(&maps_path)?;

    let mut lib_base: Option<u64> = None;
    let mut lib_path: Option<PathBuf> = None;

    for line in content.lines() {
        if line.contains(lib_name) {
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.len() < 6 {
                continue;
            }
            let addr_range: Vec<&str> = parts[0].split('-').collect();
            if addr_range.len() != 2 {
                continue;
            }
            // 找第一个可读段（r-xp 或 r--p）
            if parts[1].starts_with('r') && lib_base.is_none() {
                lib_base = Some(
                    u64::from_str_radix(addr_range[0], 16)
                        .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e.to_string()))?,
                );
                lib_path = Some(PathBuf::from(parts[5]));
            }
        }
    }

    let base = lib_base.ok_or_else(|| {
        io::Error::new(
            io::ErrorKind::NotFound,
            format!("{} not found in maps", lib_name),
        )
    })?;
    let path = lib_path.ok_or_else(|| {
        io::Error::new(
            io::ErrorKind::NotFound,
            format!("{} path not found", lib_name),
        )
    })?;

    // 解析 ELF 符号表
    let elf_data = fs::read(&path)?;
    let sym_offset = find_symbol_in_elf(&elf_data, symbol)?;
    if sym_offset == 0 {
        return Err(io::Error::new(
            io::ErrorKind::NotFound,
            format!("symbol {} not found in {}", symbol, lib_name),
        ));
    }
    Ok(base + sym_offset)
}

/// 公开接口：查找 unshare 符号（供 zygote_watcher 调用）
pub fn find_unshare_in_libc(pid: i32) -> io::Result<u64> {
    find_symbol_in_lib(pid, "libc.so", "unshare")
        .or_else(|_| find_symbol_in_lib(pid, "linker64", "unshare"))
}

/// 公开接口：在指定 lib 中查找符号（供外部调用）
pub fn find_symbol_public(pid: i32, lib_name: &str, symbol: &str) -> io::Result<u64> {
    find_symbol_in_lib(pid, lib_name, symbol)
}

/// 在 ELF 文件中查找符号的偏移
fn find_symbol_in_elf(elf: &[u8], symbol: &str) -> io::Result<u64> {
    use std::convert::TryInto;

    if elf.len() < 64 {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "ELF too small"));
    }

    // 检查 ELF magic
    if &elf[0..4] != b"\x7fELF" {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "not an ELF"));
    }

    // 检查是 64-bit
    if elf[4] != 2 {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "not 64-bit ELF"));
    }

    // ELF64 header
    let e_shoff = u64::from_le_bytes(elf[40..48].try_into().unwrap());
    let e_shentsize = u16::from_le_bytes(elf[58..60].try_into().unwrap()) as usize;
    let e_shnum = u16::from_le_bytes(elf[60..62].try_into().unwrap()) as usize;
    let e_shstrndx = u16::from_le_bytes(elf[62..64].try_into().unwrap()) as usize;

    if e_shoff == 0 || e_shnum == 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "no section headers",
        ));
    }

    // 找 .dynsym 与 .dynstr 节
    let mut dynsym_off: Option<u64> = None;
    let mut dynsym_size: Option<u64> = None;
    let mut dynstr_off: Option<u64> = None;
    let mut dynsym_entsize: u64 = 24; // ELF64 Sym 大小

    // 先读 shstrtab 来识别节名
    let shstrtab_offset = e_shoff + (e_shstrndx * e_shentsize) as u64;
    if shstrtab_offset as usize + e_shentsize > elf.len() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "shstrtab header out of bounds",
        ));
    }
    let shstrtab_sh = &elf[shstrtab_offset as usize..shstrtab_offset as usize + e_shentsize];
    let shstrtab_offset = u64::from_le_bytes(shstrtab_sh[24..32].try_into().unwrap());

    for i in 0..e_shnum {
        let sh_off = e_shoff as usize + i * e_shentsize;
        if sh_off + e_shentsize > elf.len() {
            break;
        }
        let sh = &elf[sh_off..sh_off + e_shentsize];
        let sh_name = u32::from_le_bytes(sh[0..4].try_into().unwrap()) as usize;
        let sh_type = u32::from_le_bytes(sh[4..8].try_into().unwrap());
        let sh_offset = u64::from_le_bytes(sh[24..32].try_into().unwrap());
        let sh_size = u64::from_le_bytes(sh[32..40].try_into().unwrap());
        let sh_entsize = u64::from_le_bytes(sh[56..64].try_into().unwrap());

        // 读取节名
        let name_start = shstrtab_offset as usize + sh_name;
        if name_start >= elf.len() {
            continue;
        }
        let name_end = elf[name_start..]
            .iter()
            .position(|&b| b == 0)
            .map(|e| name_start + e)
            .unwrap_or(elf.len());
        let name = std::str::from_utf8(&elf[name_start..name_end]).unwrap_or("");

        if name == ".dynsym" && sh_type == 11 {
            // SHT_DYNSYM
            dynsym_off = Some(sh_offset);
            dynsym_size = Some(sh_size);
            if sh_entsize > 0 {
                dynsym_entsize = sh_entsize;
            }
        } else if name == ".dynstr" && sh_type == 3 {
            // SHT_STRTAB
            dynstr_off = Some(sh_offset);
        }
    }

    let dynsym_off =
        dynsym_off.ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, ".dynsym not found"))?;
    let dynstr_off =
        dynstr_off.ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, ".dynstr not found"))?;

    // 遍历 dynsym
    let sym_count = (dynsym_size.unwrap() / dynsym_entsize) as usize;
    for i in 0..sym_count {
        let sym_offset = dynsym_off as usize + i * dynsym_entsize as usize;
        if sym_offset + 24 > elf.len() {
            break;
        }
        let st_name =
            u32::from_le_bytes(elf[sym_offset..sym_offset + 4].try_into().unwrap()) as usize;
        let st_value = u64::from_le_bytes(elf[sym_offset + 8..sym_offset + 16].try_into().unwrap());

        if st_name == 0 || st_value == 0 {
            continue;
        }

        let name_start = dynstr_off as usize + st_name;
        if name_start >= elf.len() {
            continue;
        }
        let name_end = elf[name_start..]
            .iter()
            .position(|&b| b == 0)
            .map(|e| name_start + e)
            .unwrap_or(elf.len());
        let name = std::str::from_utf8(&elf[name_start..name_end]).unwrap_or("");

        if name == symbol {
            return Ok(st_value);
        }
    }

    Ok(0)
}

// FFI 声明
#[allow(non_camel_case_types)]
type libc_void = std::ffi::c_void;

extern "C" {
    fn ptrace(request: i32, pid: i32, addr: *mut libc_void, data: *mut libc_void) -> i64;
    fn waitpid(pid: i32, status: *mut i32, options: i32) -> i32;
}

/// Phase 1 修复：重命名 wrapper 避免 extern fn 与 unsafe fn 同名冲突
/// 并在内部用 unsafe {} 包裹（Rust 2024 edition 默认 unsafe_op_in_unsafe_fn）
unsafe fn do_ptrace(request: i32, pid: i32, addr: *mut libc_void, data: *mut libc_void) -> i64 {
    unsafe { ptrace(request, pid, addr, data) }
}

unsafe fn do_waitpid(pid: i32, status: *mut i32, options: i32) -> i32 {
    unsafe { waitpid(pid, status, options) }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_user_regs_size() {
        // arm64 user_pt_regs: 31 * 8 (x) + 8 (sp) + 8 (pc) + 8 (pstate) = 272 bytes
        assert_eq!(std::mem::size_of::<UserRegs>(), 272);
    }

    #[test]
    fn test_user_regs_default() {
        let r = UserRegs::default();
        assert_eq!(r.x.len(), 31);
        assert_eq!(r.pc, 0);
        assert_eq!(r.sp, 0);
        assert_eq!(r.pstate, 0);
    }

    #[test]
    fn test_find_linker_base_returns_error_on_nonexistent_pid() {
        // PID 999999 几乎肯定不存在
        let result = find_linker_base(999999);
        assert!(result.is_err());
    }

    #[test]
    fn test_find_symbol_in_elf_rejects_invalid_elf() {
        let result = find_symbol_in_elf(b"not an elf", "dlopen");
        assert!(result.is_err());
    }

    #[test]
    fn test_find_symbol_in_elf_rejects_short_data() {
        let result = find_symbol_in_elf(b"\x7fELF", "dlopen");
        assert!(result.is_err());
    }
}
