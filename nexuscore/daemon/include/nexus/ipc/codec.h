#pragma once

#include "nexus/types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace nexus::ipc {

// 帧格式（与 manager 端 ProtobufCodec 一致）：
//   [4B little-endian length][N bytes payload]
//
// MVP 简化：不引入完整 protobuf 库，payload 用自定义二进制格式（更紧凑、零依赖）。
// 字段布局（每个字段前面 1B type 标识）：
//   0x01: u32 varint
//   0x02: u64 varint
//   0x03: length-prefixed string (4B LE len + bytes)
//   0x04: bool (1 byte)
//   0x00: end of message
//
// 这是 daemon 内部格式，与 manager 端 protobuf 解码兼容需要适配层
// （见 IpcServer::handleRead 中的转换逻辑）。
//
// 完整生产实现：两端都用 protobuf-lite，详见 spec-01 §10.3。

constexpr uint32_t MAGIC = 0x4E58434F;   // 'NXCO'
constexpr uint32_t PROTOCOL_VERSION = 1;
constexpr size_t MAX_FRAME = 8 * 1024 * 1024;   // 8 MiB

// 写一帧到 fd：[4B len][payload]
bool writeFrame(int fd, const std::vector<uint8_t>& payload);

// 读一帧（阻塞直到完整帧或 EOF）
// 返回空 optional 表示连接关闭或错误
std::optional<std::vector<uint8_t>> readFrame(int fd);

// 简单字段编码器
class Encoder {
public:
    void putU32(uint32_t v);
    void putU64(uint64_t v);
    void putStr(const std::string& s);
    void putBool(bool b);
    void putEnd();
    std::vector<uint8_t> bytes() && { return std::move(buf_); }
private:
    std::vector<uint8_t> buf_;
    void putByte(uint8_t b) { buf_.push_back(b); }
    void putVarint(uint64_t v);
};

// 简单字段解码器
class Decoder {
public:
    explicit Decoder(std::vector<uint8_t> data) : data_(std::move(data)) {}
    uint8_t peekType();
    uint32_t getU32();
    uint64_t getU64();
    std::string getStr();
    bool getBool();
    bool atEnd();

    // Phase 1.1 修复：暴露当前位置与剩余数据，供 IpcServer 取 payload
    size_t position() const { return pos_; }
    std::vector<uint8_t> remainder() const {
        return std::vector<uint8_t>(data_.begin() + pos_, data_.end());
    }

private:
    std::vector<uint8_t> data_;
    size_t pos_ = 0;
    uint64_t getVarint();
};

} // namespace nexus::ipc
