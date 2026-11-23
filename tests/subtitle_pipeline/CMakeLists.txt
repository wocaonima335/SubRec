cmake_minimum_required(VERSION 3.21)
# The production queue includes corecrt.h. Build through the application test
# configuration on Windows; no portability shim or app/GL workaround is added.
find_package(Qt6 REQUIRED COMPONENTS Core Test)
find_package(Threads REQUIRED)
add_executable(subtitle_pipeline_tests pipeline_tests.cpp
    fakes/recorder/ffrecorder.h fake_muxer.cpp
    ../../thread/ffaencoderthread.cpp ../../thread/ffthread.cpp
    ../../encoder/ffaencoder.cpp ../../queue/recordersyncqueue.cpp
    ../../queue/ffaframequeue.cpp ../../queue/ffapacketqueue.cpp ../../queue/packet_guard.cpp)
set_target_properties(subtitle_pipeline_tests PROPERTIES AUTOMOC ON)
target_compile_features(subtitle_pipeline_tests PRIVATE cxx_std_17)
# fakes MUST precede the repo root propagated by subtitle_headers.
target_include_directories(subtitle_pipeline_tests BEFORE PRIVATE fakes ../.. "${FFMPEG_ROOT}/include")
target_compile_definitions(subtitle_pipeline_tests PRIVATE SUBREC_HAS_SUBTITLE=1
    ASR_EXE="$<TARGET_FILE:subrec-asr>" FAKEHOST_EXE="$<TARGET_FILE:subrec-asr-fakehost>")
target_link_libraries(subtitle_pipeline_tests PRIVATE subtitle_audio subtitle_host subtitle_live Qt6::Test Threads::Threads)
foreach(component IN ITEMS avcodec avutil)
    find_library(pipeline_${component} NAMES ${component} PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(subtitle_pipeline_tests PRIVATE "${pipeline_${component}}")
endforeach()
add_dependencies(subtitle_pipeline_tests subrec-asr subrec-asr-fakehost)
foreach(case IN ITEMS cue_timing_splices pause_final_latency hung_host_throughput tap_exit_paths)
    add_test(NAME subtitle_pipeline_${case} COMMAND subtitle_pipeline_tests ${case})
    set_tests_properties(subtitle_pipeline_${case} PROPERTIES TIMEOUT 30 LABELS "subtitle;pipeline;G")
endforeach()
set_tests_properties(subtitle_pipeline_hung_host_throughput PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
# Registered for PC3 only. CTest's ci label must never include real capture/model work.
add_test(NAME record_seconds_subtitle_live COMMAND "$<TARGET_FILE:appSubRec>")
set_tests_properties(record_seconds_subtitle_live PROPERTIES TIMEOUT 90
    LABELS "subtitle;device;needs_asr_model"
    ENVIRONMENT "SUBREC_AUTOTEST_SCENARIO=record_seconds_subtitle_live;SUBREC_AUTOTEST_RECORD_MS=15000")
# Public hook lifecycle against the production scheduler/session using its existing
# fake-opener friend seam. state() reentry verifies callbacks are outside m_mutex.
add_executable(subtitle_scheduler_hook_tests scheduler_hooks.cpp)
set_target_properties(subtitle_scheduler_hook_tests PROPERTIES AUTOMOC ON)
target_compile_features(subtitle_scheduler_hook_tests PRIVATE cxx_std_17)
target_include_directories(subtitle_scheduler_hook_tests PRIVATE ../.. "${FFMPEG_ROOT}/include")
target_link_libraries(subtitle_scheduler_hook_tests PRIVATE
    recorder_module capture_module queue_module muxer_module demuxer_module decoder_module
    resampler_module filter_module thread_module event_module encoder_module
    subtitle_tap Qt6::Test Qt6::Gui Qt6::Widgets ${FFMPEG_LIBRARIES})
add_test(NAME subtitle_pipeline_scheduler_hooks COMMAND subtitle_scheduler_hook_tests)
set_tests_properties(subtitle_pipeline_scheduler_hooks PROPERTIES TIMEOUT 20 LABELS "subtitle;pipeline;G")

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/SubRecTests.cmake")
subrec_configure_tests()
