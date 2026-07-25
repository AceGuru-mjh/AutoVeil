# NexusCore Daemon (nexusd) — CMake build
#
# 目标：在 Android NDK 下静态编译 nexusd，输出 arm64-v8a 二进制。
# 构建命令（在 docker 或本地 NDK 环境）：
#   cmake -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-34 -DANDROID_STL=c++_static
#   cmake --build build-arm64 -j
#
# 依赖：
# - protobuf-lite（用于 nexus.proto 编解码，子模块或 prebuilt）
# - nlohmann/json（header-only，vendored 在 third_party/）
# - libsepol（可选，MVP 用 shell out magiskpolicy 替代）

cmake_minimum_required(VERSION 3.22)
project(nexusd
    VERSION 1.0.0
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(
    -Wall -Wextra -Wno-unused-parameter
    -ffunction-sections -fdata-sections
    -fno-exceptions -fno-rtti
)

# 版本宏（与 include/nexus/version.h 协同）
add_compile_definitions(NEXUS_VERSION="1.0.0")

# 整改：禁用 -Werror，避免新 NDK 的 warning 一票否决 CI。
# add_compile_options(-Werror)

# ============ third_party: nlohmann/json (header-only) ============
# 期望目录结构：
#   third_party/nlohmann/json.hpp
# 仓库 owner 需自行 git submodule add https://github.com/nlohmann/json third_party/nlohmann
set(JSON_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party)
if(EXISTS ${JSON_INCLUDE_DIR}/nlohmann/json.hpp)
    message(STATUS "nlohmann/json found at ${JSON_INCLUDE_DIR}")
else()
    message(WARNING "nlohmann/json not found; using minimal shim")
endif()

# ============ third_party: protobuf-lite ============
# MVP 简化：不依赖完整 protobuf 库，自己写极简帧编解码（参考 spec-01 §10.3 的 length-prefix + magic）
# 实际生产应 link libprotobuf-lite.a，详见 docs/build.md
set(NEXUS_PROTOBUF_LITE OFF CACHE BOOL "Use real protobuf-lite (otherwise use minimal hand-written codec)")

# ============ proto 生成 ============
# 复制 manager 端的 nexus.proto 到 daemon/proto/，保证两端协议一致
configure_file(
    ${CMAKE_SOURCE_DIR}/proto/nexus.proto
    ${CMAKE_BINARY_DIR}/proto/nexus.proto
    COPYONLY)

# ============ nexusd 可执行 ============
set(NEXUSD_SOURCES
    src/main.cpp
    src/log.cpp
    src/types.cpp
    src/util.cpp
    src/env_detector.cpp
    src/selinux_manager.cpp
    src/module_loader.cpp
    src/script_executor.cpp
    src/event_bus.cpp
    src/fs/i_file_system_interceptor.cpp
    src/fs/overlayfs_interceptor.cpp
    src/fs/bind_mount_interceptor.cpp
    src/fs/fs_detector.cpp
    src/fs/noop_interceptor.cpp
    src/ipc/ipc_server.cpp
    src/ipc/credential_check.cpp
    src/ipc/codec.cpp
    src/ipc/handlers.cpp
    src/ipc/manager_uid_resolver.cpp
)

add_executable(nexusd ${NEXUSD_SOURCES})

target_include_directories(nexusd PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_BINARY_DIR}/proto
)

# Android 系统库
target_link_libraries(nexusd PRIVATE
    log       # __android_log_print
    cutils    # property_get 等
)

# 静态链接减少依赖
if(ANDROID)
    target_link_options(nexusd PRIVATE -static-libstdc++ -static-libgcc)
endif()

# Strip release binary
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET nexusd POST_BUILD
        COMMAND ${CMAKE_STRIP} $<TARGET_FILE:nexusd>
        COMMENT "Stripping nexusd")
endif()

# ============ nexuscli (模块脚本可调用的 CLI 工具) ============
add_executable(nexuscli
    src/cli_main.cpp
    src/log.cpp
    src/ipc/codec.cpp
    src/ipc/nexuscli_lib.cpp
)
target_include_directories(nexuscli PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)
target_link_libraries(nexuscli PRIVATE log)

# ============ 单元测试（仅在非 Android 平台编译） ============
if(NOT ANDROID)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============ install ============
install(TARGETS nexusd nexuscli
    RUNTIME DESTINATION bin
)
