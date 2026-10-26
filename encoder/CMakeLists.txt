qt_add_library(encoder_module STATIC
    ffaencoder.cpp
    ffaencoder.h
    ffvencoder.h
    ffvencoder.cpp
)

target_link_libraries(encoder_module
    PRIVATE
    Qt6::Core
    ${FFMPEG_LIBRARIES}
    queue_module)

target_include_directories(encoder_module PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..  # 添加项目根目录，使得可以找到queue/ffapacketqueue.h
)
