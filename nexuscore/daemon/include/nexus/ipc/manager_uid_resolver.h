#pragma once

#include "nexus/types.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace nexus::ipc {

// Manager UID 解析器
//
// 整改 #5：原 spec chmod 0660 + chown root:system，Manager 是 untrusted_app
// 不在 gid=1000 组，无法 connect。解决：动态读取 Manager 的 UID 并 chown socket。
//
// 解析来源（按优先级）：
// 1. /data/adb/nexuscore/manager_uid（Magisk 模块 post-fs-data 写入）
// 2. 通过 pm path com.nexus.manager 查找 APK 路径，stat 得到 UID
// 3. 解析失败返回空 optional
class ManagerUidResolver {
public:
    static std::optional<int> resolve();
};

} // namespace nexus::ipc
