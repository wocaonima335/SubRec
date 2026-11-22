# tests/subtitle_models（工作包 E，docs/subtitle/work-packages.md §3 表 E）。
# Qt Test + 本地 QTcpServer HTTP 服务器（test_http_server.h），只监听 127.0.0.1，绝不联网。
# 两种进入方式（由 subtitle/models/CMakeLists.txt 条件 add_subdirectory）：
#   1) subtitle 独立构建：SUBREC_SUBTITLE_STANDALONE=ON，ctest 直接可跑；
#   2) 仓库内顶层构建：SUBREC_BUILD_LIFECYCLE_TESTS / BUILD_TESTING 为真（CI build/ci-tests）。
# 末尾按 SubRecTests 规则登记：ci 标签、Windows PATH、目标进 SUBREC_TEST_TARGETS（汇入 subrec_tests）。

if(NOT TARGET Qt6::Test)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()
if(NOT TARGET Qt6::Network)
    find_package(Qt6 REQUIRED COMPONENTS Network)
endif()

add_executable(subtitle_models_tests
    subtitle_models_tests.h
    test_support.h
    test_http_server.h
    test_main.cpp
    tests_manifest.cpp
    tests_reflection.cpp
    tests_download.cpp
    tests_license.cpp
    tests_remove_stale.cpp)
set_target_properties(subtitle_models_tests PROPERTIES AUTOMOC ON)
target_link_libraries(subtitle_models_tests PRIVATE subtitle_models Qt6::Test Qt6::Network)

# 每个用例一个 ctest 条目（Qt Test 支持按函数名运行），全部设 TIMEOUT 防卡死。
set(SUBTITLE_MODELS_TEST_CASES
    manifest_builtin_selfcheck
    reflection_contract
    models_keys_match_catalog_roles
    range_resume_from_part
    disconnect_switches_mirror
    hash_mismatch_switches_mirror
    hash_mismatch_all_fail
    mirror_404_then_success
    mirror_refused_then_success
    server_ignores_range_restarts
    size_mismatch_rejected
    http_scheme_rejected_without_loopback
    multi_file_download
    cancel_keeps_part
    license_gate_blocks_and_accepts
    license_version_change_requires_reaccept
    license_persists_across_instances
    remove_installed
    remove_while_downloading
    stale_detection
    engine_request_installed
    engine_request_missing_or_stale)

foreach(case IN LISTS SUBTITLE_MODELS_TEST_CASES)
    add_test(NAME ${case} COMMAND subtitle_models_tests ${case})
    set_tests_properties(${case} PROPERTIES TIMEOUT 60)
endforeach()

# 涉及连接被拒/挂起服务器（Stall）的用例放宽到 90 s。
set_tests_properties(
    mirror_refused_then_success
    cancel_keeps_part
    remove_while_downloading
    PROPERTIES TIMEOUT 90)

# 按 SubRecTests 规则登记（与其他 tests/* 一致）：未带 device 标签的用例追加 ci 标签、
# WIN32 上设置 PATH（Qt/编译器 DLL），目标追加进全局 SUBREC_TEST_TARGETS
#（顶层 WIN32 块 add_custom_target(subrec_tests DEPENDS ...) 收集）。
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/SubRecTests.cmake")
subrec_configure_tests()
