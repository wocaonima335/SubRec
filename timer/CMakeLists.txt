qt_add_library(timer_module STATIC
    fftimer.h
    fftimer.cpp

  
)
target_link_libraries(timer_module PRIVATE
    PRIVATE 
        Qt6::Core
        ${FFMPEG_LIBRARIES}
        queue_module
)

# 添加FFmpeg头文件目录
target_include_directories(timer_module PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..  # 添加项目根目录，使得可以找到queue/ffapacketqueue.h
)
