// Boot Image Patcher 实现
//
// Phase 2：NexusCore 自研 boot patcher，不抄 Magisk boot_patch.sh。
//
// 关键差异：
// 1. 不依赖任何外部工具（如 magiskboot / mkbootimg）
// 2. C++ 直接解析 Android Boot Header v0-v4
// 3. 自研 cpio 解析器（newc 格式）
// 4. 仅修改 ramdisk，不动 kernel
//
// 详见 nexus/boot/boot_patcher.h 的设计文档注释。

#include "nexus/boot/boot_patcher.h"
#include "nexus/util.h"
#include "nexus/log.h"

#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <openssl/sha.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nexus {

// Android Boot Image Header v0 magic
static const char BOOT_MAGIC[] = "ANDROID!";
static const size_t BOOT_MAGIC_SIZE = 8;

// Vendor Boot Header v3+ magic
static const char VENDOR_BOOT_MAGIC[] = "VNDRBOOT";
static const size_t VENDOR_BOOT_MAGIC_SIZE = 8;

// CPIO newc 格式 magic
static const uint32_t CPIO_MAGIC_NEWC = 0x070701;
static const uint32_t CPIO_MAGIC_NEWC_CRC = 0x070702;
static const char CPIO_TRAILER[] = "TRAILER!!!";

// Android Boot Image Header v0 (legacy)
struct BootImageHeaderV0 {
    char magic[BOOT_MAGIC_SIZE];
    uint32_t kernelSize;
    uint32_t kernelAddr;
    uint32_t ramdiskSize;
    uint32_t ramdiskAddr;
    uint32_t secondSize;
    uint32_t secondAddr;
    uint32_t tagsAddr;
    uint32_t pageSize;
    uint32_t headerVersion;
    uint32_t osVersion;
    char name[16];
    char cmdline[512];
    uint32_t id[8];
    uint8_t extraCmdline[1024];
};

// Android Boot Image Header v1-v2 (扩展版)
struct BootImageHeaderV1 {
    char magic[BOOT_MAGIC_SIZE];
    uint32_t kernelSize;
    uint32_t kernelAddr;
    uint32_t ramdiskSize;
    uint32_t ramdiskAddr;
    uint32_t secondSize;
    uint32_t secondAddr;
    uint32_t tagsAddr;
    uint32_t pageSize;
    uint32_t headerVersion;
    uint32_t osVersion;
    char name[16];
    char cmdline[512];
    uint32_t id[8];
    uint8_t extraCmdline[1024];
    uint32_t recoveryDtboSize;
    uint64_t recoveryDtboOffset;
    uint32_t headerSize;
};

// Android Boot Image Header v3-v4 (GKI)
struct BootImageHeaderV3 {
    char magic[BOOT_MAGIC_SIZE];
    uint32_t kernelSize;
    uint32_t ramdiskSize;
    uint32_t osVersion;
    uint32_t headerSize;
    uint32_t reserved[4];
    uint32_t headerVersion;
    char cmdline[1536];
};

// Vendor Boot Header v3-v4
struct VendorBootHeaderV3 {
    char magic[VENDOR_BOOT_MAGIC_SIZE];
    uint32_t headerVersion;
    uint32_t pageSize;
    uint32_t kernelAddr;
    uint32_t ramdiskAddr;
    uint32_t vendorRamdiskSize;
    char cmdline[2048];
    uint32_t tagsAddr;
    char name[16];
    uint32_t headerSize;
    uint32_t dtbSize;
    uint64_t dtbAddr;
};

bool BootPatcher::checkMagic(const std::vector<uint8_t>& data) {
    if (data.size() < BOOT_MAGIC_SIZE) return false;
    return std::memcmp(data.data(), BOOT_MAGIC, BOOT_MAGIC_SIZE) == 0 ||
           std::memcmp(data.data(), VENDOR_BOOT_MAGIC, VENDOR_BOOT_MAGIC_SIZE) == 0;
}

bool BootPatcher::checkMagic(const std::string& data) {
    if (data.size() < BOOT_MAGIC_SIZE) return false;
    return std::memcmp(data.data(), BOOT_MAGIC, BOOT_MAGIC_SIZE) == 0 ||
           std::memcmp(data.data(), VENDOR_BOOT_MAGIC, VENDOR_BOOT_MAGIC_SIZE) == 0;
}

