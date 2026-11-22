# tests/asr_host（工作包 C）。两种进入方式：
#   1) 独立：cmake -S tools/asr_host -B build-asr（SUBREC_ASR_BUILD_TESTS 默认 ON）
#   2) 仓库内：tools/asr_host -DSUBREC_ASR_BUILD_TESTS=ON（默认 OFF，避免与 G 的 tests/ 挂接重复）
# 纯 C++17 测试框架（无外部依赖）；崩溃/挂起类用例逐个设 TIMEOUT，ctest 不会卡死。

add_executable(asr_host_tests
    test_framework.h
    test_main.cpp
    child_process.h
    child_process.cpp
    wav_builder.h
    host_driver.h
    "${SUBREC_ASR_REPO_ROOT}/tools/asr_host/fake_host/fake_host_script.cpp"
    test_protocol.cpp
    test_options.cpp
    test_fake_engine.cpp
    test_fake_host.cpp
    test_host_live.cpp
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
)

foreach(case IN LISTS ASR_HOST_TEST_CASES)
    add_test(NAME ${case} COMMAND asr_host_tests ${case})
    set_tests_properties(${case} PROPERTIES TIMEOUT 60)
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
    PROPERTIES TIMEOUT 90)
