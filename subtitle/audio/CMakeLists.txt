# 工作包 A 独占。实现时把 audio_stubs.cpp 拆为 mono_resampler_16k.cpp / pcm_spool.cpp / ingest_worker.cpp。
find_package(Threads REQUIRED)
add_library(subtitle_audio STATIC
    mono_resampler_16k.h
    pcm_spool.h
    ingest_worker.h
    mono_resampler_16k.cpp
    pcm_spool.cpp
    ingest_worker.cpp
)
target_link_libraries(subtitle_audio PUBLIC subtitle_headers subtitle_tap Threads::Threads)
