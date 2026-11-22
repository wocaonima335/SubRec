# M0 检查（工作包 M0 维护；后续由 G 决定是否保留）。
# 1) 头文件自洽：为每个公共头文件生成一个只 #include 该头的 .cpp，编成 OBJECT 库（不链接）。
# 2) 链接检查：subtitle_m0_link_check 链接全部纯 C++ 目标并调用每个桩，保证每个非 inline 声明都有定义。

file(GLOB_RECURSE subrec_subtitle_all_headers CONFIGURE_DEPENDS
    RELATIVE "${SUBREC_SUBTITLE_INCLUDE_ROOT}"
    "${SUBREC_SUBTITLE_INCLUDE_ROOT}/subtitle/*.h")

# 需要 Qt 的头文件
set(subrec_subtitle_qt_headers
    subtitle/host/asr_host_client.h
    subtitle/host/asr_host_client_factory.h
    subtitle/host/subtitle_metatypes.h
    subtitle/service/subtitle_service.h
    subtitle/service/subtitle_job_runner.h
    subtitle/service/refine_scheduler.h
    subtitle/models/model_manager.h)

set(pure_sources)
set(qt_sources)
foreach(hdr IN LISTS subrec_subtitle_all_headers)
    string(MAKE_C_IDENTIFIER "${hdr}" id)
    set(src "${CMAKE_CURRENT_BINARY_DIR}/hdr_${id}.cpp")
    file(GENERATE OUTPUT "${src}" CONTENT "// 自动生成：只包含 ${hdr}\n#include \"${hdr}\"\n")
    list(FIND subrec_subtitle_qt_headers "${hdr}" qt_index)
    if(qt_index EQUAL -1)
        list(APPEND pure_sources "${src}")
    else()
        list(APPEND qt_sources "${src}")
    endif()
endforeach()

add_library(subtitle_header_check_pure OBJECT ${pure_sources})
target_link_libraries(subtitle_header_check_pure PRIVATE subtitle_headers)

if(SUBREC_SUBTITLE_HAS_QT)
    add_library(subtitle_header_check_qt OBJECT ${qt_sources})
    set_target_properties(subtitle_header_check_qt PROPERTIES AUTOMOC OFF)
    target_link_libraries(subtitle_header_check_qt PRIVATE subtitle_headers Qt6::Core)
endif()

add_executable(subtitle_m0_link_check link_check.cpp)
target_link_libraries(subtitle_m0_link_check PRIVATE subtitle_pure)
add_test(NAME subtitle_m0_link_check COMMAND subtitle_m0_link_check)
