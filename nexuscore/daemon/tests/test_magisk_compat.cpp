// MagiskCompat 单元测试
//
// Phase 7：覆盖 magisk_compat.h 的核心功能
// - parseModuleProp KV 解析
// - convertToManifest JSON 生成
// - isMagiskModule 检测
// - getShimScript 内容验证

#include "nexus/magisk_compat.h"
#include "nexus/util.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

using namespace nexus;

static void test_parse_module_prop_valid() {
    // 创建临时 module.prop 文件
    std::string tmpDir = "/tmp/nexus_test_magisk_mod";
    mkdirRecursive(tmpDir, 0755);
    std::string propPath = tmpDir + "/module.prop";
    std::string content =
        "id=test_module\n"
        "name=Test Module\n"
        "version=v1.0.0\n"
        "versionCode=1\n"
        "author=tester\n"
        "description=A test module\n";
    writeFile(propPath, content, 0644);

    auto r = MagiskCompat::parseModuleProp(propPath);
    assert(r);
    assert(r->id == "test_module");
    assert(r->name == "Test Module");
    assert(r->version == "v1.0.0");
    assert(r->versionCode == 1);
    assert(r->author == "tester");
    assert(r->description == "A test module");
    assert(r->minNexusVersion == "1.0");   // 自动填充

    // 清理
    ::unlink(propPath.c_str());
    ::rmdir(tmpDir.c_str());
    std::printf("test_parse_module_prop_valid OK\n");
}

static void test_parse_module_prop_missing_fields() {
    std::string tmpDir = "/tmp/nexus_test_magisk_mod2";
    mkdirRecursive(tmpDir, 0755);
    std::string propPath = tmpDir + "/module.prop";
    // 缺 author
    std::string content =
        "id=test_mod\n"
        "name=Test\n"
        "version=v1.0\n";
    writeFile(propPath, content, 0644);

    auto r = MagiskCompat::parseModuleProp(propPath);
    assert(!r);   // 必填字段缺失应失败

    ::unlink(propPath.c_str());
    ::rmdir(tmpDir.c_str());
    std::printf("test_parse_module_prop_missing_fields OK\n");
}

static void test_parse_module_prop_nonexistent() {
    auto r = MagiskCompat::parseModuleProp("/tmp/nonexistent_module.prop");
    assert(!r);
    std::printf("test_parse_module_prop_nonexistent OK\n");
}

static void test_parse_module_prop_with_comments() {
    std::string tmpDir = "/tmp/nexus_test_magisk_mod3";
    mkdirRecursive(tmpDir, 0755);
    std::string propPath = tmpDir + "/module.prop";
    std::string content =
        "# This is a comment\n"
        "id=commented_mod\n"
        "\n"
        "# Another comment\n"
        "name=Commented Module\n"
        "version=v2.0\n"
        "author=tester\n";
    writeFile(propPath, content, 0644);

    auto r = MagiskCompat::parseModuleProp(propPath);
    assert(r);
    assert(r->id == "commented_mod");
    assert(r->name == "Commented Module");
    assert(r->version == "v2.0");

    ::unlink(propPath.c_str());
    ::rmdir(tmpDir.c_str());
    std::printf("test_parse_module_prop_with_comments OK\n");
}

static void test_is_magisk_module_true() {
    std::string tmpDir = "/tmp/nexus_test_is_magisk";
    mkdirRecursive(tmpDir, 0755);
    writeFile(tmpDir + "/module.prop", "id=x\nname=x\nversion=x\nauthor=x\n", 0644);

    assert(MagiskCompat::isMagiskModule(tmpDir));

    ::unlink((tmpDir + "/module.prop").c_str());
    ::rmdir(tmpDir.c_str());
    std::printf("test_is_magisk_module_true OK\n");
}

static void test_is_magisk_module_false() {
    std::string tmpDir = "/tmp/nexus_test_not_magisk";
    mkdirRecursive(tmpDir, 0755);
    // 没有 module.prop

    assert(!MagiskCompat::isMagiskModule(tmpDir));

    ::rmdir(tmpDir.c_str());
    std::printf("test_is_magisk_module_false OK\n");
}

