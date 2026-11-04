# Owned by Codex. Unit tests for B1–B4, U6–U8, H1, H3, FB1 (UI_BATCH_PLAN.md §2).
# Pattern: add_executable(<name>_tests <name>_tests.cpp), link ui_module and/or
# ui_test_shell_batch2, add_test(... LABELS "ui"). Empty until the modules land.
add_executable(key_chord_tests ${CMAKE_CURRENT_LIST_DIR}/key_chord_tests.cpp)
target_link_libraries(key_chord_tests PRIVATE ui_test_shell_batch2)
add_test(NAME key_chord_tests COMMAND key_chord_tests)
set_tests_properties(key_chord_tests PROPERTIES LABELS "ui")

find_package(Qt6 REQUIRED COMPONENTS Test)
set_target_properties(key_chord_tests PROPERTIES AUTOMOC ON)
target_link_libraries(key_chord_tests PRIVATE Qt6::Test)
foreach(suite IN ITEMS recorder_view shell_view output_naming audio_level_monitor
                       floating_bar_policy hotkey_controller recording_library subtitle_view)
    qt_add_executable(${suite}_tests ${CMAKE_CURRENT_LIST_DIR}/${suite}_tests.cpp)
    target_link_libraries(${suite}_tests PRIVATE ui_module ui_test_shell_batch2 Qt6::Test Qt6::Quick Qt6::Widgets)
    add_test(NAME ${suite}_tests COMMAND ${suite}_tests)
    set_tests_properties(${suite}_tests PROPERTIES TIMEOUT 30 LABELS "ui"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software")
endforeach()

# Reuse the existing shell regressions without changing their shared CMake file.
foreach(suite IN ITEMS shell_logic shell_settings tray_menu_model)
    add_executable(ui_${suite}_tests ${subrec_root}/tests/shell/${suite}_tests.cpp)
    target_link_libraries(ui_${suite}_tests PRIVATE shell_module ui_test_shell_batch2 Qt6::Widgets)
endforeach()
qt_add_executable(ui_window_controller_tests
    ${subrec_root}/tests/shell/window_controller_tests.cpp
    ${subrec_root}/tests/shell/fakes/tray_controller.cpp
    ${subrec_root}/tests/shell/fakes/observable_recorder.h
    ${subrec_root}/shell/window_controller.cpp ${subrec_root}/shell/window_controller.h
    ${subrec_root}/shell/shell_state_source.cpp ${subrec_root}/shell/shell_state_source.h
    ${subrec_root}/shell/capture_exclusion.cpp ${subrec_root}/shell/capture_exclusion.h
    ${subrec_root}/shell/power_request.cpp ${subrec_root}/shell/power_request.h
    ${subrec_root}/shell/tray_controller.h)
target_compile_definitions(ui_window_controller_tests PRIVATE SUBREC_SHELL_CONTROLLER_TEST_DOUBLES=1)
target_include_directories(ui_window_controller_tests PRIVATE ${subrec_root}/tests/shell)
target_link_libraries(ui_window_controller_tests PRIVATE ui_test_shell_batch2 Qt6::Quick Qt6::Widgets)
if(WIN32)
    target_link_libraries(ui_window_controller_tests PRIVATE user32)
endif()
function(codex_shell_modes suite)
    foreach(mode IN LISTS ARGN)
        add_test(NAME ui_${suite}_${mode} COMMAND ui_${suite}_tests ${mode})
        set_tests_properties(ui_${suite}_${mode} PROPERTIES TIMEOUT 30 LABELS "ui;shell"
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software")
    endforeach()
endfunction()
codex_shell_modes(shell_logic close minimize exit notifications exclusion states)
codex_shell_modes(shell_settings defaults roundtrip signals invalid unsupported autotest_isolated floating)
codex_shell_modes(tray_menu_model model tooltip icons tray)
codex_shell_modes(window_controller start actions visibility close no-tray source batch2)

# QML interaction checks live with their owning backend suites.
target_link_libraries(recording_library_tests PRIVATE ui_moduleplugin)
target_link_libraries(subtitle_view_tests PRIVATE ui_moduleplugin)
