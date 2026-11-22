# 工作包 A 独占。实现 AudioTap::push 时在此链接 avutil（只需头文件与 av_get_bytes_per_sample 等）。
add_library(subtitle_tap STATIC
    audio_tap.h
    audio_tap.cpp
    recording_hook.h
)
target_link_libraries(subtitle_tap PUBLIC subtitle_headers)
