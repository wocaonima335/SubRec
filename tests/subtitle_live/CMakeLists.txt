# Package H. W can append its own executables and cases below this block.
add_executable(subtitle_h_agreement_tests h_agreement.cpp)
add_executable(subtitle_h_document_tests h_document.cpp)
add_executable(subtitle_h_fixer_tests h_fixer.cpp)
foreach(group IN ITEMS agreement document fixer)
    target_link_libraries(subtitle_h_${group}_tests PRIVATE subtitle_live)
endforeach()
set(h_agreement_cases timing guard punctuation regression resets freeze mismatch compatibility)
set(h_document_cases idempotent out_of_order resume throttling provisional_on provisional_drop
    provisional_off boundaries states live_config refined_files)
set(h_fixer_cases window merge_boundaries finished user_edited refined_merge)
foreach(group IN ITEMS agreement document fixer)
    foreach(case IN LISTS h_${group}_cases)
        add_test(NAME subtitle_h_${group}_${case} COMMAND subtitle_h_${group}_tests ${case})
        set_tests_properties(subtitle_h_${group}_${case} PROPERTIES TIMEOUT 30 LABELS "subtitle;live;H")
    endforeach()
endforeach()
# Reuse B's compile-time fault seam without changing its source or production target.
add_executable(subtitle_h_atomic_tests h_atomic.cpp ../../subtitle/core/subtitle_writers.cpp)
target_link_libraries(subtitle_h_atomic_tests PRIVATE subtitle_live)
target_compile_definitions(subtitle_h_atomic_tests PRIVATE SUBREC_CORE_TEST_SEAM=1)
add_test(NAME subtitle_h_flush_midwrite_failure COMMAND subtitle_h_atomic_tests)
set_tests_properties(subtitle_h_flush_midwrite_failure PROPERTIES TIMEOUT 30 LABELS "subtitle;live;H")

# ---- Package W: injected timestamps and ManualClock, no sleeps or host process. ----
add_executable(subtitle_w_degrade_tests w_degrade.cpp)
add_executable(subtitle_w_watchdog_tests w_watchdog.cpp)
set(w_degrade_cases l0_l1_lag l0_l1_rtf l0_l1_cpu l1_l2_lag l1_l2_rtf sustain_reset
    l1_l0_hysteresis l2_l1_hysteresis min_interval l2_l3_lag recording_health health_window
    l3_latched force_level timestamps)
set(w_watchdog_cases heartbeat decode_rtf load_timeout backoff restart_window poison poison_idle
    gpu_cpu nonretry oom process_started set_backend reset_for_job)
foreach(group IN ITEMS degrade watchdog)
    target_link_libraries(subtitle_w_${group}_tests PRIVATE subtitle_live)
    foreach(case IN LISTS w_${group}_cases)
        add_test(NAME subtitle_w_${group}_${case} COMMAND subtitle_w_${group}_tests ${case})
        set_tests_properties(subtitle_w_${group}_${case} PROPERTIES TIMEOUT 10 LABELS "subtitle;live;W")
    endforeach()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/SubRecTests.cmake")
subrec_configure_tests()
