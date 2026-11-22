# merger_stubs.cpp 及 local_agreement / sentence_fixer / subtitle_merger 归工作包 H；
# degrade_watchdog_stubs.cpp 及 degrade_controller / host_watchdog 归工作包 W。本文件由 H 维护，W 只改自己的行。
add_library(subtitle_live STATIC
    local_agreement.h
    sentence_fixer.h
    subtitle_merger.h
    local_agreement.cpp
    sentence_fixer.cpp
    subtitle_merger.cpp
    # ---- W ----
    degrade_controller.h
    host_watchdog.h
    degrade_watchdog_stubs.cpp
)
target_link_libraries(subtitle_live PUBLIC subtitle_headers subtitle_core)
