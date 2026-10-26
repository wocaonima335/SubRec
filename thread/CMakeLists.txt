qt_add_library(thread_module STATIC
    ffthread.cpp
    ffthread.h
    ffdemuxerthread.h
    ffdemuxerthread.cpp
    ffadecoderthread.h
    ffadecoderthread.cpp
    ffvdecoderthread.h
    ffvdecoderthread.cpp
    ffvfilterthread.h
    ffvfilterthread.cpp
    ffafilterthread.h
    ffafilterthread.cpp
    ffaencoderthread.h
    ffaencoderthread.cpp
    ffvencoderthread.h
    ffvencoderthread.cpp
    ffmuxerthread.h
    ffmuxerthread.cpp
    ffscreengrabthread.h
    ffscreengrabthread.cpp
)

target_link_libraries(thread_module
    PRIVATE
    Qt::Core
    ${FFMPEG_LIBRARIES}
    queue_module
    opengl_module
    event_module
    decoder_module
    capture_module)

target_include_directories(thread_module PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..  # 添加项目根目录，使得可以找到queue/ffapacketqueue.h
)

