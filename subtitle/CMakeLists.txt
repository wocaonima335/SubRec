# AI 字幕库骨架（M0）。design-live-v2.md §10、design-verified.md §11.1。
#
# 两种用法：
#   1) 仓库内：顶层 -DSUBREC_BUILD_SUBTITLE=ON 时 add_subdirectory(subtitle)。默认 OFF，appSubRec 不链接任何字幕目标
#      （链接与 POST_BUILD 复制 tools/asr_host 产物到 asr/ 属于集成挂接点 I5，由工作包 G 最后做）。
#   2) 独立：cmake -S subtitle -B <build>。不需要 FFmpeg；找到 Qt6 Core 才构建 Qt 目标，否则只构建纯 C++17 目标。
#
# 目标与依赖方向（单向）：
#   subtitle_headers（INTERFACE：include 根 = 仓库根，C++17）
#   subtitle_tap ← subtitle_audio            纯 C++17（tap 实现阶段再加 avutil）
#   subtitle_core ← subtitle_live            纯 C++17
#   subtitle_protocol                        纯 C++17，与 tools/asr_host 子进程共用 host/asr_protocol.cpp
#   subtitle_host（Qt Core）← subtitle_service（Qt Core）    subtitle_models（Qt Core；E 实现时加 Network）
#   subtitle_pure（INTERFACE 聚合全部纯 C++ 目标）
# subtitle/* 不得 include recorder/、thread/ 等录制代码。

cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(SubRecSubtitle LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(SUBREC_SUBTITLE_STANDALONE ON)
    enable_testing()
else()
    set(SUBREC_SUBTITLE_STANDALONE OFF)
endif()

option(SUBREC_SUBTITLE_WITH_QT "Build the Qt-based subtitle targets (host/service/models) when Qt6 Core is available" ON)
option(SUBREC_SUBTITLE_M0_CHECKS "Build M0 checks: per-header self-containment objects and a stub link check" ON)

get_filename_component(SUBREC_SUBTITLE_INCLUDE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

add_library(subtitle_headers INTERFACE)
target_include_directories(subtitle_headers INTERFACE "${SUBREC_SUBTITLE_INCLUDE_ROOT}")
target_compile_features(subtitle_headers INTERFACE cxx_std_17)

# ---- Qt 探测 ----
set(SUBREC_SUBTITLE_HAS_QT OFF)
if(SUBREC_SUBTITLE_WITH_QT)
    if(TARGET Qt6::Core)
        set(SUBREC_SUBTITLE_HAS_QT ON)
    else()
        find_package(Qt6 QUIET COMPONENTS Core)
        if(Qt6Core_FOUND)
            set(SUBREC_SUBTITLE_HAS_QT ON)
        endif()
    endif()
endif()
if(NOT SUBREC_SUBTITLE_HAS_QT)
    message(STATUS "subtitle: Qt6 Core not available, building pure C++17 targets only")
endif()

# 纯 C++17 目标
add_subdirectory(tap)
add_subdirectory(audio)
add_subdirectory(core)
add_subdirectory(live)
add_subdirectory(host)        # subtitle_protocol 总是构建；subtitle_host 仅在有 Qt 时
if(SUBREC_SUBTITLE_HAS_QT)
    add_subdirectory(service)
    add_subdirectory(models)
endif()

add_library(subtitle_pure INTERFACE)
target_link_libraries(subtitle_pure INTERFACE
    subtitle_tap subtitle_audio subtitle_core subtitle_live subtitle_protocol)

if(SUBREC_SUBTITLE_M0_CHECKS)
    add_subdirectory(m0_check)
endif()

# package A tests: standalone defaults ON; app follows its existing test switch.
if(SUBREC_SUBTITLE_STANDALONE)
    option(BUILD_TESTING "Build subtitle tests" ON)
endif()
if((SUBREC_SUBTITLE_STANDALONE AND BUILD_TESTING) OR
   (NOT SUBREC_SUBTITLE_STANDALONE AND SUBREC_BUILD_LIFECYCLE_TESTS))
    enable_testing()
    add_subdirectory("${SUBREC_SUBTITLE_INCLUDE_ROOT}/tests/subtitle_tap" "tests/subtitle_tap")
    add_subdirectory("${SUBREC_SUBTITLE_INCLUDE_ROOT}/tests/subtitle_audio" "tests/subtitle_audio")
    # Package B: synthetic subtitle core tests.
    add_subdirectory("${SUBREC_SUBTITLE_INCLUDE_ROOT}/tests/subtitle_core" "tests/subtitle_core")
    # Package H: synthetic live subtitle tests.
    add_subdirectory("${SUBREC_SUBTITLE_INCLUDE_ROOT}/tests/subtitle_live" "tests/subtitle_live")
    if(SUBREC_SUBTITLE_HAS_QT)
        add_subdirectory("${SUBREC_SUBTITLE_INCLUDE_ROOT}/tests/subtitle_host" "tests/subtitle_host")
        add_subdirectory("${SUBREC_SUBTITLE_INCLUDE_ROOT}/tests/subtitle_service" "tests/subtitle_service")
    endif()
endif()
