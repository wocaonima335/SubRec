# Maintainer: work package K (Glossary / EventTags / ItnExtra).
add_executable(subtitle_rules_tests rules_tests.cpp)
target_link_libraries(subtitle_rules_tests PRIVATE subtitle_core)
set(rules_cases glossary_pinyin_hits glossary_no_false_replacements
    glossary_conflicts_and_overlaps glossary_ascii_words glossary_miss
    prompt_filter_order prompt_budget_boundaries prompt_utf8_safety
    event_mapping event_application event_idempotence
    itn_empty_identity itn_literal_order itn_edge_cases)
foreach(case IN LISTS rules_cases)
    add_test(NAME subtitle_rules_${case} COMMAND subtitle_rules_tests ${case})
    set_tests_properties(subtitle_rules_${case} PROPERTIES TIMEOUT 30 LABELS "subtitle;rules")
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/SubRecTests.cmake")
subrec_configure_tests()
