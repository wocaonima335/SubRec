qt_add_library(filter_module STATIC
    ffvfilter.cpp
    ffvfilter.h
    ffafilter.h
    ffafilter.cpp
    audio_mix_dump.cpp
    audio_mix_dump.h
    audio_timeline_mixer.cpp
    audio_timeline_mixer.h
    # FFAudioSampler (output level metering) is compiled here, not in recorder_module:
    # linking recorder_module would pull filter_module into the static-library cycle and
    # CMake's repeated cycle order then leaves thread_module -> AudioTimelineMixer unresolved.
    ../recorder/ffaudiosampler.cpp
    ../recorder/ffaudiosampler.h
)

target_link_libraries(filter_module
    PRIVATE
    Qt6::Core
    ${FFMPEG_LIBRARIES}
    queue_module
)

# 添加FFmpeg头文件目录
target_include_directories(filter_module PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..  # 添加项目根目录，使得可以找到queue/ffapacketqueue.h
)
