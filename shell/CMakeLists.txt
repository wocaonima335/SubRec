add_library(shell_module STATIC
    shell_types.h shell_logic.h shell_logic.cpp
    shell_settings.h shell_settings.cpp
    shell_state_source.h shell_state_source.cpp
    tray_controller.h tray_controller.cpp
    tray_icon_factory.h tray_icon_factory.cpp
    window_controller.h window_controller.cpp
    capture_exclusion.h capture_exclusion.cpp
    power_request.h power_request.cpp
    single_instance.h single_instance.cpp single_instance_protocol.h
)
set_target_properties(shell_module PROPERTIES AUTOMOC ON)
target_compile_features(shell_module PUBLIC cxx_std_17)
target_include_directories(shell_module PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(shell_module PRIVATE ${FFMPEG_INCLUDE_DIR} "${FFMPEG_ROOT}/include")
target_link_libraries(shell_module PUBLIC Qt6::Widgets Qt6::Network Qt6::Quick)
if(WIN32)
    target_link_libraries(shell_module PRIVATE user32)
endif()