Result<BootPatcher::BootImageInfo> BootPatcher::parseHeader(const std::string& bootImgPath) {
    auto content = readFile(bootImgPath);
    if (!content) {
        return {unexpect, Err::NotFound};
    }
    if (!checkMagic(*content)) {
        NX_LOG_E("BootPatcher", "invalid boot magic: %c%c%c%c",
                 content->size() > 0 ? (char)(*content)[0] : '?',
                 content->size() > 1 ? (char)(*content)[1] : '?',
                 content->size() > 2 ? (char)(*content)[2] : '?',
                 content->size() > 3 ? (char)(*content)[3] : '?');
        return {unexpect, Err::InvalidArg};
    }

    BootImageInfo info;
    info.bootImgPath = bootImgPath;

    // 检查是否为 vendor_boot
    bool isVendor = std::memcmp(content->data(), VENDOR_BOOT_MAGIC, VENDOR_BOOT_MAGIC_SIZE) == 0;
    info.isVendorBoot = isVendor;

    if (isVendor) {
        // Vendor Boot v3+
        if (content->size() < sizeof(VendorBootHeaderV3)) {
            return {unexpect, Err::InvalidArg};
        }
        VendorBootHeaderV3 hdr;
        std::memcpy(&hdr, content->data(), sizeof(hdr));
        info.headerVersion = hdr.headerVersion;
        info.pageSize = hdr.pageSize;
        info.ramdiskSize = hdr.vendorRamdiskSize;
        info.cmdline = hdr.cmdline;
    } else {
        // 普通 boot.img
        if (content->size() < sizeof(BootImageHeaderV0)) {
            return {unexpect, Err::InvalidArg};
        }
        BootImageHeaderV0 hdr;
        std::memcpy(&hdr, content->data(), sizeof(hdr));
        info.headerVersion = hdr.headerVersion;
        info.pageSize = hdr.pageSize;
        info.kernelSize = hdr.kernelSize;
        info.ramdiskSize = hdr.ramdiskSize;
        info.secondSize = hdr.secondSize;
        info.cmdline = hdr.cmdline;

        // 计算原始 SHA1（v0-v2 的 id 字段）
        if (info.headerVersion <= 2) {
            char sha1_hex[41];
            for (int i = 0; i < 8; ++i) {
                std::snprintf(sha1_hex + i * 8, 9, "%08x", hdr.id[i]);
            }
            sha1_hex[40] = '\0';
            info.sha1 = sha1_hex;
        }
    }

    NX_LOG_I("BootPatcher", "parsed: version=%u pageSize=%u kernel=%u ramdisk=%u isVendor=%d",
             info.headerVersion, info.pageSize, info.kernelSize, info.ramdiskSize,
             (int)info.isVendorBoot);
    return info;
}

Result<std::vector<uint8_t>> BootPatcher::extractRamdisk(const BootImageInfo& info) {
    auto content = readFile(info.bootImgPath);
    if (!content) {
        return {unexpect, Err::IoError};
    }

    // 计算 ramdisk 偏移
    // 对于 v0-v2：header_size + kernel_size（按 page 对齐）
    // 对于 v3+：header + kernel
    // 对于 vendor_boot：header + vendor_ramdisk（位置不同）
    size_t ramdiskOffset = 0;
    if (info.isVendorBoot) {
        // vendor_boot: header + dtb（如果有）+ vendor_ramdisk
        // 简化：vendor_boot v3 的 ramdisk 在 header 后
        ramdiskOffset = info.pageSize;   // page-aligned header
    } else {
        // 普通_boot：page + kernel (page-aligned)
        size_t headerPages = 1;
        size_t kernelPages = (info.kernelSize + info.pageSize - 1) / info.pageSize;
        ramdiskOffset = (headerPages + kernelPages) * info.pageSize;
    }

    if (ramdiskOffset + info.ramdiskSize > content->size()) {
        NX_LOG_E("BootPatcher", "ramdisk out of bounds: offset=%zu size=%u total=%zu",
                 ramdiskOffset, info.ramdiskSize, content->size());
        return {unexpect, Err::InvalidArg};
    }

    std::vector<uint8_t> ramdisk(
        content->begin() + ramdiskOffset,
        content->begin() + ramdiskOffset + info.ramdiskSize);
    return ramdisk;
}

