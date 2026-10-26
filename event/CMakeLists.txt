qt_add_library(event_module STATIC
    ffevent.h
    ffevent.cpp
    ffopensourceevent.h
    ffopensourceevent.cpp
    ffclosesourceevent.h
    ffclosesourceevent.cpp
    ffprocessevent.h
    ffprocessevent.cpp)

target_link_libraries(event_module PRIVATE
    PRIVATE 
        Qt6::Core
        ${FFMPEG_LIBRARIES}
        queue_module
        decoder_module
        demuxer_module
)
# 添加FFmpeg头文件目录
target_include_directories(event_module PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..  # 添加项目根目录，使得可以找到queue/ffapacketqueue.h
)
