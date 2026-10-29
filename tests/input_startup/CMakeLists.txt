cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(input_startup_tests LANGUAGES CXX)
    enable_testing()
endif()
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Multimedia Widgets)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${ROOT}/cmake/SubRecFFmpeg.cmake")
subrec_require_ffmpeg_root()
add_library(opener_compile_check OBJECT "${ROOT}/event/ffopensourceevent.cpp")
add_executable(input_startup_tests input_startup_tests.cpp
    "${ROOT}/demuxer/demuxer.cpp"
    "${ROOT}/thread/ffdemuxerthread.cpp"
    "${ROOT}/thread/ffthread.cpp"
    "${ROOT}/queue/ffapacketqueue.cpp"
    "${ROOT}/queue/ffvpacketqueue.cpp")
foreach(target IN ITEMS input_startup_tests opener_compile_check)
    target_include_directories(${target} PRIVATE "${ROOT}" "${FFMPEG_ROOT}/include")
    target_link_libraries(${target} PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Multimedia Qt6::Widgets)
endforeach()
foreach(lib IN ITEMS avformat avcodec avutil avdevice swresample swscale)
    find_library(FF_${lib} NAMES ${lib} PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(input_startup_tests PRIVATE "${FF_${lib}}")
endforeach()
if(WIN32)
    target_link_libraries(input_startup_tests PRIVATE ole32)
endif()
add_test(NAME input_startup COMMAND input_startup_tests)
set_tests_properties(input_startup PROPERTIES TIMEOUT 30)
# Run with Qt, FFmpeg and compiler runtime directories on PATH. No deployment.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/SubRecTests.cmake")
subrec_configure_tests()
# Uses lavfi sine/color plus WAV/Matroska demuxers; all inputs are synthetic.
set_property(TEST input_startup APPEND PROPERTY LABELS needs_ffmpeg_test_components)
