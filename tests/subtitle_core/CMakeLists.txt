add_executable(subtitle_core_tests core_tests.cpp)
target_link_libraries(subtitle_core_tests PRIVATE subtitle_core)
target_compile_definitions(subtitle_core_tests PRIVATE SUBREC_CORE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
set(core_cases golden_srt golden_vtt code_points effective_characters ascii_ratio
    normalization_tags normalization_fillers normalization_invalid_utf8
    hallucination_blacklist hallucination_repetition hallucination_speed hallucination_no_speech
    cues_hard_boundaries cues_punctuation cues_char_count cues_duration cues_token_times
    overlaps_monotonic ass_format json_roundtrip json_invalid srt_roundtrip srt_invalid paths atomic_success)
foreach(case IN LISTS core_cases)
    add_test(NAME subtitle_core_${case} COMMAND subtitle_core_tests ${case})
    set_tests_properties(subtitle_core_${case} PROPERTIES TIMEOUT 30 LABELS "subtitle;core")
endforeach()
# A private compile-time seam in the writer, absent from the production library.
add_executable(subtitle_core_atomic_tests atomic_tests.cpp ../../subtitle/core/subtitle_writers.cpp)
target_link_libraries(subtitle_core_atomic_tests PRIVATE subtitle_headers)
target_compile_definitions(subtitle_core_atomic_tests PRIVATE SUBREC_CORE_TEST_SEAM=1)
add_test(NAME subtitle_core_atomic_midwrite_failure COMMAND subtitle_core_atomic_tests)
set_tests_properties(subtitle_core_atomic_midwrite_failure PROPERTIES TIMEOUT 30 LABELS "subtitle;core")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/SubRecTests.cmake")
subrec_configure_tests()