Result<BootPatcher::CpioHeader> BootPatcher::readCpioHeader(
    const std::vector<uint8_t>& data, size_t offset) {
    if (offset + sizeof(CpioHeader) > data.size()) {
        return {unexpect, Err::InvalidArg};
    }
    CpioHeader hdr;
    // CPIO newc 格式是 ASCII hex 字符串，不是二进制
    char hex[9];
    auto parseHex = [&](size_t pos) -> uint32_t {
        std::memcpy(hex, data.data() + offset + pos, 8);
        hex[8] = '\0';
        return std::strtoul(hex, nullptr, 16);
    };
    hdr.magic = parseHex(0);
    hdr.ino = parseHex(8);
    hdr.mode = parseHex(16);
    hdr.uid = parseHex(24);
    hdr.gid = parseHex(32);
    hdr.nlink = parseHex(40);
    hdr.mtime = parseHex(48);
    hdr.filesize = parseHex(56);
    hdr.devmajor = parseHex(64);
    hdr.devminor = parseHex(72);
    hdr.rdevmajor = parseHex(80);
    hdr.rdevminor = parseHex(88);
    hdr.namesize = parseHex(96);
    hdr.check = parseHex(104);
    return hdr;
}

void BootPatcher::writeCpioEntry(
    std::vector<uint8_t>& out,
    const std::string& name,
    const std::vector<uint8_t>& content,
    uint32_t mode) {
    // 写 CPIO newc 格式 entry
    auto writeHex = [&](uint32_t v) {
        char hex[9];
        std::snprintf(hex, sizeof(hex), "%08x", v);
        out.insert(out.end(), hex, hex + 8);
    };
    writeHex(CPIO_MAGIC_NEWC);     // magic
    writeHex(1);                    // ino (递增)
    writeHex(mode);                 // mode
    writeHex(0);                    // uid
    writeHex(0);                    // gid
    writeHex(1);                    // nlink
    writeHex(0);                    // mtime
    writeHex((uint32_t)content.size()); // filesize
    writeHex(0);                    // devmajor
    writeHex(0);                    // devminor
    writeHex(0);                    // rdevmajor
    writeHex(0);                    // rdevminor
    writeHex((uint32_t)name.size() + 1); // namesize (含 null)
    writeHex(0);                    // check

    // name + null
    out.insert(out.end(), name.begin(), name.end());
    out.push_back(0);

    // 4 字节对齐
    while (out.size() % 4 != 0) {
        out.push_back(0);
    }

    // content
    out.insert(out.end(), content.begin(), content.end());

    // 4 字节对齐
    while (out.size() % 4 != 0) {
        out.push_back(0);
    }
}

Result<std::vector<uint8_t>> BootPatcher::patchRamdisk(
    const std::vector<uint8_t>& original,
    const std::string& nexusdPath,
    const std::string& initScript) {
    // Phase 2 创新：
    // 不抄 Magisk 的 cpio 重组方式（magiskinit 替换 init）
    // NexusCore 用"增量追加"：保留原 ramdisk 全部内容，
    // 仅在末尾追加 nexusinit + nexus.rc + nexusd 二进制

    NX_LOG_I("BootPatcher", "patching ramdisk: orig=%zu bytes", original.size());

    std::vector<uint8_t> out(original);

    // 读 nexusd 二进制
    auto nexusdBin = readFile(nexusdPath);
    if (!nexusdBin) {
        NX_LOG_E("BootPatcher", "cannot read nexusd: %s", nexusdPath.c_str());
        return {unexpect, Err::IoError};
    }

    // 追加 nexusd 二进制（mode 0755）
    // Phase 1 修复：readFile 返回 string，需转为 vector<uint8_t>
    std::vector<uint8_t> nexusdVec(nexusdBin->begin(), nexusdBin->end());
    writeCpioEntry(out, "nexusd", nexusdVec, 0100755);

    // 追加 nexus.rc（init service 定义，mode 0644）
    std::vector<uint8_t> rcContent(initScript.begin(), initScript.end());
    writeCpioEntry(out, "nexus.rc", rcContent, 0100644);

    // 追加 bootstrap 脚本（init.rc import 后执行，mode 0755）
    std::string bootstrap = R"(#!/system/bin/sh
# NexusCore bootstrap：在 init 早期由 nexus.rc service 启动
NEXUSD=/data/adb/nexuscore/bin/nexusd
mkdir -p /data/adb/nexuscore/bin
mkdir -p /data/adb/nexuscore/modules
mkdir -p /data/adb/nexuscore/overlay
mkdir -p /data/adb/nexuscore/logs
# 标记 NexusCore 已完成 bootstrap
echo "1.0.0" > /data/adb/nexuscore/.version
touch /data/adb/nexuscore/.bootstrapped
# 启动 daemon
if [ -x "$NEXUSD" ]; then
    "$NEXUSD" &
fi
)";
    std::vector<uint8_t> bootstrapBin(bootstrap.begin(), bootstrap.end());
    writeCpioEntry(out, "init.nexus.rc", bootstrapBin, 0100755);

    // 写 CPIO trailer
    std::vector<uint8_t> trailerContent;
    writeCpioEntry(out, CPIO_TRAILER, trailerContent, 0);

    // 4 字节对齐到块边界
    while (out.size() % 4 != 0) {
        out.push_back(0);
    }

    NX_LOG_I("BootPatcher", "patched ramdisk: new=%zu bytes (added %zu)",
             out.size(), out.size() - original.size());
    return out;
}

