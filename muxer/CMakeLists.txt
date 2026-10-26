qt_add_library(muxer_module STATIC
    ffmuxer.h
    ffmuxer.cpp
  
)
target_link_libraries(muxer_module PRIVATE
    PRIVATE 
        Qt6::Core
        ${FFMPEG_LIBRARIES}
        queue_module
)

# 添加FFmpeg头文件目录
target_include_directories(muxer_module PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..  # 添加项目根目录，使得可以找到queue/ffapacketqueue.h
)
