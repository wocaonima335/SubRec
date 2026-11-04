# Standalone harness for SubRec.Ui (UI_BATCH_PLAN.md §5): builds ui/ and the batch-2 shell
# stubs without FFmpeg or the recording engine, runs the interface smoke test and exposes
# Qt's qmllint targets (ui_module_qmllint / all_qmllint).
#   cmake -S tests/ui -B build-ui -DCMAKE_PREFIX_PATH=<Qt>/6.8.0/<kit>
#   cmake --build build-ui && ctest --test-dir build-ui && cmake --build build-ui --target all_qmllint
# Not added by the top-level CMakeLists.txt yet (module INT-B decides when).
cmake_minimum_required(VERSION 3.21)
project(subrec_ui_tests LANGUAGES CXX)
enable_testing()
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Widgets)
qt_standard_project_setup(REQUIRES 6.5)

set(subrec_root "${CMAKE_CURRENT_LIST_DIR}/../..")

# Batch-2 shell units (normally inside shell_module under SUBREC_BUILD_NEW_UI) plus the
# engine-free batch-1 units they use (ShellSettings for FB1, shell_logic, tray icons for TB1).
# Unit tests of H1/H2/FB1/TB1 link only this.
add_library(ui_test_shell_batch2 STATIC
    ${subrec_root}/shell/shell_types.h
    ${subrec_root}/shell/shell_logic.h ${subrec_root}/shell/shell_logic.cpp
    ${subrec_root}/shell/shell_settings.h ${subrec_root}/shell/shell_settings.cpp
    ${subrec_root}/shell/tray_icon_factory.h ${subrec_root}/shell/tray_icon_factory.cpp
    ${subrec_root}/shell/key_chord.h ${subrec_root}/shell/key_chord.cpp
    ${subrec_root}/shell/global_hotkeys.h ${subrec_root}/shell/global_hotkeys.cpp
    ${subrec_root}/shell/floating_bar_policy.h ${subrec_root}/shell/floating_bar_policy.cpp
    ${subrec_root}/shell/taskbar_overlay.h ${subrec_root}/shell/taskbar_overlay.cpp)
set_target_properties(ui_test_shell_batch2 PROPERTIES AUTOMOC ON)
target_compile_features(ui_test_shell_batch2 PUBLIC cxx_std_17)
target_include_directories(ui_test_shell_batch2 PUBLIC ${subrec_root})
target_link_libraries(ui_test_shell_batch2 PUBLIC Qt6::Gui Qt6::Widgets)

# Harness stand-in for the app's shell_module, so ui_module (which links shell_module when
# that target exists) resolves WindowController/ShellSettings/... once B2, H3 and FB2 call
# into them. Same test seams as tests/shell (fake recorder, preview double); never built
# when a real shell_module exists (top-level build).
if(NOT TARGET shell_module)
    add_library(shell_module STATIC
        ${subrec_root}/shell/shell_state_source.h ${subrec_root}/shell/shell_state_source.cpp
        ${subrec_root}/shell/tray_controller.h ${subrec_root}/shell/tray_controller.cpp
        ${subrec_root}/shell/window_controller.h ${subrec_root}/shell/window_controller.cpp
        ${subrec_root}/shell/capture_exclusion.h ${subrec_root}/shell/capture_exclusion.cpp
        ${subrec_root}/shell/power_request.h ${subrec_root}/shell/power_request.cpp
        # Listed so AUTOMOC generates the fake FFRecorder's meta-object.
        ${subrec_root}/tests/shell/fakes/observable_recorder.h)
    set_target_properties(shell_module PROPERTIES AUTOMOC ON)
    target_compile_definitions(shell_module PRIVATE SUBREC_SHELL_CONTROLLER_TEST_DOUBLES=1)
    target_include_directories(shell_module PRIVATE "${subrec_root}/tests/shell")
    target_link_libraries(shell_module PUBLIC ui_test_shell_batch2 Qt6::Widgets Qt6::Quick)
    if(WIN32)
        target_link_libraries(shell_module PRIVATE user32)
    endif()
endif()

add_subdirectory("${subrec_root}/ui" ui_module_build)

qt_add_executable(ui_interface_smoke ui_interface_smoke.cpp)
target_compile_features(ui_interface_smoke PRIVATE cxx_std_17)
target_link_libraries(ui_interface_smoke PRIVATE
    ui_module ui_moduleplugin ui_test_shell_batch2 Qt6::Quick Qt6::Widgets)
add_test(NAME ui_interface_smoke COMMAND ui_interface_smoke)
set_tests_properties(ui_interface_smoke PROPERTIES TIMEOUT 30 LABELS "ui"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software")

# Per-owner test lists (UI_BATCH_PLAN.md §4): each file appends add_executable/add_test
# for its own *_tests.cpp. Both start empty.
include("${CMAKE_CURRENT_LIST_DIR}/tests_codex.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/tests_zcode.cmake")
