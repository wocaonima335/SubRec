# tests/asr_host（工作包 C）。两种进入方式：
#   1) 独立：cmake -S tools/asr_host -B build-asr（SUBREC_ASR_BUILD_TESTS 默认 ON）
#   2) 仓库内：顶层 -DSUBREC_BUILD_SUBTITLE=ON 且测试开关开启时自动构建（tools/asr_host 缺省跟随
#      SUBREC_BUILD_LIFECYCLE_TESTS / BUILD_TESTING），本目录末尾按 SubRecTests 规则登记：
#      ci 标签 + 目标进 SUBREC_TEST_TARGETS（顶层 WIN32 块汇成 subrec_tests 依赖）。
# 纯 C++17 测试框架（无外部依赖）；崩溃/挂起类用例逐个设 TIMEOUT，ctest 不会卡死。

add_executable(asr_host_tests
    test_framework.h
    test_main.cpp
    child_process.h
    child_process.cpp
    wav_builder.h
    host_driver.h
    test_paths.h
    "${SUBREC_ASR_REPO_ROOT}/tools/asr_host/fake_host/fake_host_script.cpp"
    test_protocol.cpp
    test_options.cpp
    test_fake_engine.cpp
    test_fake_host.cpp
    test_host_live.cpp
    test_utf8_paths.cpp
)
target_include_directories(asr_host_tests PRIVATE
    "${SUBREC_ASR_REPO_ROOT}"
    "${SUBREC_ASR_REPO_ROOT}/tools/asr_host"
    "${SUBREC_ASR_REPO_ROOT}/tools/asr_host/src")
target_link_libraries(asr_host_tests PRIVATE subrec_asr_core)
target_compile_definitions(asr_host_tests PRIVATE
    SUBREC_ASR_EXE="$<TARGET_FILE:subrec-asr>"
    SUBREC_ASR_FAKEHOST_EXE="$<TARGET_FILE:subrec-asr-fakehost>")
add_dependencies(asr_host_tests subrec-asr subrec-asr-fakehost)

# 每个用例一个 ctest 条目（超时：崩溃/挂起类必须设置，其余 60 s 兜底）
set(ASR_HOST_TEST_CASES
    proto_roundtrip_all_messages
    proto_roundtrip_load_engine_request
    proto_roundtrip_partial_tokens
    proto_roundtrip_final_words_prob
    proto_decode_line_too_long
    proto_decode_not_json
    proto_decode_unknown_type
    proto_decode_missing_required_fields
    proto_decode_bad_field_types
    proto_decode_optional_defaults
    proto_decode_unknown_fields_ignored
    proto_decode_unicode_escapes
    proto_accepted_by_role
    opts_parse_host_arguments
    opts_fake_parse_all_keys
    opts_fake_parse_errors
    opts_fake_format_roundtrip
    opts_fake_engine_requires_hooks
    engine_final_text_and_words
    engine_lang_en_mix
    engine_partial_prefix_grows
    engine_partial_noise
    engine_partial_regress
    engine_final_differs
    engine_load_fail
    engine_refine_empty
    engine_rtf_and_refine_rtf_sleep
    engine_seed_determinism
    fakehost_script_parse_errors
    fakehost_handshake_and_loaded
    fakehost_reply_hello_custom_protocol
    fakehost_expect_skips_other_types
    fakehost_hb_stream_and_off
    fakehost_garbage_and_longline
    fakehost_crash
    fakehost_eof_exits_zero
    fakehost_quit_after_script_end
    fakehost_hang_stops_output
    live_endpoint_final_and_done
    live_partial_stream_and_seq
    live_splice_forces_cut
    live_max_segment_force_cut
    live_resume_from_16k
    live_resume_continues_seg_ids
    fault_injection_counts_per_open
    live_set_level_disables_partial
    live_garbage_stdout_keeps_protocol_clean
    fault_crash_at_seg
    fault_oom_at_seg
    fault_illegal_instr_exit
    fault_hang_at_seg_keeps_hb
    fault_hang_all_stops_hb
    fault_load_fail_error_and_stay_alive
    host_unknown_type_ignored
    host_bad_json_exits_703
    host_too_long_line_exits_703
    host_hello_version_mismatch_exits_704
    host_open_before_load_not_ready
    host_eof_exits_zero
    host_refine_role_replay
    host_refine_role_rejects_live_commands
    utf8_path_chinese_spool_and_script
)

foreach(case IN LISTS ASR_HOST_TEST_CASES)
    add_test(NAME ${case} COMMAND asr_host_tests ${case})
    set_tests_properties(${case} PROPERTIES TIMEOUT 60)
endforeach()

# 需要真实模型文件的用例（当前没有；接入真实引擎后填用例名列表即可，机制先留好）。
# 这类用例同时打 device 标签：subrec_configure_tests 不给带 device 的用例追加 ci，
# 因此 CI 的 ctest -L "^ci$" 不会选中它们，-LE "^(device|...)$" 一并排除（不能改 .github，
# 只能复用 device 这条既有排除通道；needs_asr_model 供本机 ctest -L needs_asr_model 定向运行）。
set(ASR_HOST_NEEDS_MODEL_CASES "")
foreach(case IN LISTS ASR_HOST_NEEDS_MODEL_CASES)
    set_property(TEST ${case} APPEND PROPERTY LABELS "device;needs_asr_model")
endforeach()

# 崩溃/挂起/慢速注入类：收紧超时，防止 ctest 卡死
set_tests_properties(
    fault_crash_at_seg
    fault_oom_at_seg
    fault_illegal_instr_exit
    fault_hang_at_seg_keeps_hb
    fault_hang_all_stops_hb
    fakehost_crash
    fakehost_hang_stops_output
    fakehost_hb_stream_and_off
    PROPERTIES TIMEOUT 30)

set_tests_properties(
    live_partial_stream_and_seq
    live_set_level_disables_partial
    utf8_path_chinese_spool_and_script
    PROPERTIES TIMEOUT 90)

# 按 SubRecTests 规则登记（与其他 tests/* 一致，两种进入方式都执行）：未带 device 标签的用例
# 追加 ci 标签、WIN32 上设置 PATH（编译器 bin，MinGW 运行时 DLL）、把 asr_host_tests 追加到全局
# SUBREC_TEST_TARGETS（顶层 WIN32 块 add_custom_target(subrec_tests DEPENDS ...) 收集，
# CI 的 build/ci-tests 用 --target subrec_tests 构建时随之带上 subrec-asr / subrec-asr-fakehost）。
include("${SUBREC_ASR_REPO_ROOT}/cmake/SubRecTests.cmake")
subrec_configure_tests()
