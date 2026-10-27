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
    target_sources(capture_module PRIVATE wasapi_loopback_capture.cpp wasapi_loopback_capture.h
        wasapi_capture_backend.cpp wasapi_capture_backend.h media/media_conversion.cpp)
    # MinGW's ksmedia.h declares format GUIDs even with INITGUID; ksuser defines
    # KSDATAFORMAT_SUBTYPE_PCM/IEEE_FLOAT. STATIC library PRIVATE link dependencies
    # propagate to every consumer (including audio_filter_dual_tests) via LINK_ONLY.
    target_link_libraries(capture_module PRIVATE ole32 uuid propsys ksuser avrt)
    # DXGI output enumeration for the ddagrab screen devices.
    target_link_libraries(capture_module PRIVATE dxgi)
endif()

target_include_directories(capture_module PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
