qt_add_library(resampler_module STATIC
    ffaresampler.h
    ffaresampler.cpp
    ffvresampler.h
    ffvresampler.cpp)

target_link_libraries(resampler_module
    PRIVATE
    Qt::Core
    ${FFMPEG_LIBRARIES}
    queue_module
)

target_include_directories(resampler_module PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..  # 添加项目根目录，使得可以找到queue/ffapacketqueue.h
)
