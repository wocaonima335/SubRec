cmake_minimum_required(VERSION 3.21)
project(capture_media LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/SubRecFFmpeg.cmake")
subrec_require_ffmpeg_root()
foreach(component IN ITEMS avutil swscale swresample)
    if(NOT EXISTS "${FFMPEG_ROOT}/include/lib${component}" OR
       NOT EXISTS "${FFMPEG_ROOT}/lib/${component}.lib")
        message(FATAL_ERROR "Missing FFmpeg ${component} under ${FFMPEG_ROOT} (FFMPEG_ROOT must be a shared FFmpeg build with include/ and lib/)")
    endif()
    add_library(media_ffmpeg_${component} UNKNOWN IMPORTED)
    set_target_properties(media_ffmpeg_${component} PROPERTIES
        IMPORTED_LOCATION "${FFMPEG_ROOT}/lib/${component}.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_ROOT}/include")
endforeach()

add_library(capture_media_conversion STATIC media_conversion.cpp media_conversion.h)
target_compile_features(capture_media_conversion PUBLIC cxx_std_17)
set_target_properties(capture_media_conversion PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(capture_media_conversion PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../..")
target_link_libraries(capture_media_conversion PUBLIC
    media_ffmpeg_swscale media_ffmpeg_swresample media_ffmpeg_avutil)
if(MINGW)
    target_compile_options(capture_media_conversion PRIVATE -Wall -Wextra -Wpedantic)
endif()
