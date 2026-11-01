add_library(shell_module STATIC
    shell_types.h shell_logic.h shell_logic.cpp
    shell_settings.h shell_settings.cpp
    shell_state_source.h shell_state_source.cpp
    tray_controller.h tray_controller.cpp
    tray_icon_factory.h tray_icon_factory.cpp
)
set_target_properties(shell_module PROPERTIES AUTOMOC ON)
target_compile_features(shell_module PUBLIC cxx_std_17)
target_include_directories(shell_module PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(shell_module PRIVATE ${FFMPEG_INCLUDE_DIR} "${FFMPEG_ROOT}/include")
target_link_libraries(shell_module PUBLIC Qt6::Widgets Qt6::Network Qt6::Quick)
if(WIN32)
    target_link_libraries(shell_module PRIVATE user32)
endif()

# S0b/S4-S7 belong to ZCode. Before that merge the independent shell pieces
# still build; integration of WindowController also requires the S7 preview API.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/capture_exclusion.h"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/power_request.h")
    target_sources(shell_module PRIVATE window_controller.h window_controller.cpp)
else()
    message(STATUS "shell_module: WindowController awaits S0b headers and S7 preview API")
endif()
foreach(component IN ITEMS capture_exclusion power_request single_instance)
    foreach(extension IN ITEMS h cpp)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${component}.${extension}")
            target_sources(shell_module PRIVATE "${component}.${extension}")
        endif()
    endforeach()
endforeach()