Result<std::vector<uint8_t>> BootPatcher::repackImage(
    const BootImageInfo& info,
    const std::vector<uint8_t>& newRamdisk) {
    auto content = readFile(info.bootImgPath);
    if (!content) {
        return {unexpect, Err::IoError};
    }

    // Phase 1 修复：readFile 返回 string，需用迭代器构造 vector
    std::vector<uint8_t> out(content->begin(), content->end());

    // 更新 ramdisk 内容
    // 简化：找到 ramdisk 偏移，替换为新 ramdisk
    // 实际生产需要重新计算 page 对齐 + 更新 header 中的 ramdiskSize
    size_t ramdiskOffset = 0;
    if (info.isVendorBoot) {
        ramdiskOffset = info.pageSize;
    } else {
        size_t headerPages = 1;
        size_t kernelPages = (info.kernelSize + info.pageSize - 1) / info.pageSize;
        ramdiskOffset = (headerPages + kernelPages) * info.pageSize;
    }

    // 替换 ramdisk 段
    if (ramdiskOffset + info.ramdiskSize > out.size()) {
        return {unexpect, Err::InvalidArg};
    }

    // 删除旧 ramdisk
    out.erase(out.begin() + ramdiskOffset,
              out.begin() + ramdiskOffset + info.ramdiskSize);

    // 插入新 ramdisk
    out.insert(out.begin() + ramdiskOffset,
               newRamdisk.begin(), newRamdisk.end());

    // 更新 header 中的 ramdiskSize
    if (!info.isVendorBoot) {
        if (info.headerVersion <= 2 && out.size() >= sizeof(BootImageHeaderV0)) {
            BootImageHeaderV0* hdr = reinterpret_cast<BootImageHeaderV0*>(out.data());
            hdr->ramdiskSize = (uint32_t)newRamdisk.size();
        } else if (info.headerVersion >= 3 && out.size() >= sizeof(BootImageHeaderV3)) {
            BootImageHeaderV3* hdr = reinterpret_cast<BootImageHeaderV3*>(out.data());
            hdr->ramdiskSize = (uint32_t)newRamdisk.size();
        }
    } else {
        if (out.size() >= sizeof(VendorBootHeaderV3)) {
            VendorBootHeaderV3* hdr = reinterpret_cast<VendorBootHeaderV3*>(out.data());
            hdr->vendorRamdiskSize = (uint32_t)newRamdisk.size();
        }
    }

    // 更新 SHA1（v0-v2 的 id 字段）
    updateSha1(out, info);

    NX_LOG_I("BootPatcher", "repacked image: %zu -> %zu bytes",
             content->size(), out.size());
    return out;
}

