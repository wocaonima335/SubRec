# core_stubs.cpp 及 text_normalizer / hallucination_filter / cue_builder / subtitle_writers 归工作包 B；
# rules_extra_stubs.cpp 及 glossary / event_tags / itn_extra 归工作包 K。本文件由 B 维护，K 只在下方列表追加自己的文件。
add_library(subtitle_core STATIC
    text_normalizer.h
    hallucination_filter.h
    cue_builder.h
    subtitle_writers.h
    text_normalizer.cpp
    hallucination_filter.cpp
    cue_builder.cpp
    subtitle_writers.cpp
    # ---- K ----
    glossary.h
    event_tags.h
    itn_extra.h
    glossary.cpp
    event_tags.cpp
    itn_extra.cpp
    pinyin_table.inc
    rules_utf8.inc
)
target_link_libraries(subtitle_core PUBLIC subtitle_headers)
