#include "nexus/module_loader.h"
#include "nexus/magisk_compat.h"
#include "nexus/util.h"
#include "nexus/log.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <regex>
#include <sys/stat.h>

// 整改：MVP 不依赖完整 nlohmann/json，使用极简手写 JSON parser
// 仅支持 manifest.json 用到的子集（string / int / bool / array / object）
// 生产应换为 nlohmann/json。
namespace nexus {

namespace {

// 极简 JSON value
struct JsonValue {
    enum Type { Null, Bool, Int, String, Array, Object };
    Type type = Null;
    bool boolVal = false;
    int64_t intVal = 0;
    std::string strVal;
    std::vector<JsonValue> arrVal;
    std::vector<std::pair<std::string, JsonValue>> objVal;

    const JsonValue* find(const std::string& key) const {
        if (type != Object) return nullptr;
        for (auto& [k, v] : objVal) {
            if (k == key) return &v;
        }
        return nullptr;
    }
};

// 极简递归下降 parser
struct JsonParser {
    const std::string& s;
    size_t i = 0;
    explicit JsonParser(const std::string& str) : s(str) {}

    void skipWs() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    }

    bool parse(JsonValue& out) {
        skipWs();
        if (i >= s.size()) return false;
        char c = s[i];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') { out.type = JsonValue::String; return parseString(out.strVal); }
        if (c == 't' || c == 'f') return parseBool(out);
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
        if (c == 'n') return parseNull(out);
        return false;
    }

    bool parseObject(JsonValue& out) {
        out.type = JsonValue::Object;
        ++i; skipWs();
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        while (i < s.size()) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (i >= s.size() || s[i] != ':') return false;
            ++i;
            JsonValue v;
            if (!parse(v)) return false;
            out.objVal.emplace_back(std::move(key), std::move(v));
            skipWs();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            return false;
        }
        return false;
    }

    bool parseArray(JsonValue& out) {
        out.type = JsonValue::Array;
        ++i; skipWs();
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        while (i < s.size()) {
            JsonValue v;
            if (!parse(v)) return false;
            out.arrVal.push_back(std::move(v));
            skipWs();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            return false;
        }
        return false;
    }

    bool parseString(std::string& out) {
        if (s[i] != '"') return false;
        ++i;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char e = s[i + 1];
                if (e == 'n') out.push_back('\n');
                else if (e == 't') out.push_back('\t');
                else if (e == 'r') out.push_back('\r');
                else if (e == '"') out.push_back('"');
                else if (e == '\\') out.push_back('\\');
                else if (e == '/') out.push_back('/');
                else out.push_back(e);
                i += 2;
            } else {
                out.push_back(s[i]);
                ++i;
            }
        }
        if (i >= s.size()) return false;
        ++i;
        return true;
    }

    bool parseBool(JsonValue& out) {
        out.type = JsonValue::Bool;
        if (s.compare(i, 4, "true") == 0) {
            out.boolVal = true; i += 4; return true;
        }
        if (s.compare(i, 5, "false") == 0) {
            out.boolVal = false; i += 5; return true;
        }
        return false;
    }

    bool parseNull(JsonValue& out) {
        if (s.compare(i, 4, "null") == 0) {
            out.type = JsonValue::Null; i += 4; return true;
        }
        return false;
    }

    bool parseNumber(JsonValue& out) {
        size_t start = i;
        if (s[i] == '-') ++i;
        size_t digits_start = i;
        while (i < s.size() && (s[i] >= '0' && s[i] <= '9')) ++i;
        // Phase 1.2 修复：std::stoll 在 -fno-exceptions 下抛 std::terminate。
        // 改用 strtoll + errno 检查。
        if (i == digits_start) {
            // 单独的 '-' 不是合法数字
            return false;
        }
        std::string numstr = s.substr(start, i - start);
        errno = 0;
        char* end = nullptr;
        long long v = std::strtoll(numstr.c_str(), &end, 10);
        if (end == numstr.c_str() || *end != '\0' || errno == ERANGE) {
            return false;
        }
        out.type = JsonValue::Int;
        out.intVal = v;
        return true;
    }
};

} // namespace

