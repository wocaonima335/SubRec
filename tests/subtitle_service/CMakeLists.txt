find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(subtitle_d_service_tests service_tests.cpp)
set_target_properties(subtitle_d_service_tests PROPERTIES AUTOMOC ON)
target_link_libraries(subtitle_d_service_tests PRIVATE subtitle_service Qt6::Test)
target_compile_definitions(subtitle_d_service_tests PRIVATE
    ASR_EXE="$<TARGET_FILE:subrec-asr>" FAKEHOST_EXE="$<TARGET_FILE:subrec-asr-fakehost>"
    SUBREC_TAP_HAS_FFMPEG=$<BOOL:${SUBREC_TAP_HAS_FFMPEG}>)
if(SUBREC_TAP_HAS_FFMPEG)
    target_include_directories(subtitle_d_service_tests PRIVATE "${SUBREC_TAP_FFMPEG_INCLUDE}")
endif()
add_dependencies(subtitle_d_service_tests subrec-asr subrec-asr-fakehost)
set(cases realtime_finalize rtf_l1 rtf_l2 crash_recovery hang_recovery poison_segment oom_l1_l3
    illegal_instruction protocol_exit_191 end_false shutdown_incomplete current_split live_config restore_live
    fifo_preemption service_end_false service_shutdown service_throttle)
list(APPEND cases service_health_l3 service_engine_request_provider)
if(SUBREC_TAP_HAS_FFMPEG)
    list(APPEND cases tap_realtime tap_rtf_l1 tap_rtf_l2 tap_crash tap_hang tap_poison tap_oom tap_illegal)
endif()
foreach(case IN LISTS cases)
    add_test(NAME subtitle_d_service_${case} COMMAND subtitle_d_service_tests ${case})
    set_tests_properties(subtitle_d_service_${case} PROPERTIES TIMEOUT 30 LABELS "subtitle;service;D")
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/SubRecTests.cmake")
subrec_configure_tests()
