// 简单测试：util 工具

#include "nexus/util.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace nexus;

static void test_string_utils() {
    assert(split("a,b,c", ',').size() == 3);
    assert(split("a,b,c", ',')[0] == "a");
    assert(split("a,b,c", ',')[2] == "c");
    assert(split("", ',').size() == 1);
    assert(split("abc", ',').size() == 1);

    assert(trim("  hello  ") == "hello");
    assert(trim("\t\nhello\r\n") == "hello");
    assert(trim("") == "");
    assert(trim("   ") == "");

    assert(startsWith("hello world", "hello"));
    assert(!startsWith("hello world", "world"));
    assert(endsWith("hello world", "world"));
    assert(!endsWith("hello world", "hello"));

    std::printf("test_string_utils OK\n");
}

static void test_hash_path() {
    auto h1 = hashPath("/system/build.prop");
    auto h2 = hashPath("/system/build.prop");
    auto h3 = hashPath("/system/etc/hosts");
    assert(h1 == h2);
    assert(h1 != h3);
    assert(h1.size() == 16);   // FNV-1a 64bit hex
    std::printf("test_hash_path OK (hash=%s)\n", h1.c_str());
}

static void test_copy_file() {
    std::string tmpDir = "/tmp/nexus_test";
    mkdirRecursive(tmpDir, 0755);
    std::string src = tmpDir + "/src.txt";
    std::string dst = tmpDir + "/dst.txt";
    writeFile(src, "hello world\n", 0644);
    bool ok = copyFile(src, dst);
    assert(ok);
    auto content = readFile(dst);
    assert(content.has_value());
    assert(*content == "hello world\n");
    std::printf("test_copy_file OK\n");
}

int main() {
    test_string_utils();
    test_hash_path();
    test_copy_file();
    std::printf("All util tests passed.\n");
    return 0;
}
