# Daemon 单元测试
#
# 使用 assert + main，避免 GoogleTest 外部依赖。
# 仅在 host 平台编译（CMAKE_SOURCE_DIR/CMakeLists.txt 中 if(NOT ANDROID) 启用）。

add_executable(nexusd_tests
    test_codec.cpp
    test_util.cpp
    test_module_loader.cpp
    test_event_bus.cpp
    test_script_executor.cpp
    test_boot_patcher.cpp
    test_magisk_compat.cpp
    test_su_daemon.cpp
)

# 复用 daemon 的源文件作为测试对象
target_sources(nexusd_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/log.cpp
    ${CMAKE_SOURCE_DIR}/src/types.cpp
    ${CMAKE_SOURCE_DIR}/src/util.cpp
    ${CMAKE_SOURCE_DIR}/src/env_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/selinux_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/module_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/script_executor.cpp
    ${CMAKE_SOURCE_DIR}/src/event_bus.cpp
    ${CMAKE_SOURCE_DIR}/src/fs/i_file_system_interceptor.cpp
    ${CMAKE_SOURCE_DIR}/src/fs/overlayfs_interceptor.cpp
    ${CMAKE_SOURCE_DIR}/src/fs/bind_mount_interceptor.cpp
    ${CMAKE_SOURCE_DIR}/src/fs/noop_interceptor.cpp
    ${CMAKE_SOURCE_DIR}/src/fs/fs_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/codec.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/credential_check.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/manager_uid_resolver.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/ipc_server.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/handlers.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/nexuscli_lib.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon_core.cpp
    ${CMAKE_SOURCE_DIR}/src/boot/boot_patcher.cpp
    ${CMAKE_SOURCE_DIR}/src/boot/nexus_rc.cpp
    ${CMAKE_SOURCE_DIR}/src/su_daemon.cpp
    ${CMAKE_SOURCE_DIR}/src/magisk_compat.cpp
)

target_include_directories(nexusd_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

find_package(Threads REQUIRED)
target_link_libraries(nexusd_tests PRIVATE Threads::Threads)

# 测试时禁用 Android 特定代码（log.cpp 用 __android_log_print，host 测试时 stub）
target_compile_definitions(nexusd_tests PRIVATE NEXUS_TEST_HOST=1)

# Phase 7: boot_patcher 用 SHA1，需要链接 OpenSSL
find_package(OpenSSL REQUIRED)
target_link_libraries(nexusd_tests PRIVATE OpenSSL::Crypto)

add_test(NAME nexusd_tests COMMAND nexusd_tests)
