cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(encoder_failure_tests LANGUAGES CXX)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Core)
    include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/SubRecFFmpeg.cmake")
    subrec_require_ffmpeg_root()
endif()
find_package(Threads REQUIRED)
# Compile the real worker loops and queues against link-time codec/filter/muxer
# fakes. No production seam, device, optional codec or test hook is required.
add_executable(encoder_failure_tests encoder_failure_tests.cpp
    fakes/recorder/ffrecorder.h
    ../../thread/ffvencoderthread.cpp ../../thread/ffaencoderthread.cpp
    ../../thread/ffthread.cpp ../../queue/ffvframequeue.cpp
    ../../queue/ffaframequeue.cpp ../../queue/recordersyncqueue.cpp)
set_target_properties(encoder_failure_tests PROPERTIES AUTOMOC ON)
target_compile_features(encoder_failure_tests PRIVATE cxx_std_17)
target_include_directories(encoder_failure_tests PRIVATE fakes ../.. "${FFMPEG_ROOT}/include")
target_link_libraries(encoder_failure_tests PRIVATE Qt6::Core Threads::Threads
    "${FFMPEG_ROOT}/lib/avutil.lib")
foreach(mode IN ITEMS video audio audio_sync)
    add_test(NAME encoder_failure_${mode} COMMAND encoder_failure_tests ${mode})
    set_tests_properties(encoder_failure_${mode} PROPERTIES TIMEOUT 10)
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/SubRecTests.cmake")
subrec_configure_tests()
