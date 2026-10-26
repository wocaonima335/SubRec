qt_add_library(render_module STATIC
    ffarender.h
    ffarender.cpp )

target_link_libraries(render_module
PRIVATE
    Qt6::Core
    Qt6::Multimedia
    ${FFMPEG_LIBRARIES}
    decoder_module
)

# 添加FFmpeg头文件目录
target_include_directories(render_module PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
