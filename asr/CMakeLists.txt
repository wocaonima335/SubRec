# subrec-asr：AI 字幕识别宿主子进程（M0 骨架）。design-live-v2.md §9.1、§10.3 C、§11.1；D12（MSVC）。
#
# 独立工程，不依赖 Qt、FFmpeg，也不被顶层 CMakeLists.txt 构建（主程序是 MinGW，宿主按 D12 用 MSVC，
# CI 由集成挂接点 I7 新增单独 job）。M0 不链接 sherpa-onnx、不下载任何模型。
#
#   cmake -S asr -B build-asr && cmake --build build-asr
#
# 目标
#   subrec_asr_core          宿主内部库：引擎/VAD 接口、假引擎、运行时骨架 + 共享的协议编解码
#   subrec-asr               宿主可执行文件
#   subrec-asr-fakehost      进程外假宿主（脚本回放协议，供主进程测试）
#
# 与主进程共享的只有三个纯 C++17 头 subtitle/subtitle_types.h、subtitle/subtitle_error.h、
# subtitle/host/asr_protocol.h，以及 subtitle/host/asr_protocol.cpp（直接编译进来，不经 subtitle/ 的 CMake）。

cmake_minimum_required(VERSION 3.16)
project(SubRecAsrHost LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # sherpa-onnx v2.0.2 shared-MT 包为静态 CRT（D12）；宿主与之保持一致。
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8 /W4)
endif()

get_filename_component(SUBREC_ASR_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(SUBREC_ASR_VERSION "0.0.0-m0" CACHE STRING "subrec-asr version string reported in hello")

find_package(Threads REQUIRED)

add_library(subrec_asr_core STATIC
    src/asr_engine.h
    src/engine_fake.h
    src/engine_fake.cpp
    src/host_options.h
    src/host_runtime.h
    src/host_stubs.cpp
    "${SUBREC_ASR_REPO_ROOT}/subtitle/host/asr_protocol.h"
    "${SUBREC_ASR_REPO_ROOT}/subtitle/host/asr_protocol.cpp"
)
# include 根 = 仓库根，只为了 "subtitle/..." 这三个共享头；宿主代码不得 include subtitle/ 下其他文件。
target_include_directories(subrec_asr_core PUBLIC
    "${SUBREC_ASR_REPO_ROOT}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(subrec_asr_core PUBLIC Threads::Threads)

add_executable(subrec-asr src/main.cpp)
target_compile_definitions(subrec-asr PRIVATE SUBREC_ASR_VERSION="${SUBREC_ASR_VERSION}")
target_link_libraries(subrec-asr PRIVATE subrec_asr_core)

add_executable(subrec-asr-fakehost
    fake_host/fake_host_script.h
    fake_host/fake_host_main.cpp)
target_include_directories(subrec-asr-fakehost PRIVATE "${SUBREC_ASR_REPO_ROOT}")
