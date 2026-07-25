#pragma once

#include "nexus/types.h"
#include "nexus/util.h"
#include <string>
#include <vector>

namespace nexus {

/// Boot Image Patcher
///
/// Phase 2：NexusCore 自研的 boot image 修补器，不抄 Magisk boot_patch.sh。
///
/// 工作原理（与 Magisk 完全不同）：
///
/// Magisk:
///   1. 解析 boot.img 的 Android Boot Header
///   2. 解压 kernel（gzip/lz4）
///   3. 在 ramdisk 内注入 magiskinit
///   4. 重新打包 boot.img
///
/// NexusCore (创新方案)：
///   1. 解析 Android Vendor Boot Header (AVB 2.0 + Boot Header v4)
///   2. 不修改 kernel，仅修改 ramdisk
///   3. 在 ramdisk 中注入 nexusinit 二进制 + nexus.rc service 定义
///   4. init 在挂载 /data 后启动 nexusd，nexusd 接管 root 与模块系统
///   5. 使用自研签名方案（不依赖 AVB，绕过 verified boot）
///
/// 支持的 boot header 版本：
///   - v0 (legacy Android < 8.0)
///   - v1-v4 (Android 8.0+)
///   - vendor_boot v3-v4 (Android 11+ GKI)
///
/// 不支持的方案（与 Magisk 区别）：
///   - 不修改 boot.img 的 kernel（避免兼容性问题）
///   - 不依赖 recovery 模式安装
///   - 不通过 vbmeta 修改 verified boot
class BootPatcher {
public:
    /// Boot image 信息
    struct BootImageInfo {
        std::string bootImgPath;
        std::string outputPath;
        uint32_t headerVersion = 0;
        uint32_t kernelSize = 0;
        uint32_t ramdiskSize = 0;
        uint32_t secondSize = 0;       // secondary bootloader
        uint32_t pageSize = 4096;
        std::string sha1;              // 原始 SHA1（用于回滚）
        std::string cmdline;           // 原 cmdline
        bool isVendorBoot = false;
    };

    /// 修补结果
    struct PatchResult {
        bool success = false;
        std::string outputPath;
        std::string errorMsg;
        std::string sha1;              // 修补后 SHA1
        bool needReboot = true;
    };

    /// 修补 boot image
    ///
    /// @param bootImgPath 原始 boot.img 路径
    /// @param outputPath 修补后输出路径
    /// @param nexusdPath nexusd 二进制路径（注入到 ramdisk）
    /// @param initScript nexusinit 启动脚本内容
    /// @return 修补结果
    static PatchResult patch(
        const std::string& bootImgPath,
        const std::string& outputPath,
        const std::string& nexusdPath,
        const std::string& initScript);

    /// 解析 boot image header
    static Result<BootImageInfo> parseHeader(const std::string& bootImgPath);

    /// 提取 ramdisk 段（cpio 格式）
    static Result<std::vector<uint8_t>> extractRamdisk(const BootImageInfo& info);

    /// 修改 ramdisk：注入 nexusinit + nexus.rc
    ///
    /// 不抄 Magisk 的 ramdisk 修改方式（magiskinit 用 cpio 重组），
    /// NexusCore 用自研的 cpio 增量修改器：保留原 ramdisk 全部内容，
    /// 仅追加 nexusinit 二进制 + nexus.rc service 定义。
    static Result<std::vector<uint8_t>> patchRamdisk(
        const std::vector<uint8_t>& original,
        const std::string& nexusdPath,
        const std::string& initScript);

    /// 重新打包 boot image
    static Result<std::vector<uint8_t>> repackImage(
        const BootImageInfo& info,
        const std::vector<uint8_t>& newRamdisk);

    /// 检测当前设备 boot image 路径
    /// 通常为 /dev/block/by-name/boot_a 或 boot_b (A/B 设备)
    static Result<std::string> detectBootPath();

    /// 备份原始 boot image（用于卸载时恢复）
    static Result<void> backupOriginal(const std::string& bootImgPath);

    /// 恢复原始 boot image（卸载时调用）
    static Result<void> restoreOriginal();

private:
    /// 检查 boot image magic（ANDROID!）
    /// 重载支持 string 与 vector 两种调用方式
    /// Phase 7: 改为 public 供测试调用
public:
    static bool checkMagic(const std::vector<uint8_t>& data);
    static bool checkMagic(const std::string& data);
private:

    /// 计算并写入新的 SHA1
    static void updateSha1(std::vector<uint8_t>& image, const BootImageInfo& info);

    /// 解析 cpio 头部
    struct CpioHeader {
        uint32_t magic;       // 0x070701 or 0x070702 (newc/oldc)
        uint32_t ino;
        uint32_t mode;
        uint32_t uid;
        uint32_t gid;
        uint32_t nlink;
        uint32_t mtime;
        uint32_t filesize;
        uint32_t devmajor;
        uint32_t devminor;
        uint32_t rdevmajor;
        uint32_t rdevminor;
        uint32_t namesize;
        uint32_t check;
    };

    /// 读取 cpio 头部
    static Result<CpioHeader> readCpioHeader(const std::vector<uint8_t>& data, size_t offset);

    /// 写入 cpio entry
    static void writeCpioEntry(
        std::vector<uint8_t>& out,
        const std::string& name,
        const std::vector<uint8_t>& content,
        uint32_t mode);
};

} // namespace nexus
