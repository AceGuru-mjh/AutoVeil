// 完整测试：util 工具函数

#include "nexus/util.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace nexus;

static void test_string_utils() {
    assert(split("a,b,c", ',').size() == 3);
    assert(split("a,b,c", ',')[0] == "a");
    assert(split("a,b,c", ',')[2] == "c");
    assert(split("", ',').size() == 1);
    assert(split("abc", ',').size() == 1);
    assert(split("a,b,c", ',')[1] == "b");

    assert(trim("  hello  ") == "hello");
    assert(trim("\t\nhello\r\n") == "hello");
    assert(trim("") == "");
    assert(trim("   ") == "");
    assert(trim("hello") == "hello");

    assert(startsWith("hello world", "hello"));
    assert(!startsWith("hello world", "world"));
    assert(startsWith("hello", ""));        // 空前缀永远 true
    assert(endsWith("hello world", "world"));
    assert(!endsWith("hello world", "hello"));
    assert(endsWith("hello", ""));

    std::printf("test_string_utils OK\n");
}

static void test_hash_path_consistency() {
    auto h1 = hashPath("/system/build.prop");
    auto h2 = hashPath("/system/build.prop");
    auto h3 = hashPath("/system/etc/hosts");
    auto h4 = hashPath("/system/etc/hosts");
    assert(h1 == h2);
    assert(h1 != h3);
    assert(h3 == h4);
    assert(h1.size() == 16);   // FNV-1a 64bit hex
    std::printf("test_hash_path_consistency OK (hash(/system/build.prop)=%s)\n", h1.c_str());
}

static void test_copy_file_basic() {
    std::string tmpDir = "/tmp/nexus_test_util";
    mkdirRecursive(tmpDir, 0755);
    std::string src = tmpDir + "/src.txt";
    std::string dst = tmpDir + "/dst.txt";
    writeFile(src, "hello world\n", 0644);
    bool ok = copyFile(src, dst);
    assert(ok);
    auto content = readFile(dst);
    assert(content.has_value());
    assert(*content == "hello world\n");
    std::printf("test_copy_file_basic OK\n");
}

static void test_copy_file_large() {
    std::string tmpDir = "/tmp/nexus_test_util";
    mkdirRecursive(tmpDir, 0755);
    std::string src = tmpDir + "/large.bin";
    std::string dst = tmpDir + "/large_copy.bin";
    // 256 KiB 二进制数据
    std::string data(256 * 1024, '\0');
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = (char)(i & 0xFF);
    }
    writeFile(src, data, 0644);
    bool ok = copyFile(src, dst);
    assert(ok);
    auto content = readFile(dst);
    assert(content.has_value());
    assert(content->size() == data.size());
    assert(*content == data);
    std::printf("test_copy_file_large OK (256 KiB verified)\n");
}

static void test_copy_file_overwrite() {
    std::string tmpDir = "/tmp/nexus_test_util";
    mkdirRecursive(tmpDir, 0755);
    std::string src = tmpDir + "/src2.txt";
    std::string dst = tmpDir + "/dst2.txt";
    writeFile(src, "new content\n", 0644);
    writeFile(dst, "old content that is longer than new\n", 0644);
    bool ok = copyFile(src, dst);
    assert(ok);
    auto content = readFile(dst);
    assert(content.has_value());
    assert(*content == "new content\n");
    std::printf("test_copy_file_overwrite OK\n");
}

static void test_mkdir_recursive() {
    std::string deep = "/tmp/nexus_test_util/a/b/c/d/e";
    bool ok = mkdirRecursive(deep, 0755);
    assert(ok);
    assert(probeDir(deep));
    std::printf("test_mkdir_recursive OK\n");
}

static void test_read_write_file() {
    std::string tmpDir = "/tmp/nexus_test_util";
    mkdirRecursive(tmpDir, 0755);
    std::string path = tmpDir + "/rw_test.txt";
    std::string content = "line 1\nline 2\nline 3\n";
    bool ok = writeFile(path, content, 0644);
    assert(ok);
    auto read = readFile(path);
    assert(read.has_value());
    assert(*read == content);
    std::printf("test_read_write_file OK\n");
}

static void test_read_nonexistent_file() {
    auto content = readFile("/tmp/nexus_test_util/nonexistent_xyz_12345.txt");
    assert(!content.has_value());
    std::printf("test_read_nonexistent_file OK\n");
}

static void test_probe_file_dir() {
    std::string tmpDir = "/tmp/nexus_test_util";
    mkdirRecursive(tmpDir, 0755);
    std::string filePath = tmpDir + "/probe.txt";
    std::string dirPath = tmpDir + "/subdir";
    mkdirRecursive(dirPath, 0755);
    writeFile(filePath, "x", 0644);

    assert(probeFile(filePath));
    assert(!probeFile(dirPath));        // dir is not file
    assert(probeDir(dirPath));
    assert(!probeDir(filePath));        // file is not dir
    assert(!probeFile("/tmp/nexus_test_util/no_such_file"));
    std::printf("test_probe_file_dir OK\n");
}

int main() {
    test_string_utils();
    test_hash_path_consistency();
    test_copy_file_basic();
    test_copy_file_large();
    test_copy_file_overwrite();
    test_mkdir_recursive();
    test_read_write_file();
    test_read_nonexistent_file();
    test_probe_file_dir();
    std::printf("All util tests passed.\n");
    return 0;
}
