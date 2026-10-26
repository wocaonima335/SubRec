qt_add_library(capture_module STATIC
    capture_types.h
    capture_types.cpp
    capture_backend.h
    dshow_capture_backend.h
    dshow_capture_backend.cpp
    ddagrab_capture_backend.h
    ddagrab_capture_backend.cpp
    ddagrab_screen_source.h
    ddagrab_screen_source.cpp
)

target_link_libraries(capture_module
    PRIVATE
        Qt6::Core
        ${FFMPEG_LIBRARIES}
)
if(WIN32)
    # DXGI output enumeration for the ddagrab screen devices.
    target_link_libraries(capture_module PRIVATE dxgi)
endif()

target_include_directories(capture_module PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
