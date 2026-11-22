# service_stubs.cpp 及 subtitle_service / subtitle_job_runner 归工作包 D；refine_scheduler 归工作包 R（M2）。
add_library(subtitle_service STATIC
    subtitle_service.h
    subtitle_job_runner.h
    service_stubs.cpp
    # ---- R ----
    refine_scheduler.h
    refine_scheduler.cpp
)
set_target_properties(subtitle_service PROPERTIES AUTOMOC ON)
target_link_libraries(subtitle_service PUBLIC
    subtitle_headers subtitle_host subtitle_live subtitle_audio subtitle_tap subtitle_core Qt6::Core)
