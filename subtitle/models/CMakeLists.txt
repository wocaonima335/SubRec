# 工作包 E 独占：ModelManager（清单/下载/校验/断点续传/许可/删除/engineRequestFor）。
# 实现阶段追加 Qt6::Network（顶层仓库构建已 find_package Network；独立 subtitle 构建在此补找）。
# models.json 经 subtitle_models.qrc 编进资源（AUTORCC；运行时 :/subtitle/models/models.json，
# model_manager.cpp 用 Q_INIT_RESOURCE 保证静态库链接时不被裁掉）。

if(NOT TARGET Qt6::Network)
    find_package(Qt6 REQUIRED COMPONENTS Network)
endif()

add_library(subtitle_models STATIC
    model_manager.h
    model_manager.cpp
    models.json
    subtitle_models.qrc)
set_target_properties(subtitle_models PROPERTIES AUTOMOC ON AUTORCC ON)
target_link_libraries(subtitle_models
    PUBLIC subtitle_headers Qt6::Core
    PRIVATE Qt6::Network)
