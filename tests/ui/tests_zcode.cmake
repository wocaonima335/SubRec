# Owned by ZCode. Unit tests for U1, U4, FB2, H2, TB1 where testable off Windows
# (UI_BATCH_PLAN.md §2). Same pattern as tests_codex.cmake.
function(subrec_ui_add_zcode_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_compile_features(${name} PRIVATE cxx_std_17)
    target_link_libraries(${name} PRIVATE ui_module ui_moduleplugin ui_test_shell_batch2
        Qt6::Gui Qt6::Quick)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 30 LABELS "ui"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software")
endfunction()

# U1
subrec_ui_add_zcode_test(theme_controller_tests)
if(WIN32)
    # The test reads SPI_GETCLIENTAREAANIMATION itself (SystemParametersInfoW).
    target_link_libraries(theme_controller_tests PRIVATE user32)
endif()

# H2 (shell-only; no ui_module QML needed but the helper keeps one registration pattern)
subrec_ui_add_zcode_test(global_hotkeys_tests)

# TB1
subrec_ui_add_zcode_test(taskbar_overlay_tests)

# U4
subrec_ui_add_zcode_test(app_preferences_tests)
