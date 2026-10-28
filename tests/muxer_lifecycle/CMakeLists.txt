cmake_minimum_required(VERSION 3.21)
project(muxer_lifecycle_tests LANGUAGES CXX)
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Core)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/SubRecFFmpeg.cmake")
subrec_require_ffmpeg_root()
add_executable(muxer_lifecycle_tests muxer_lifecycle_tests.cpp ../../muxer/ffmuxer.cpp)
target_compile_features(muxer_lifecycle_tests PRIVATE cxx_std_17)
target_include_directories(muxer_lifecycle_tests PRIVATE ../.. "${FFMPEG_ROOT}/include")
target_link_libraries(muxer_lifecycle_tests PRIVATE Qt6::Core)
foreach(component IN ITEMS avformat avcodec avutil)
    target_link_libraries(muxer_lifecycle_tests PRIVATE "${FFMPEG_ROOT}/lib/${component}.lib")
endforeach()
if(MINGW)
    target_compile_options(muxer_lifecycle_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME muxer_lifecycle COMMAND muxer_lifecycle_tests)
set_tests_properties(muxer_lifecycle PROPERTIES TIMEOUT 20)
# Runtime DLLs are resolved using PATH; no install, deployment, or artifact deletion.

add_executable(aac_priming_tests aac_priming_tests.cpp ../../muxer/ffmuxer.cpp)
target_compile_features(aac_priming_tests PRIVATE cxx_std_17)
target_include_directories(aac_priming_tests PRIVATE ../.. "${FFMPEG_ROOT}/include")
target_link_libraries(aac_priming_tests PRIVATE Qt6::Core)
foreach(component IN ITEMS avformat avcodec avutil)
    target_link_libraries(aac_priming_tests PRIVATE "${FFMPEG_ROOT}/lib/${component}.lib")
endforeach()
if(MINGW)
    target_compile_options(aac_priming_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME aac_priming COMMAND aac_priming_tests)
set_tests_properties(aac_priming PROPERTIES TIMEOUT 30)

find_program(SUBREC_POWERSHELL NAMES pwsh powershell)
if(SUBREC_POWERSHELL)
    add_test(NAME mp4_edit_list_parser COMMAND "${SUBREC_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/mp4_edit_list_tests.ps1"
        -PrimingTest "$<TARGET_FILE:aac_priming_tests>")
    set_tests_properties(mp4_edit_list_parser PROPERTIES TIMEOUT 60)
endif()

# Run the actual AAC encoder, bounded packet queues and mux worker; only the
# unused recorder/UI hooks are replaced in this standalone Core-only target.
add_executable(aac_pipeline_priming_tests aac_pipeline_priming_tests.cpp
    ../../encoder/ffaencoder.cpp ../../queue/ffapacketqueue.cpp ../../queue/ffaframequeue.cpp
    ../../queue/ffvpacketqueue.cpp ../../thread/ffthread.cpp
    ../../thread/ffmuxerthread.cpp ../../muxer/ffmuxer.cpp)
target_compile_features(aac_pipeline_priming_tests PRIVATE cxx_std_17)
target_include_directories(aac_pipeline_priming_tests PRIVATE
    ui_stubs ../.. "${FFMPEG_ROOT}/include")
target_link_libraries(aac_pipeline_priming_tests PRIVATE Qt6::Core)
foreach(component IN ITEMS avformat avcodec avutil)
    target_link_libraries(aac_pipeline_priming_tests PRIVATE "${FFMPEG_ROOT}/lib/${component}.lib")
endforeach()
if(MINGW)
    target_compile_options(aac_pipeline_priming_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME aac_pipeline_priming COMMAND aac_pipeline_priming_tests)
set_tests_properties(aac_pipeline_priming PROPERTIES TIMEOUT 30)

add_executable(stop_deadline_tests stop_deadline_tests.cpp
    ../../thread/ffthread.cpp ../../queue/ffapacketqueue.cpp)
target_compile_features(stop_deadline_tests PRIVATE cxx_std_17)
target_include_directories(stop_deadline_tests PRIVATE ../.. "${FFMPEG_ROOT}/include")
target_link_libraries(stop_deadline_tests PRIVATE Qt6::Core
    "${FFMPEG_ROOT}/lib/avcodec.lib" "${FFMPEG_ROOT}/lib/avutil.lib")
add_test(NAME stop_deadline COMMAND stop_deadline_tests)
set_tests_properties(stop_deadline PROPERTIES TIMEOUT 10)

add_executable(capture_diagnostics_tests capture_diagnostics_tests.cpp
    ../../queue/ffapacketqueue.cpp ../../queue/ffvpacketqueue.cpp ../../queue/ffaframequeue.cpp
    ../../thread/ffthread.cpp ../../thread/ffmuxerthread.cpp ../../muxer/ffmuxer.cpp)
target_compile_features(capture_diagnostics_tests PRIVATE cxx_std_17)
target_include_directories(capture_diagnostics_tests PRIVATE ui_stubs ../.. "${FFMPEG_ROOT}/include")
target_link_libraries(capture_diagnostics_tests PRIVATE Qt6::Core
    "${FFMPEG_ROOT}/lib/avformat.lib" "${FFMPEG_ROOT}/lib/avcodec.lib" "${FFMPEG_ROOT}/lib/avutil.lib")
add_test(NAME capture_diagnostics COMMAND capture_diagnostics_tests)
set_tests_properties(capture_diagnostics PROPERTIES TIMEOUT 10)
