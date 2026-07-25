//! 工具函数

use std::path::{Path, PathBuf};

/// 递归创建目录（等价 mkdir -p）
pub fn mkdir_p(path: &Path) -> std::io::Result<()> {
    std::fs::create_dir_all(path)
}

/// 检查文件存在
pub fn file_exists(path: &Path) -> bool {
    path.is_file()
}

/// 检查目录存在
pub fn dir_exists(path: &Path) -> bool {
    path.is_dir()
}

/// 读取整个文件
pub fn read_file(path: &Path) -> std::io::Result<Vec<u8>> {
    std::fs::read(path)
}

/// 读取整个文件为 UTF-8 string
pub fn read_file_string(path: &Path) -> std::io::Result<String> {
    std::fs::read_to_string(path)
}

/// 写文件（覆盖）
pub fn write_file(path: &Path, content: &[u8]) -> std::io::Result<()> {
    std::fs::write(path, content)
}

/// 简易 FNV-1a 64bit hash，与 daemon 端 hashPath 兼容
pub fn fnv1a_64(data: &[u8]) -> u64 {
    let mut h: u64 = 0xcbf29ce484222325;
    for &b in data {
        h ^= b as u64;
        h = h.wrapping_mul(0x100000001b3);
    }
    h
}

/// hex 编码
pub fn hex_encode(data: &[u8]) -> String {
    let mut s = String::with_capacity(data.len() * 2);
    for b in data {
        s.push_str(&format!("{:02x}", b));
    }
    s
}

/// 哈希路径为 16 字符 hex（与 daemon 端 hashPath 行为一致）
pub fn hash_path(path: &str) -> String {
    hex_encode(&fnv1a_64(path.as_bytes()).to_be_bytes())
}

/// 列出目录下的子目录名
pub fn list_subdirs(path: &Path) -> std::io::Result<Vec<String>> {
    let mut out = Vec::new();
    for entry in std::fs::read_dir(path)? {
        let entry = entry?;
        if entry.file_type()?.is_dir() {
            if let Some(name) = entry.file_name().to_str() {
                out.push(name.to_string());
            }
        }
    }
    Ok(out)
}

/// 拼接路径
pub fn join_path(base: &str, sub: &str) -> PathBuf {
    PathBuf::from(base).join(sub)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_fnv1a_consistency() {
        let h1 = fnv1a_64(b"/system/build.prop");
        let h2 = fnv1a_64(b"/system/build.prop");
        let h3 = fnv1a_64(b"/system/etc/hosts");
        assert_eq!(h1, h2);
        assert_ne!(h1, h3);
    }

    #[test]
    fn test_hash_path_format() {
        let h = hash_path("/system/build.prop");
        assert_eq!(h.len(), 16);
        assert!(h.chars().all(|c| c.is_ascii_hexdigit()));
    }

    #[test]
    fn test_hex_encode() {
        assert_eq!(hex_encode(&[0, 1, 255]), "0001ff");
        assert_eq!(hex_encode(&[]), "");
    }

    #[test]
    fn test_write_read_file() {
        let tmp = std::env::temp_dir().join("nexushook_test_util.txt");
        write_file(&tmp, b"hello world").unwrap();
        let content = read_file(&tmp).unwrap();
        assert_eq!(content, b"hello world");
        std::fs::remove_file(&tmp).ok();
    }

    #[test]
    fn test_file_dir_exists() {
        let tmp_dir = std::env::temp_dir();
        assert!(dir_exists(&tmp_dir));
        let tmp_file = tmp_dir.join("nexushook_nonexistent.txt");
        assert!(!file_exists(&tmp_file));
    }
}
