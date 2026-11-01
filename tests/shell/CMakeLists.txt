cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(subrec_shell_tests LANGUAGES CXX)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Widgets Quick Network)
endif()

# Like the other in-tree harnesses, compile the production units directly.
# This keeps pure tests independent of the recording engine and pending S0b.
set(shell_root "${CMAKE_CURRENT_LIST_DIR}/../..")
add_library(shell_test_ui STATIC
    ${shell_root}/shell/shell_logic.cpp
    ${shell_root}/shell/shell_settings.cpp ${shell_root}/shell/shell_settings.h
    ${shell_root}/shell/tray_controller.cpp ${shell_root}/shell/tray_controller.h
    ${shell_root}/shell/tray_icon_factory.cpp)
set_target_properties(shell_test_ui PROPERTIES AUTOMOC ON)
target_compile_features(shell_test_ui PUBLIC cxx_std_17)
target_include_directories(shell_test_ui PUBLIC ${shell_root})
target_link_libraries(shell_test_ui PUBLIC Qt6::Widgets)
foreach(suite IN ITEMS shell_logic tray_menu_model shell_settings)
    add_executable(${suite}_tests ${suite}_tests.cpp)
    target_link_libraries(${suite}_tests PRIVATE shell_test_ui)
endforeach()

# Fakes are private to this executable. In particular no ZCode class definition
# or fake implementation is ever added to shell_module.
add_executable(window_controller_tests window_controller_tests.cpp
    fakes/tray_controller.cpp fakes/recorder/ffrecorder.h
    ${shell_root}/shell/shell_logic.cpp
    ${shell_root}/shell/shell_settings.cpp ${shell_root}/shell/shell_settings.h
    ${shell_root}/shell/shell_state_source.cpp ${shell_root}/shell/shell_state_source.h
    ${shell_root}/shell/window_controller.cpp ${shell_root}/shell/window_controller.h
    ${shell_root}/shell/tray_controller.h)
set_target_properties(window_controller_tests PROPERTIES AUTOMOC ON)
target_compile_features(window_controller_tests PRIVATE cxx_std_17)
target_include_directories(window_controller_tests BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/fakes" ${shell_root})
target_link_libraries(window_controller_tests PRIVATE Qt6::Widgets Qt6::Quick)

# Neither test accepts mode arguments. The protocol test is plain C++ (no Qt).
add_executable(single_instance_protocol_tests single_instance_protocol_tests.cpp)
set_target_properties(single_instance_protocol_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(single_instance_protocol_tests PRIVATE cxx_std_17)
target_include_directories(single_instance_protocol_tests PRIVATE ${shell_root})
# Minimal equivalent of shell_module: only SingleInstance + AUTOMOC, Core,
# Network and Threads (std::thread in the harness), plus user32 on Windows.
# Linking shell_module would also pull its combined moc object and engine APIs.
find_package(Threads REQUIRED)
add_executable(single_instance_tests single_instance_tests.cpp
    ${shell_root}/shell/single_instance.cpp ${shell_root}/shell/single_instance.h)
set_target_properties(single_instance_tests PROPERTIES AUTOMOC ON)
target_compile_features(single_instance_tests PRIVATE cxx_std_17)
target_include_directories(single_instance_tests PRIVATE ${shell_root})
target_link_libraries(single_instance_tests PRIVATE Qt6::Core Qt6::Network Threads::Threads)
if(WIN32)
    target_link_libraries(single_instance_tests PRIVATE user32)
endif()
foreach(suite IN ITEMS single_instance_protocol single_instance)
    add_test(NAME ${suite} COMMAND ${suite}_tests)
    set_tests_properties(${suite} PROPERTIES TIMEOUT 15 LABELS "shell")
endforeach()

function(shell_test_modes suite)
    foreach(mode IN LISTS ARGN)
        add_test(NAME ${suite}_${mode} COMMAND ${suite}_tests ${mode})
        set_tests_properties(${suite}_${mode} PROPERTIES TIMEOUT 15 LABELS "shell"
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software")
    endforeach()
endfunction()
shell_test_modes(shell_logic close minimize exit notifications exclusion states)
shell_test_modes(tray_menu_model model tooltip icons tray)
shell_test_modes(shell_settings defaults roundtrip signals invalid unsupported)
shell_test_modes(window_controller start actions visibility close no-tray source)
include("${shell_root}/cmake/SubRecTests.cmake")
subrec_configure_tests()
