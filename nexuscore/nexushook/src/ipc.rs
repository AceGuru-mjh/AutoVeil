//! IPC - zygote 与 companion 之间的 UDS 通信
//!
//! 协议格式（与 daemon IPC codec 一致）：
//!   [4B LE length][payload bytes]
//!
//! Payload 是自定义二进制格式（varint + string + bool）：
//!   0x01: u32 varint
//!   0x02: u64 varint
//!   0x03: length-prefixed string
//!   0x04: bool
//!   0x00: end of message

use std::io::{self, Read, Write};
use std::os::unix::net::UnixStream;
use std::path::Path;

/// Hook 调用请求（zygote → companion）
#[derive(Debug, Clone)]
pub struct HookCall {
    pub module_id: String,
    pub hook_id: u32,
    pub handler: String,
    pub args: Vec<HookValue>,
}

/// Hook 返回值（companion → zygote）
#[derive(Debug, Clone)]
pub struct HookReturn {
    pub ok: bool,
    pub value: HookValue,
    pub error: String,
}

/// hook 参数 / 返回值（基本类型 + 字符串 + 字节数组）
#[derive(Debug, Clone)]
pub enum HookValue {
    Void,
    Bool(bool),
    Int(i64),
    Long(i64),
    String(String),
    Bytes(Vec<u8>),
}

impl HookValue {
    pub fn encode(&self) -> Vec<u8> {
        let mut buf = Vec::new();
        match self {
            HookValue::Void => {
                buf.push(0x00);
            }
            HookValue::Bool(b) => {
                buf.push(0x04);
                buf.push(if *b { 1 } else { 0 });
            }
            HookValue::Int(i) | HookValue::Long(i) => {
                buf.push(0x02);
                buf.extend_from_slice(&encode_varint(*i as u64));
            }
            HookValue::String(s) => {
                buf.push(0x03);
                let bytes = s.as_bytes();
                buf.extend_from_slice(&encode_varint(bytes.len() as u64));
                buf.extend_from_slice(bytes);
            }
            HookValue::Bytes(b) => {
                buf.push(0x05);
                buf.extend_from_slice(&encode_varint(b.len() as u64));
                buf.extend_from_slice(b);
            }
        }
        buf
    }

    pub fn decode(data: &[u8], pos: &mut usize) -> Option<Self> {
        if *pos >= data.len() {
            return None;
        }
        let tag = data[*pos];
        *pos += 1;
        match tag {
            0x00 => Some(HookValue::Void),
            0x04 => {
                if *pos >= data.len() {
                    return None;
                }
                let b = data[*pos] != 0;
                *pos += 1;
                Some(HookValue::Bool(b))
            }
            0x02 => {
                let (v, np) = decode_varint(data, *pos)?;
                *pos = np;
                Some(HookValue::Long(v as i64))
            }
            0x03 => {
                let (len, np) = decode_varint(data, *pos)?;
                *pos = np;
                if *pos + len as usize > data.len() {
                    return None;
                }
                let s = String::from_utf8_lossy(&data[*pos..*pos + len as usize]).to_string();
                *pos += len as usize;
                Some(HookValue::String(s))
            }
            0x05 => {
                let (len, np) = decode_varint(data, *pos)?;
                *pos = np;
                if *pos + len as usize > data.len() {
                    return None;
                }
                let b = data[*pos..*pos + len as usize].to_vec();
                *pos += len as usize;
                Some(HookValue::Bytes(b))
            }
            _ => None,
        }
    }
}

fn encode_varint(mut v: u64) -> Vec<u8> {
    let mut buf = Vec::new();
    while v >= 0x80 {
        buf.push((v as u8) | 0x80);
        v >>= 7;
    }
    buf.push(v as u8);
    buf
}

fn decode_varint(data: &[u8], mut pos: usize) -> Option<(u64, usize)> {
    let mut result: u64 = 0;
    let mut shift = 0;
    while pos < data.len() {
        let b = data[pos];
        pos += 1;
        result |= ((b & 0x7F) as u64) << shift;
        if (b & 0x80) == 0 {
            return Some((result, pos));
        }
        shift += 7;
        if shift >= 64 {
            return None;
        }
    }
    None
}

/// 写一帧
pub fn write_frame(stream: &mut UnixStream, payload: &[u8]) -> io::Result<()> {
    let len = (payload.len() as u32).to_le_bytes();
    stream.write_all(&len)?;
    stream.write_all(payload)?;
    Ok(())
}

/// 读一帧
pub fn read_frame(stream: &mut UnixStream) -> io::Result<Vec<u8>> {
    let mut len_buf = [0u8; 4];
    stream.read_exact(&mut len_buf)?;
    let len = u32::from_le_bytes(len_buf) as usize;
    if len > 8 * 1024 * 1024 {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "frame too large"));
    }
    let mut buf = vec![0u8; len];
    stream.read_exact(&mut buf)?;
    Ok(buf)
}

/// 连接到 companion socket
pub fn connect(socket_path: &Path) -> io::Result<UnixStream> {
    UnixStream::connect(socket_path)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_varint_roundtrip() {
        for v in [0u64, 1, 127, 128, 65535, 65536, u32::MAX as u64, u64::MAX] {
            let encoded = encode_varint(v);
            let (decoded, _) = decode_varint(&encoded, 0).unwrap();
            assert_eq!(v, decoded, "failed for v={}", v);
        }
    }

    #[test]
    fn test_hook_value_encode_decode_void() {
        let v = HookValue::Void;
        let encoded = v.encode();
        let mut pos = 0;
        let decoded = HookValue::decode(&encoded, &mut pos).unwrap();
        assert!(matches!(decoded, HookValue::Void));
    }

    #[test]
    fn test_hook_value_encode_decode_bool() {
        for b in [true, false] {
            let v = HookValue::Bool(b);
            let encoded = v.encode();
            let mut pos = 0;
            let decoded = HookValue::decode(&encoded, &mut pos).unwrap();
            assert!(matches!(decoded, HookValue::Bool(x) if x == b));
        }
    }

    #[test]
    fn test_hook_value_encode_decode_long() {
        let v = HookValue::Long(0x1234567890ABCDEF);
        let encoded = v.encode();
        let mut pos = 0;
        let decoded = HookValue::decode(&encoded, &mut pos).unwrap();
        assert!(matches!(decoded, HookValue::Long(x) if x == 0x1234567890ABCDEF));
    }

    #[test]
    fn test_hook_value_encode_decode_string() {
        let v = HookValue::String("hello world".to_string());
        let encoded = v.encode();
        let mut pos = 0;
        let decoded = HookValue::decode(&encoded, &mut pos).unwrap();
        assert!(matches!(decoded, HookValue::String(s) if s == "hello world"));
    }

    #[test]
    fn test_hook_value_encode_decode_unicode_string() {
        let v = HookValue::String("你好世界".to_string());
        let encoded = v.encode();
        let mut pos = 0;
        let decoded = HookValue::decode(&encoded, &mut pos).unwrap();
        assert!(matches!(decoded, HookValue::String(s) if s == "你好世界"));
    }

    #[test]
    fn test_hook_value_encode_decode_bytes() {
        let v = HookValue::Bytes(vec![0, 1, 2, 3, 255, 254, 253]);
        let encoded = v.encode();
        let mut pos = 0;
        let decoded = HookValue::decode(&encoded, &mut pos).unwrap();
        assert!(matches!(decoded, HookValue::Bytes(b) if b == vec![0, 1, 2, 3, 255, 254, 253]));
    }
}
