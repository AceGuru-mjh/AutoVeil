# Daemon 单元测试
#
# 每个测试文件编译为单独的可执行文件（避免 main 冲突）。
# 仅在 host 平台编译（父 CMakeLists.txt 中 if(NOT ANDROID) 启用）。

# 共享的 daemon 源文件（用于测试对象）
set(NEXUSD_TEST_SOURCES
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

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# 每个测试文件编译为独立的可执行
set(NEXUSD_TESTS
    test_codec
    test_util
    test_module_loader
    test_event_bus
    test_script_executor
    test_boot_patcher
    test_magisk_compat
    test_su_daemon
)

foreach(test ${NEXUSD_TESTS})
    add_executable(${test} ${test}.cpp ${NEXUSD_TEST_SOURCES})
    target_include_directories(${test} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/third_party
    )
    target_link_libraries(${test} PRIVATE Threads::Threads OpenSSL::Crypto)
    target_compile_definitions(${test} PRIVATE NEXUS_TEST_HOST=1)
    add_test(NAME ${test} COMMAND ${test})
endforeach()
