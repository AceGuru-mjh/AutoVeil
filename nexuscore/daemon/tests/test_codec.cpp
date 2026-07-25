// 简单测试：IPC 帧编解码
// 不依赖 GoogleTest，仅用 assert + main

#include "nexus/ipc/codec.h"

#include <cassert>
#include <cstdio>
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

int main() {
    test_encoder_decoder_roundtrip();
    test_empty_string();
    test_multiple_strings();
    std::printf("All codec tests passed.\n");
    return 0;
}