void BootPatcher::updateSha1(std::vector<uint8_t>& image, const BootImageInfo& info) {
    // 计算整个 image 的 SHA1（v0-v2 的 id 字段）
    // 实际 Android boot header 的 SHA1 计算方式更复杂（部分字段），这里简化
    if (info.headerVersion > 2) {
        return;   // v3+ 不用 id 字段
    }
    // 简化：只更新 id[0]
    if (image.size() < sizeof(BootImageHeaderV0)) {
        return;
    }
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(image.data() + sizeof(BootImageHeaderV0),
         image.size() - sizeof(BootImageHeaderV0),
         hash);
    BootImageHeaderV0* hdr = reinterpret_cast<BootImageHeaderV0*>(image.data());
    std::memset(hdr->id, 0, sizeof(hdr->id));
    std::memcpy(&hdr->id[0], hash, sizeof(uint32_t));
}

BootPatcher::PatchResult BootPatcher::patch(
    const std::string& bootImgPath,
    const std::string& outputPath,
    const std::string& nexusdPath,
    const std::string& initScript) {
    PatchResult result;

    NX_LOG_I("BootPatcher", "patching %s -> %s", bootImgPath.c_str(), outputPath.c_str());

    // 1. 解析 header
    auto infoR = parseHeader(bootImgPath);
    if (!infoR) {
        result.errorMsg = std::string("parseHeader failed: ") + errString(infoR.error());
        return result;
    }
    auto info = *infoR;

    // 2. 提取 ramdisk
    auto ramdiskR = extractRamdisk(info);
    if (!ramdiskR) {
        result.errorMsg = std::string("extractRamdisk failed: ") + errString(ramdiskR.error());
        return result;
    }

    // 3. 修改 ramdisk
    auto patchedRamdiskR = patchRamdisk(*ramdiskR, nexusdPath, initScript);
    if (!patchedRamdiskR) {
        result.errorMsg = std::string("patchRamdisk failed: ") + errString(patchedRamdiskR.error());
        return result;
    }

    // 4. 重新打包
    auto newImageR = repackImage(info, *patchedRamdiskR);
    if (!newImageR) {
        result.errorMsg = std::string("repackImage failed: ") + errString(newImageR.error());
        return result;
    }

    // 5. 写入输出文件
    if (!writeFile(outputPath,
                   std::string_view(reinterpret_cast<const char*>(newImageR->data()),
                                    newImageR->size()))) {
        result.errorMsg = "writeFile failed";
        return result;
    }

    result.success = true;
    result.outputPath = outputPath;
    result.needReboot = true;
    NX_LOG_I("BootPatcher", "patch succeeded: %s", outputPath.c_str());
    return result;
}

Result<std::string> BootPatcher::detectBootPath() {
    // 常见 boot image 路径
    const char* candidates[] = {
        "/dev/block/by-name/boot_a",
        "/dev/block/by-name/boot_b",
        "/dev/block/by-name/boot",
        nullptr,
    };
    for (int i = 0; candidates[i]; ++i) {
        if (probeFile(candidates[i])) {
            return std::string(candidates[i]);
        }
    }
    // 通过 /proc/cmdline 找 androidboot.slot
    auto cmdline = readFile("/proc/cmdline");
    if (cmdline) {
        std::string s = *cmdline;
        auto pos = s.find("androidboot.slot_suffix=");
        if (pos != std::string::npos) {
            std::string slot = s.substr(pos + 23, 2);   // _a or _b
            std::string path = "/dev/block/by-name/boot" + slot;
            if (probeFile(path)) {
                return path;
            }
        }
    }
    return {unexpect, Err::NotFound};
}

Result<void> BootPatcher::backupOriginal(const std::string& bootImgPath) {
    std::string backupPath = "/data/adb/nexuscore/backup/boot.img.orig";
    mkdirRecursive("/data/adb/nexuscore/backup", 0700);
    if (!copyFile(bootImgPath, backupPath)) {
        return {unexpect, Err::IoError};
    }
    NX_LOG_I("BootPatcher", "backed up original boot to %s", backupPath.c_str());
    return {};
}

Result<void> BootPatcher::restoreOriginal() {
    std::string backupPath = "/data/adb/nexuscore/backup/boot.img.orig";
    if (!probeFile(backupPath)) {
        return {unexpect, Err::NotFound};
    }
    auto bootPathR = detectBootPath();
    if (!bootPathR) {
        return {unexpect, bootPathR.error()};
    }
    if (!copyFile(backupPath, *bootPathR)) {
        return {unexpect, Err::IoError};
    }
    NX_LOG_I("BootPatcher", "restored original boot from %s", backupPath.c_str());
    return {};
}

} // namespace nexus
