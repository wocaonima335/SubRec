qt_add_library(decoder_module STATIC
    ffvdecoder.h
    ffvdecoder.cpp
    ffadecoder.h
    ffadecoder.cpp )

target_link_libraries(decoder_module
    PRIVATE
    Qt6::Core
    ${FFMPEG_LIBRARIES}
    queue_module
    resampler_module
    recorder_module)


# 添加FFmpeg头文件目录
target_include_directories(decoder_module PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..  # 添加项目根目录，使得可以找到queue/ffapacketqueue.h
)
