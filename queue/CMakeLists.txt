qt_add_library(queue_module STATIC
    recordersyncqueue.h
    recordersyncqueue.cpp
    ffvpacketqueue.h
    ffapacketqueue.h
    ffvpacketqueue.cpp
    ffvframequeue.h
    ffaframequeue.h
    ffpacket.h
    ffapacketqueue.cpp
    ffaframequeue.cpp
    ffvframequeue.cpp
    ffboundedqueue.h
)

target_link_libraries(queue_module 
PRIVATE
    Qt6::Core
     ${FFMPEG_LIBRARIES}
)

# 添加FFmpeg头文件目录
target_include_directories(queue_module PRIVATE ${FFMPEG_INCLUDE_DIR}
 ${CMAKE_CURRENT_SOURCE_DIR}/.. )
