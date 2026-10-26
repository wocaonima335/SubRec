qt_add_library(recorder_module STATIC
    ffrecorder.h
    ffrecorder_p.h
    ffrecoder_p.cpp
    ffrecoder.cpp
    capture_scheduler.h
    capture_scheduler.cpp
    capture_session.h
    capture_session.cpp
    ffaudiosampler.h
    ffaudiosampler.cpp
)
target_link_libraries(recorder_module PRIVATE
    PRIVATE 
        Qt6::Core
        Qt6::Widgets
        Qt6::Gui
        capture_module
        ${FFMPEG_LIBRARIES}
        queue_module
        muxer_module
        demuxer_module
        encoder_module
        decoder_module
        filter_module
        thread_module
)

target_include_directories(recorder_module PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..  # 添加项目根目录，使得可以找到queue/ffapacketqueue.h
)