static void test_convert_to_manifest() {
    std::string tmpDir = "/tmp/nexus_test_convert";
    mkdirRecursive(tmpDir, 0755);
    writeFile(tmpDir + "/module.prop",
              "id=convert_mod\nname=Convert\nversion=v1.0\nversionCode=10\nauthor=test\n",
              0644);
    // 创建脚本与 system/ 目录，验证 capabilities 自动推断
    writeFile(tmpDir + "/service.sh", "#!/system/bin/sh\necho hi\n", 0755);
    mkdirRecursive(tmpDir + "/system", 0755);

    auto r = MagiskCompat::convertToManifest(tmpDir);
    assert(r);
    // 验证 JSON 包含必要字段
    assert(r->find("\"id\": \"convert_mod\"") != std::string::npos);
    assert(r->find("\"name\": \"Convert\"") != std::string::npos);
    assert(r->find("EXECUTE_SHELL") != std::string::npos);   // 有 .sh
    assert(r->find("MOUNT_FILESYSTEM") != std::string::npos); // 有 system/

    // 清理
    ::unlink((tmpDir + "/module.prop").c_str());
    ::unlink((tmpDir + "/service.sh").c_str());
    ::rmdir((tmpDir + "/system").c_str());
    ::rmdir(tmpDir.c_str());
    std::printf("test_convert_to_manifest OK\n");
}

static void test_convert_module_creates_manifest() {
    std::string tmpDir = "/tmp/nexus_test_convert_module";
    mkdirRecursive(tmpDir, 0755);
    writeFile(tmpDir + "/module.prop",
              "id=auto_convert\nname=Auto\nversion=v1.0\nauthor=test\n",
              0644);

    auto r = MagiskCompat::convertModule(tmpDir);
    assert(r);

    // 验证 manifest.json 已生成
    assert(probeFile(tmpDir + "/manifest.json"));

    // 清理
    ::unlink((tmpDir + "/module.prop").c_str());
    ::unlink((tmpDir + "/manifest.json").c_str());
    ::rmdir(tmpDir.c_str());
    std::printf("test_convert_module_creates_manifest OK\n");
}

static void test_get_shim_script() {
    auto shim = MagiskCompat::getShimScript();
    // 验证包含必要的 shim 函数
    assert(shim.find("ui_print") != std::string::npos);
    assert(shim.find("set_perm") != std::string::npos);
    assert(shim.find("set_perm_recursive") != std::string::npos);
    assert(shim.find("abort") != std::string::npos);
    std::printf("test_get_shim_script OK\n");
}

static void test_list_sh_files() {
    std::string tmpDir = "/tmp/nexus_test_sh_files";
    mkdirRecursive(tmpDir, 0755);
    writeFile(tmpDir + "/service.sh", "#!/system/bin/sh\n", 0755);
    writeFile(tmpDir + "/post-fs-data.sh", "#!/system/bin/sh\n", 0755);
    writeFile(tmpDir + "/customize.sh", "#!/system/bin/sh\n", 0755);
    writeFile(tmpDir + "/not_a_script.txt", "not a script\n", 0644);

    auto files = MagiskCompat::listShFiles(tmpDir);
    assert(files.size() == 3);

    // 清理
    ::unlink((tmpDir + "/service.sh").c_str());
    ::unlink((tmpDir + "/post-fs-data.sh").c_str());
    ::unlink((tmpDir + "/customize.sh").c_str());
    ::unlink((tmpDir + "/not_a_script.txt").c_str());
    ::rmdir(tmpDir.c_str());
    std::printf("test_list_sh_files OK\n");
}

static void test_has_system_dir() {
    std::string tmpDir = "/tmp/nexus_test_system_dir";
    mkdirRecursive(tmpDir, 0755);

    assert(!MagiskCompat::hasSystemDir(tmpDir));

    mkdirRecursive(tmpDir + "/system", 0755);
    assert(MagiskCompat::hasSystemDir(tmpDir));

    ::rmdir((tmpDir + "/system").c_str());
    ::rmdir(tmpDir.c_str());
    std::printf("test_has_system_dir OK\n");
}

int main() {
    test_parse_module_prop_valid();
    test_parse_module_prop_missing_fields();
    test_parse_module_prop_nonexistent();
    test_parse_module_prop_with_comments();
    test_is_magisk_module_true();
    test_is_magisk_module_false();
    test_convert_to_manifest();
    test_convert_module_creates_manifest();
    test_get_shim_script();
    test_list_sh_files();
    test_has_system_dir();
    std::printf("All magisk_compat tests passed.\n");
    return 0;
}
