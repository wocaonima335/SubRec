# Demuxer模块
qt_add_library(demuxer_module STATIC
    demuxer.h
    demuxer.cpp)



target_link_libraries(demuxer_module
    PRIVATE 
        Qt6::Core
        Qt6::Gui
        ${FFMPEG_LIBRARIES}
        queue_module
)

# 添加FFmpeg头文件目录
target_include_directories(demuxer_module PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..  # 添加项目根目录，使得可以找到queue/ffapacketqueue.h
)


# 如果需要，可以添加其他依赖
# find_package(Qt6 REQUIRED COMPONENTS Multimedia)
# target_link_libraries(demuxer_module PRIVATE Qt6::Multimedia)
