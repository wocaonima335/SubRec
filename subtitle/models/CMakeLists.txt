# 工作包 E 独占。实现下载时追加 Qt6::Network（顶层已 find_package Network）。
add_library(subtitle_models STATIC
    model_manager.h
    model_manager.cpp
)
set_target_properties(subtitle_models PROPERTIES AUTOMOC ON)
target_link_libraries(subtitle_models PUBLIC subtitle_headers Qt6::Core)
