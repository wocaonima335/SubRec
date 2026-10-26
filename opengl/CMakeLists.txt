qt_add_library(opengl_module STATIC
  ffglitem.cpp
  ffglitem.h
  ffglrenderer.h
  ffglrenderer.cpp
)

target_link_libraries(opengl_module PUBLIC Qt6::Gui Qt6::Quick queue_module)

# 添加FFmpeg头文件目录
target_include_directories(opengl_module PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..  # 添加项目根目录，使得可以找到queue/ffapacketqueue.h
)

