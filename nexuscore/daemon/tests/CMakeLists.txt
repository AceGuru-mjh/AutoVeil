add_executable(nexusd_tests
    test_codec.cpp
    test_util.cpp
)

target_include_directories(nexusd_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

find_package(Threads REQUIRED)
target_link_libraries(nexusd_tests PRIVATE Threads::Threads)

# 简化：使用 assert 而非 GoogleTest，避免外部依赖
add_test(NAME nexusd_tests COMMAND nexusd_tests)
