# subrec-asr：AI 字幕识别宿主子进程（工作包 C，ZCode 终端）。design-live-v2.md §9.1、§10.3 C、§11.1；D12（MSVC）。
#
# 源码目录 tools/asr_host/；安装目录名仍是 asr/（运行时 {app}\asr\subrec-asr.exe），二者不要混淆。
# 不依赖 Qt、FFmpeg。本阶段不链接 sherpa-onnx、不下载任何模型。两种用法：
#   1) 独立（发布宿主按 D12 用 MSVC）：cmake -S tools/asr_host -B build-asr && cmake --build build-asr
#   2) 仓库内：顶层 -DSUBREC_BUILD_SUBTITLE=ON 时随 subtitle/ 一起 add_subdirectory(tools/asr_host)，
#      用主程序工具链（MinGW）编译，只作编译检查；appSubRec 不依赖它，也不复制到输出目录（I5 由包 G 做）。
#
# 目标
#   subrec_asr_core          宿主内部库：协议 IO、spool 尾随读取、段状态机、父看门、引擎/VAD 工厂、
#                            假引擎 + 共享的协议编解码
#   subrec-asr               宿主可执行文件
#   subrec-asr-fakehost      进程外假宿主（脚本回放协议，供主进程测试）
#
# 与主进程共享的只有三个纯 C++17 头 subtitle/subtitle_types.h、subtitle/subtitle_error.h、
# subtitle/host/asr_protocol.h，以及 subtitle/host/asr_protocol.cpp（直接编译进来，不经 subtitle/ 的 CMake）。
# host_stubs.cpp 已按工作包规则删除并拆分为各同名 .cpp。

cmake_minimum_required(VERSION 3.16)
project(SubRecAsrHost LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # sherpa-onnx v2.0.2 shared-MT 包为静态 CRT（D12）；宿主与之保持一致。
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8 /W4)
endif()

get_filename_component(SUBREC_ASR_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SUBREC_ASR_VERSION "0.0.0-m0" CACHE STRING "subrec-asr version string reported in hello")

find_package(Threads REQUIRED)

add_library(subrec_asr_core STATIC
    src/asr_engine.h
    src/asr_engine.cpp
    src/engine_fake.h
    src/engine_fake.cpp
    src/fake_hooks.h
    src/host_options.h
    src/host_options.cpp
    src/host_runtime.h
    src/host_runtime.cpp
    src/protocol_io.cpp
    src/segment_state_machine.cpp
    src/spool_tail_reader.cpp
    src/parent_watch.cpp
    "${SUBREC_ASR_REPO_ROOT}/subtitle/host/asr_protocol.h"
    "${SUBREC_ASR_REPO_ROOT}/subtitle/host/asr_protocol.cpp"
)
# include 根 = 仓库根，只为了 "subtitle/..." 这三个共享头；宿主代码不得 include subtitle/ 下其他文件。
target_include_directories(subrec_asr_core PUBLIC
    "${SUBREC_ASR_REPO_ROOT}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(subrec_asr_core PUBLIC SUBREC_ASR_VERSION="${SUBREC_ASR_VERSION}")
target_link_libraries(subrec_asr_core PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(subrec_asr_core PUBLIC psapi)   # GetProcessMemoryInfo
endif()

add_executable(subrec-asr src/main.cpp)
target_link_libraries(subrec-asr PRIVATE subrec_asr_core)

add_executable(subrec-asr-fakehost
    fake_host/fake_host_script.h
    fake_host/fake_host_script.cpp
    fake_host/fake_host_main.cpp
    "${SUBREC_ASR_REPO_ROOT}/subtitle/host/asr_protocol.h"
    "${SUBREC_ASR_REPO_ROOT}/subtitle/host/asr_protocol.cpp")
target_include_directories(subrec-asr-fakehost PRIVATE "${SUBREC_ASR_REPO_ROOT}")

# wmain 入口：MinGW 需要 -municode 设置入口点（MSVC 自动选择 wmainCRTStartup）。
if(WIN32 AND NOT MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(subrec-asr PRIVATE -municode)
    target_link_options(subrec-asr-fakehost PRIVATE -municode)
endif()

# ---- 测试（tests/asr_host，工作包 C）----
# 独立构建（cmake -S tools/asr_host）默认开启：build-asr 里直接 ctest；
# 仓库内构建默认关闭（tests/ 的挂接归包 G / SubRecTests 规则），可显式 -DSUBREC_ASR_BUILD_TESTS=ON 打开。
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(SUBREC_ASR_TESTS_DEFAULT ON)
else()
    set(SUBREC_ASR_TESTS_DEFAULT OFF)
endif()
option(SUBREC_ASR_BUILD_TESTS "Build tests/asr_host (ctest from the standalone tools/asr_host build)"
       ${SUBREC_ASR_TESTS_DEFAULT})
if(SUBREC_ASR_BUILD_TESTS)
    enable_testing()
    add_subdirectory("${SUBREC_ASR_REPO_ROOT}/tests/asr_host" "${CMAKE_CURRENT_BINARY_DIR}/asr_host_tests")
endif()
