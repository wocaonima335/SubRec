# subtitle_protocol：asr_protocol.h/.cpp，编解码实现归工作包 C；tools/asr_host 子进程直接编译同一个 .cpp。
add_library(subtitle_protocol STATIC
    asr_protocol.h
    asr_protocol.cpp
)
target_link_libraries(subtitle_protocol PUBLIC subtitle_headers)

# subtitle_host：AsrHostClient（Qt Core），实现归工作包 D。
if(SUBREC_SUBTITLE_HAS_QT)
    add_library(subtitle_host STATIC
        job_limits.h
        subtitle_metatypes.h
        asr_host_client.h
        asr_host_client_factory.h
        asr_host_client.cpp
    )
    set_target_properties(subtitle_host PROPERTIES AUTOMOC ON)
    target_link_libraries(subtitle_host PUBLIC subtitle_headers subtitle_protocol Qt6::Core)
endif()
