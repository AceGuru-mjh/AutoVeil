#include "nexus/env_detector.h"
#include "nexus/util.h"
#include "nexus/log.h"

#ifdef __ANDROID__
#include <cutils/properties.h>
#endif
#include <sys/stat.h>
#include <unistd.h>

namespace nexus {

RootProvider RootEnvironmentDetector::detectProvider() {
    // Magisk: /data/adb/magisk/.magisk 目录存在
    if (probeDir("/data/adb/magisk") || probeFile("/data/adb/magisk/.magisk")) {
        return RootProvider::Magisk;
    }
    // KernelSU: /data/adb/ksu/.config 存在
    if (probeDir("/data/adb/ksu") || probeFile("/data/adb/ksu/.config")) {
        return RootProvider::KernelSU;
    }
    // APatch: /data/adb/ap 存在
    if (probeDir("/data/adb/ap")) {
        return RootProvider::APatch;
    }
    return RootProvider::None;
}

Result<RootEnvironment> RootEnvironmentDetector::detect() {
    RootEnvironment env;
    env.provider = detectProvider();

    // 失败条件：所有 provider 都未找到
    // 但仍然返回 env（让 caller 决定是否进入只读模式）
    // 这里改为：未找到 provider 也算成功，但 caller 通过 env.provider == None 判断
    // 实际严格语义：provider == None 应进只读模式（main.cpp 处理）
    if (env.provider == RootProvider::None) {
        NX_LOG_W("EnvDetector", "no root provider detected; daemon will run read-only");
        return std::unexpected(Err::NotFound);
    }

    // provider 版本
    char ver[32] = {0};
    switch (env.provider) {
        case RootProvider::Magisk:
#ifdef __ANDROID__
            ::property_get("ro.magisk.version", ver, "");
#endif
            env.adbRootDir = "/data/adb/magisk";
            break;
        case RootProvider::KernelSU:
#ifdef __ANDROID__
            ::property_get("ro.kernelsu.version", ver, "");
#endif
            env.adbRootDir = "/data/adb/ksu";
            break;
        case RootProvider::APatch:
#ifdef __ANDROID__
            ::property_get("ro.apatch.version", ver, "");
#endif
            env.adbRootDir = "/data/adb/ap";
            break;
        case RootProvider::None:
            break;
    }
    env.providerVersion = ver;

    env.overlayBase   = "/data/adb/nexuscore/overlay";
    env.modulesDir    = "/data/adb/nexuscore/modules";

    // 整改 #12：sepolicyWritable 用 ::access(W_OK) 而非 probeFile
    env.sepolicyPath      = "/sys/fs/selinux/policy";
    env.sepolicyWritable  = (::access(env.sepolicyPath.c_str(), W_OK) == 0);

    env.overlayFsAvailable = kernelSupports("overlay");
    env.fuseAvailable      = kernelSupports("fuse");
    env.dynamicPartitions  = isDynamicPartitions();

    NX_LOG_I("EnvDetector", "env: provider=%s ver=%s overlay=%d fuse=%d dp=%d sepolicy_w=%d",
             rootProviderName(env.provider), env.providerVersion.c_str(),
             (int)env.overlayFsAvailable, (int)env.fuseAvailable,
             (int)env.dynamicPartitions, (int)env.sepolicyWritable);

    return env;
}

} // namespace nexus
