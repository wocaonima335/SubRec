# SubRec.Ui — new interface (UI_REDESIGN.md) + minimize batch 2 surfaces (MINIMIZE_DESIGN.md
# §3.10). FROZEN (UI_BATCH_PLAN.md §4): file lists live in files_zcode.cmake /
# files_codex.cmake, each edited only by its owner; change this file only by agreement.
#
# Built by the top-level project only with -DSUBREC_BUILD_NEW_UI=ON, and standalone by
# tests/ui. main.cpp does not load it yet (module INT-A). To load it the app must link
# ui_moduleplugin and use Q_IMPORT_QML_PLUGIN(SubRec_UiPlugin): ui_module is STATIC and its
# C++ singletons are only registered through the plugin.

include("${CMAKE_CURRENT_LIST_DIR}/files_zcode.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/files_codex.cmake")

set(ui_qml_files ${UI_ZCODE_QML} ${UI_CODEX_QML})
set(ui_singletons ${UI_ZCODE_SINGLETONS} ${UI_CODEX_SINGLETONS})

# Flatten qml/<area>/X.qml to X.qml in the module so every file sees every other type
# through the implicit module import (no "import SubRec.Ui" inside the module).
set(ui_qml_basenames)
foreach(qml_file IN LISTS ui_qml_files)
    get_filename_component(qml_name "${qml_file}" NAME)
    if(qml_name IN_LIST ui_qml_basenames)
        message(FATAL_ERROR "SubRec.Ui: duplicate QML basename ${qml_name} (${qml_file})")
    endif()
    list(APPEND ui_qml_basenames "${qml_name}")
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()
set_source_files_properties(${ui_singletons} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(ui_module STATIC)
set_target_properties(ui_module PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

qt_add_qml_module(ui_module
    URI SubRec.Ui
    VERSION 1.0
    # URI-shaped output dir so qmllint/qmlls resolve SubRec.Ui from ${CMAKE_BINARY_DIR}/ui_qml.
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ui_qml/SubRec/Ui"
    QML_FILES ${ui_qml_files}
    SOURCES
        backend/singleton_provider.h
        backend/settings_store.h backend/settings_store.cpp
        ${UI_ZCODE_SOURCES}
        ${UI_CODEX_SOURCES}
    RESOURCES ${UI_ZCODE_RESOURCES} ${UI_CODEX_RESOURCES}
    # CaptionGlyph.qml imports QtQuick.Shapes: recorded as a qmldir "depends" and given to
    # the QML tooling (qmllint, qmlcachegen, deployment import scanning).
    DEPENDENCIES QtQuick.Shapes
)

# Repo root for "ui/backend/..." and "shell/..." includes; backend/ for the generated
# qmltyperegistrations.cpp, which includes the headers by bare file name.
target_include_directories(ui_module
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.."
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/backend")
target_link_libraries(ui_module PUBLIC Qt6::Qml Qt6::Quick Qt6::QuickControls2)
# QtQuick.Shapes is required. Qt 6.8 has no public C++ module for it (its only CMake
# packages, Qt6QuickShapesPrivate and the qmlshapesplugin plugin target, are internal), and
# ui_module uses no Shapes C++ API, so nothing is linked: the QML import is loaded from the
# kit's qml directory at run time. Configure fails here if the kit lacks the module.
set(subrec_qt_shapes_qmldir "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}/QtQuick/Shapes/qmldir")
if(NOT EXISTS "${subrec_qt_shapes_qmldir}")
    message(FATAL_ERROR "SubRec.Ui: QML module QtQuick.Shapes not found in the Qt kit "
        "(${subrec_qt_shapes_qmldir}); it is part of qtdeclarative.")
endif()
# Backend implementations call shell classes (ShellView, HotkeyController, FloatingBarView).
if(TARGET shell_module)
    target_link_libraries(ui_module PRIVATE shell_module)
endif()