Result<ModuleManifest> ModuleLoader::parseManifest(const std::string& path) {
    auto content = readFile(path);
    if (!content) return {unexpect, Err::NotFound};

    JsonParser p(*content);
    JsonValue root;
    if (!p.parse(root) || root.type != JsonValue::Object) {
        return {unexpect, Err::InvalidArg};
    }

    ModuleManifest m;
    if (auto* v = root.find("id"))         if (v->type == JsonValue::String) m.id = v->strVal;
    if (auto* v = root.find("name"))       if (v->type == JsonValue::String) m.name = v->strVal;
    if (auto* v = root.find("version"))    if (v->type == JsonValue::String) m.version = v->strVal;
    if (auto* v = root.find("versionCode")) if (v->type == JsonValue::Int) m.versionCode = (int)v->intVal;
    if (auto* v = root.find("author"))     if (v->type == JsonValue::String) m.author = v->strVal;
    if (auto* v = root.find("description")) if (v->type == JsonValue::String) m.description = v->strVal;
    if (auto* v = root.find("min_nexus_version")) if (v->type == JsonValue::String) m.minNexusVersion = v->strVal;
    if (auto* v = root.find("priority"))   if (v->type == JsonValue::Int) m.priority = (int)v->intVal;
    if (auto* v = root.find("enabled"))    if (v->type == JsonValue::Bool) m.enabled = v->boolVal;
    if (auto* v = root.find("homepage"))   if (v->type == JsonValue::String) m.homepage = v->strVal;
    if (auto* v = root.find("capabilities")) {
        if (v->type == JsonValue::Array) {
            for (auto& cap : v->arrVal) {
                if (cap.type == JsonValue::String) m.capabilities.push_back(cap.strVal);
            }
        }
    }

    // 必填字段校验
    if (m.id.empty() || m.name.empty() || m.version.empty() || m.author.empty()) {
        return {unexpect, Err::InvalidArg};
    }
    if (!isValidId(m.id)) {
        return {unexpect, Err::InvalidArg};
    }
    return m;
}

