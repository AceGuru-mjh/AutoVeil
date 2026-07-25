#include "nexus/ipc/codec.h"
#include "nexus/log.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace nexus::ipc {

namespace {

ssize_t readFull(int fd, void* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = ::read(fd, (char*)buf + total, len - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return total;   // EOF
        total += n;
    }
    return total;
}

ssize_t writeFull(int fd, const void* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = ::write(fd, (const char*)buf + total, len - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += n;
    }
    return total;
}

} // namespace

bool writeFrame(int fd, const std::vector<uint8_t>& payload) {
    if (payload.size() > MAX_FRAME) return false;
    uint32_t len = htonl((uint32_t)payload.size());
    if (writeFull(fd, &len, 4) != 4) return false;
    if (writeFull(fd, payload.data(), payload.size()) != (ssize_t)payload.size()) return false;
    return true;
}

std::optional<std::vector<uint8_t>> readFrame(int fd) {
    uint32_t lenNet = 0;
    if (readFull(fd, &lenNet, 4) != 4) return std::nullopt;
    uint32_t len = ntohl(lenNet);
    if (len == 0 || len > MAX_FRAME) {
        NX_LOG_W("Codec", "bad frame len: %u", len);
        return std::nullopt;
    }
    std::vector<uint8_t> buf(len);
    if (readFull(fd, buf.data(), len) != (ssize_t)len) return std::nullopt;
    return buf;
}

// ============ Encoder ============

void Encoder::putVarint(uint64_t v) {
    while (v >= 0x80) {
        putByte((uint8_t)(v | 0x80));
        v >>= 7;
    }
    putByte((uint8_t)v);
}

void Encoder::putU32(uint32_t v) {
    putByte(0x01);
    putVarint(v);
}

void Encoder::putU64(uint64_t v) {
    putByte(0x02);
    putVarint(v);
}

void Encoder::putStr(const std::string& s) {
    putByte(0x03);
    putVarint(s.size());
    buf_.insert(buf_.end(), s.begin(), s.end());
}

void Encoder::putBool(bool b) {
    putByte(0x04);
    putByte(b ? 1 : 0);
}

void Encoder::putEnd() {
    putByte(0x00);
}

// ============ Decoder ============

uint64_t Decoder::getVarint() {
    uint64_t result = 0;
    int shift = 0;
    while (pos_ < data_.size()) {
        uint8_t b = data_[pos_++];
        result |= (uint64_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
    }
    return result;
}

uint8_t Decoder::peekType() {
    if (pos_ >= data_.size()) return 0x00;
    return data_[pos_];
}

uint32_t Decoder::getU32() {
    if (pos_ >= data_.size() || data_[pos_] != 0x01) return 0;
    ++pos_;
    return (uint32_t)getVarint();
}

uint64_t Decoder::getU64() {
    if (pos_ >= data_.size() || data_[pos_] != 0x02) return 0;
    ++pos_;
    return getVarint();
}

std::string Decoder::getStr() {
    if (pos_ >= data_.size() || data_[pos_] != 0x03) return "";
    ++pos_;
    uint64_t len = getVarint();
    if (pos_ + len > data_.size()) return "";
    std::string s((const char*)&data_[pos_], len);
    pos_ += len;
    return s;
}

bool Decoder::getBool() {
    if (pos_ >= data_.size() || data_[pos_] != 0x04) return false;
    ++pos_;
    if (pos_ >= data_.size()) return false;
    return data_[pos_++] != 0;
}

bool Decoder::atEnd() {
    return pos_ >= data_.size() || data_[pos_] == 0x00;
}

} // namespace nexus::ipc
