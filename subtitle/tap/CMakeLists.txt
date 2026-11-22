# 工作包 A 独占。实现 AudioTap::push 时在此链接 avutil（只需头文件与 av_get_bytes_per_sample 等）。
add_library(subtitle_tap STATIC
    audio_tap.h
    audio_tap.cpp
    recording_hook.h
)
target_link_libraries(subtitle_tap PUBLIC subtitle_headers)

# Package A only inspects AVFrame fields; no avutil symbols or link dependency.
set(SUBREC_TAP_FFMPEG_INCLUDE "")
if(NOT SUBREC_SUBTITLE_STANDALONE)
    set(SUBREC_TAP_FFMPEG_INCLUDE "${FFMPEG_INCLUDE_DIR}")
else()
    if(NOT FFMPEG_ROOT AND DEFINED ENV{SUBREC_FFMPEG_ROOT})
        set(FFMPEG_ROOT "$ENV{SUBREC_FFMPEG_ROOT}")
    endif()
    if(FFMPEG_ROOT AND EXISTS "${FFMPEG_ROOT}/include/libavutil/frame.h")
        set(SUBREC_TAP_FFMPEG_INCLUDE "${FFMPEG_ROOT}/include")
    endif()
endif()
if(SUBREC_TAP_FFMPEG_INCLUDE)
    target_include_directories(subtitle_tap PRIVATE "${SUBREC_TAP_FFMPEG_INCLUDE}")
    target_compile_definitions(subtitle_tap PRIVATE SUBREC_TAP_HAS_FFMPEG=1)
    set(SUBREC_TAP_HAS_FFMPEG ON PARENT_SCOPE)
else()
    target_compile_definitions(subtitle_tap PRIVATE SUBREC_TAP_HAS_FFMPEG=0)
    set(SUBREC_TAP_HAS_FFMPEG OFF PARENT_SCOPE)
    message(STATUS "subtitle: no FFmpeg headers; tap push counts formatRejects; AVFrame tests omitted")
endif()
set(SUBREC_TAP_FFMPEG_INCLUDE "${SUBREC_TAP_FFMPEG_INCLUDE}" PARENT_SCOPE)
