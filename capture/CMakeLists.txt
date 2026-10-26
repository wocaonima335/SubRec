qt_add_library(capture_module STATIC
    capture_types.h
    capture_types.cpp
    capture_backend.h
    dshow_capture_backend.h
    dshow_capture_backend.cpp
)

target_link_libraries(capture_module
    PRIVATE
        Qt6::Core
        ${FFMPEG_LIBRARIES}
)

target_include_directories(capture_module PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
