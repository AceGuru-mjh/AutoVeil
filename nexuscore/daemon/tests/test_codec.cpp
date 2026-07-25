// 完整测试：IPC 帧编解码
// 不依赖 GoogleTest，仅用 assert + main，避免外部依赖

#include "nexus/ipc/codec.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace nexus::ipc;

static void test_encoder_decoder_roundtrip() {
    Encoder enc;
    enc.putU32(42);
    enc.putU64(0x1234567890ABCDEFULL);
    enc.putStr("hello world");
    enc.putBool(true);
    enc.putEnd();
    auto bytes = std::move(enc).bytes();

    Decoder dec(bytes);
    assert(dec.getU32() == 42);
    assert(dec.getU64() == 0x1234567890ABCDEFULL);
    assert(dec.getStr() == "hello world");
    assert(dec.getBool() == true);
    assert(dec.atEnd());
    std::printf("test_encoder_decoder_roundtrip OK\n");
}

static void test_empty_string() {
    Encoder enc;
    enc.putStr("");
    enc.putEnd();
    auto bytes = std::move(enc).bytes();
    Decoder dec(bytes);
    assert(dec.getStr().empty());
    std::printf("test_empty_string OK\n");
}

static void test_multiple_strings() {
    Encoder enc;
    enc.putStr("a");
    enc.putStr("bb");
    enc.putStr("ccc");
    enc.putEnd();
    auto bytes = std::move(enc).bytes();
    Decoder dec(bytes);
    assert(dec.getStr() == "a");
    assert(dec.getStr() == "bb");
    assert(dec.getStr() == "ccc");
    std::printf("test_multiple_strings OK\n");
}

static void test_large_string() {
    // 测试大字符串（跨多字节 varint length）
    std::string big(5000, 'x');
    Encoder enc;
    enc.putStr(big);
    enc.putStr("after");
    enc.putEnd();
    auto bytes = std::move(enc).bytes();
    Decoder dec(bytes);
    assert(dec.getStr() == big);
    assert(dec.getStr() == "after");
    std::printf("test_large_string OK (size=%zu)\n", big.size());
}

static void test_bool_false() {
    Encoder enc;
    enc.putBool(false);
    enc.putBool(true);
    enc.putBool(false);
    enc.putEnd();
    auto bytes = std::move(enc).bytes();
    Decoder dec(bytes);
    assert(dec.getBool() == false);
    assert(dec.getBool() == true);
    assert(dec.getBool() == false);
    std::printf("test_bool_false OK\n");
}

static void test_varint_max() {
    Encoder enc;
    enc.putU64(UINT64_MAX);
    enc.putU32(UINT32_MAX);
    enc.putEnd();
    auto bytes = std::move(enc).bytes();
    Decoder dec(bytes);
    assert(dec.getU64() == UINT64_MAX);
    assert(dec.getU32() == UINT32_MAX);
    std::printf("test_varint_max OK\n");
}

static void test_peek_type() {
    Encoder enc;
    enc.putU32(1);
    enc.putStr("hi");
    enc.putEnd();
    auto bytes = std::move(enc).bytes();
    Decoder dec(bytes);
    assert(dec.peekType() == 0x01);   // u32
    dec.getU32();
    assert(dec.peekType() == 0x03);   // string
    dec.getStr();
    assert(dec.peekType() == 0x00);   // end
    assert(dec.atEnd());
    std::printf("test_peek_type OK\n");
}

int main() {
    test_encoder_decoder_roundtrip();
    test_empty_string();
    test_multiple_strings();
    test_large_string();
    test_bool_false();
    test_varint_max();
    test_peek_type();
    std::printf("All codec tests passed.\n");
    return 0;
}