bool ModuleLoader::isValidIdStatic(const std::string& id) {
    // Phase 1.3 修复：原用 std::regex，性能差且 <regex> 在某些 NDK 上体积大。
    // 改用手写校验：与 spec-03 §2.3 的正则 ^[a-z][a-z0-9_]{2,63}$ 等价。
    if (id.empty()) return false;
    // 第一个字符必须是 a-z
    char first = id[0];
    if (first < 'a' || first > 'z') return false;
    // 总长度 3-64 字符（第一个 + 至少 2 个 + 最多 63 个）
    if (id.size() < 3 || id.size() > 64) return false;
    // 其余字符必须是 a-z / 0-9 / _
    for (size_t i = 1; i < id.size(); ++i) {
        char c = id[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

std::vector<std::string> ModuleLoader::listSystemFiles(const std::string& modulePath) {
    std::vector<std::string> out;
    std::string systemDir = modulePath + "/system";
    if (!probeDir(systemDir)) return out;

    std::function<void(const std::string&, const std::string&)> walk;
    walk = [&](const std::string& base, const std::string& rel) {
        std::string full = base + (rel.empty() ? "" : "/" + rel);
        DIR* d = ::opendir(full.c_str());
        if (!d) return;
        struct dirent* e;
        while ((e = ::readdir(d))) {
            std::string name = e->d_name;
            if (name == "." || name == "..") continue;
            std::string childRel = rel.empty() ? name : rel + "/" + name;
            std::string childFull = base + "/" + childRel;
            struct stat st{};
            if (::stat(childFull.c_str(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) {
                walk(base, childRel);
            } else if (S_ISREG(st.st_mode)) {
                out.push_back(childRel);
            }
        }
        ::closedir(d);
    };
    walk(systemDir, "");
    return out;
}

Result<std::vector<ModuleLoader::LoadedModule>> ModuleLoader::scanModules() {
    std::vector<LoadedModule> result;
    if (!probeDir(modulesDir_)) {
        NX_LOG_W("ModuleLoader", "modules dir not found: %s", modulesDir_.c_str());
        return result;   // 空列表，不致命
    }

    DIR* d = ::opendir(modulesDir_.c_str());
    if (!d) return {unexpect, Err::IoError};
    struct dirent* e;
    while ((e = ::readdir(d))) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string modulePath = modulesDir_ + "/" + name;
        struct stat st{};
        if (::stat(modulePath.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        std::string manifestPath = modulePath + "/manifest.json";
        auto mr = parseManifest(manifestPath);
        if (!mr) {
            // Phase 6: 尝试 Magisk 兼容 - 检测 module.prop 并自动转换
            if (MagiskCompat::isMagiskModule(modulePath)) {
                NX_LOG_I("ModuleLoader", "%s: detected Magisk module, converting...", name.c_str());
                auto convR = MagiskCompat::convertModule(modulePath);
                if (convR) {
                    // 转换成功，重新解析 manifest.json
                    mr = parseManifest(manifestPath);
                }
            }
            if (!mr) {
                NX_LOG_W("ModuleLoader", "skip %s: invalid manifest (%s)",
                         name.c_str(), errString(mr.error()));
                continue;
            }
        }

        LoadedModule lm;
        lm.manifest = std::move(*mr);
        lm.path = modulePath;
        lm.hasPostFsData = probeFile(modulePath + "/post-fs-data.sh");
        lm.hasService = probeFile(modulePath + "/service.sh");
        lm.hasCustomize = probeFile(modulePath + "/customize.sh");
        lm.hasUninstall = probeFile(modulePath + "/uninstall.sh");
        lm.hasVerify = probeFile(modulePath + "/verify.sh");
        lm.systemFiles = listSystemFiles(modulePath);

        NX_LOG_I("ModuleLoader", "found %s v%s (priority=%d, caps=%zu, files=%zu)",
                 lm.manifest.id.c_str(), lm.manifest.version.c_str(),
                 lm.manifest.priority, lm.manifest.capabilities.size(),
                 lm.systemFiles.size());
        result.push_back(std::move(lm));
    }
    ::closedir(d);

    // 按 priority 升序排序（值小先挂载，值大覆盖前者）
    std::sort(result.begin(), result.end(),
              [](const LoadedModule& a, const LoadedModule& b) {
                  return a.manifest.priority < b.manifest.priority;
              });

    return result;
}

ModuleLoader::ValidationReport ModuleLoader::validate(const LoadedModule& m) {
    ValidationReport r;

    // 校验 capabilities 声明与脚本/挂载匹配
    auto hasCap = [&](const std::string& c) {
        return std::find(m.manifest.capabilities.begin(),
                         m.manifest.capabilities.end(), c) != m.manifest.capabilities.end();
    };

    if ((m.hasPostFsData || m.hasService || m.hasCustomize || m.hasUninstall) &&
        !hasCap("EXECUTE_SHELL")) {
        r.warnings.push_back(
            "module provides .sh scripts but did not declare EXECUTE_SHELL; "
            "scripts will be skipped (CAPABILITY_DENIED)");
    }
    if (!m.systemFiles.empty() && !hasCap("MOUNT_FILESYSTEM")) {
        r.warnings.push_back(
            "module provides system/ files but did not declare MOUNT_FILESYSTEM; "
            "files will not be mounted");
    }

    // 校验 min_nexus_version（MVP 仅记录）
    if (!m.manifest.minNexusVersion.empty()) {
        // TODO: 实际比较版本号
    }

    return r;
}

std::vector<MountTarget> ModuleLoader::collectMountTargets(const LoadedModule& m) {
    std::vector<MountTarget> out;
    // 仅当声明 MOUNT_FILESYSTEM 能力时才生成挂载目标
    auto hasCap = [&](const std::string& c) {
        return std::find(m.manifest.capabilities.begin(),
                         m.manifest.capabilities.end(), c) != m.manifest.capabilities.end();
    };
    if (!hasCap("MOUNT_FILESYSTEM")) {
        NX_LOG_I("ModuleLoader", "%s: MOUNT_FILESYSTEM not declared; skipping %zu system files",
                 m.manifest.id.c_str(), m.systemFiles.size());
        return out;
    }
    for (auto& rel : m.systemFiles) {
        MountTarget t;
        t.source = m.path + "/system/" + rel;
        t.target = "/" + rel;
        t.moduleId = m.manifest.id;
        out.push_back(std::move(t));
    }
    return out;
}

} // namespace nexus
