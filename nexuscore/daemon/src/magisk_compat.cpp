// Magisk 兼容层实现
//
// Phase 5：让现有 Magisk 模块能在 NexusCore 上运行。

#include "nexus/magisk_compat.h"
#include "nexus/util.h"
#include "nexus/log.h"

#include <dirent.h>
#include <sys/stat.h>

namespace nexus {

namespace {

/// 解析 KV 格式（key=value 一行一对）
std::vector<std::pair<std::string, std::string>> parseKV(const std::string& content) {
    std::vector<std::pair<std::string, std::string>> result;
    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find('\n', pos);
        if (eol == std::string::npos) eol = content.size();
        std::string line = content.substr(pos, eol - pos);
        pos = eol + 1;

        // 跳过空行和注释
        if (line.empty() || line[0] == '#') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        result.emplace_back(std::move(key), std::move(val));
    }
    return result;
}

} // anonymous namespace

Result<ModuleManifest> MagiskCompat::parseModuleProp(const std::string& propPath) {
    auto content = readFile(propPath);
    if (!content) {
        return {unexpect, Err::NotFound};
    }

    auto kv = parseKV(*content);
    ModuleManifest m;
    for (auto& [key, val] : kv) {
        if (key == "id")          m.id = val;
        else if (key == "name")   m.name = val;
        else if (key == "version") m.version = val;
        else if (key == "versionCode") {
            errno = 0;
            m.versionCode = (int)std::strtol(val.c_str(), nullptr, 10);
        }
        else if (key == "author") m.author = val;
        else if (key == "description") m.description = val;
    }

    // 校验必填字段
    if (m.id.empty() || m.name.empty() || m.version.empty() || m.author.empty()) {
        return {unexpect, Err::InvalidArg};
    }
    // NexusCore 要求 min_nexus_version，Magisk 没有这个字段，默认 "1.0"
    m.minNexusVersion = "1.0";
    return m;
}

bool MagiskCompat::isMagiskModule(const std::string& moduleDir) {
    return probeFile(moduleDir + "/module.prop");
}

std::vector<std::string> MagiskCompat::listShFiles(const std::string& dir) {
    std::vector<std::string> result;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return result;
    struct dirent* e;
    while ((e = ::readdir(d))) {
        std::string name = e->d_name;
        if (name.size() < 4) continue;
        if (name.substr(name.size() - 3) == ".sh") {
            result.push_back(name);
        }
    }
    ::closedir(d);
    return result;
}

bool MagiskCompat::hasSystemDir(const std::string& dir) {
    return probeDir(dir + "/system");
}

Result<std::string> MagiskCompat::convertToManifest(const std::string& moduleDir) {
    auto mr = parseModuleProp(moduleDir + "/module.prop");
    if (!mr) {
        return {unexpect, mr.error()};
    }
    ModuleManifest& m = *mr;

    // 推断 capabilities
    std::vector<std::string> caps;
    auto shFiles = listShFiles(moduleDir);
    if (!shFiles.empty()) {
        caps.push_back("EXECUTE_SHELL");
    }
    if (hasSystemDir(moduleDir)) {
        caps.push_back("MOUNT_FILESYSTEM");
    }

    // 生成 JSON
    std::string json = "{\n";
    json += "  \"id\": \"" + m.id + "\",\n";
    json += "  \"name\": \"" + m.name + "\",\n";
    json += "  \"version\": \"" + m.version + "\",\n";
    if (m.versionCode > 0) {
        json += "  \"versionCode\": " + std::to_string(m.versionCode) + ",\n";
    }
    json += "  \"author\": \"" + m.author + "\",\n";
    if (!m.description.empty()) {
        json += "  \"description\": \"" + m.description + "\",\n";
    }
    json += "  \"min_nexus_version\": \"1.0\",\n";
    json += "  \"priority\": 0,\n";
    json += "  \"enabled\": true,\n";
    json += "  \"capabilities\": [";
    for (size_t i = 0; i < caps.size(); ++i) {
        if (i > 0) json += ", ";
        json += "\"" + caps[i] + "\"";
    }
    json += "]\n";
    json += "}\n";
    return json;
}

Result<void> MagiskCompat::convertModule(const std::string& moduleDir) {
    // 1. 生成 manifest.json
    auto manifestR = convertToManifest(moduleDir);
    if (!manifestR) {
        return {unexpect, manifestR.error()};
    }
    std::string manifestPath = moduleDir + "/manifest.json";
    if (!writeFile(manifestPath, *manifestR, 0644)) {
        return {unexpect, Err::IoError};
    }
    NX_LOG_I("MagiskCompat", "converted module: %s (wrote manifest.json)",
             moduleDir.c_str());
    return {};
}

std::string MagiskCompat::getShimScript() {
    return R"shim(#!/system/bin/sh
# NexusCore Magisk compatibility shim
# 自动注入到 customize.sh / post-fs-data.sh / service.sh 等脚本

# ui_print: 输出到 stdout，daemon 转发到 Manager 安装进度 UI
ui_print() { echo "- $*"; }

# set_perm: chown + chmod
set_perm() {
    local f=$1 owner=$2 group=$3 perm=$4
    chown "$owner:$group" "$f" 2>/dev/null
    chmod "$perm" "$f" 2>/dev/null
}

# set_perm_recursive: 递归 chown + chmod
set_perm_recursive() {
    local d=$1 owner=$2 group=$3 dperm=$4 fperm=$5
    chown -R "$owner:$group" "$d" 2>/dev/null
    find "$d" -type d -exec chmod "$dperm" {} \; 2>/dev/null
    find "$d" -type f -exec chmod "$fperm" {} \; 2>/dev/null
}

# abort: 输出错误并 exit 1
abort() { echo "! abort: $*" >&2; exit 1; }
)shim";
}

} // namespace nexus
