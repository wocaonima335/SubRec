cmake_minimum_required(VERSION 3.16)

project(SubRec VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)

option(SUBREC_BUILD_LIFECYCLE_TESTS "Build all in-tree CTest harnesses" ON)
# New interface (ui/, QML module SubRec.Ui) and minimize batch-2 shell units. ON links
# SubRec.Ui into appSubRec and starts on it (--old-ui falls back). OFF neither builds nor
# loads SubRec.Ui and leaves out the batch-2-only shell sources, but shell_module still
# carries the ungated H4/FB1 changes, e.g. N5 when a start is rejected synchronously while
# the window is hidden (docs/UI_BATCH_PLAN.md INT-B).
option(SUBREC_BUILD_NEW_UI "Build the SubRec.Ui module and batch-2 shell units and start on the new interface" OFF)
option(SUBREC_STRIP_TEST_HOOKS
    "Compile video startup test hooks out of the application" OFF)
if(SUBREC_STRIP_TEST_HOOKS)
    if(SUBREC_BUILD_LIFECYCLE_TESTS)
        message(FATAL_ERROR
            "SUBREC_STRIP_TEST_HOOKS requires SUBREC_BUILD_LIFECYCLE_TESTS=OFF; configure tests in a separate build directory")
    endif()
    # Apply consistently to the app and all modules before adding subdirectories.
    add_compile_definitions(SUBREC_STRIP_TEST_HOOKS=1)
endif()

find_package(Qt6 REQUIRED COMPONENTS Quick)
find_package(Qt6 REQUIRED COMPONENTS Quick Qml QuickControls2 Multimedia Widgets Gui Network)

qt_standard_project_setup(REQUIRES 6.5)

# FFmpeg 库配置：FFmpeg 不随仓库提供，必须通过 -DFFMPEG_ROOT=<path>
# 或环境变量 SUBREC_FFMPEG_ROOT 指定（未提供时 CMake 会给出明确报错）。
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/SubRecFFmpeg.cmake")
subrec_require_ffmpeg_root()

set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")
set(FFMPEG_DLL_DIR "${FFMPEG_ROOT}/bin")

message(STATUS "Using FFmpeg root: ${FFMPEG_ROOT}")
message(STATUS "Using FFmpeg include dir: ${FFMPEG_INCLUDE_DIR}")
message(STATUS "Using FFmpeg lib dir: ${FFMPEG_LIB_DIR}")
message(STATUS "Using FFmpeg dll dir: ${FFMPEG_DLL_DIR}")

foreach(required_ffmpeg_dir
        "${FFMPEG_INCLUDE_DIR}"
        "${FFMPEG_LIB_DIR}"
        "${FFMPEG_DLL_DIR}")
    if(NOT EXISTS "${required_ffmpeg_dir}")
        message(FATAL_ERROR "Missing FFmpeg directory: ${required_ffmpeg_dir}")
    endif()
endforeach()

# 添加 FFmpeg 头文件目录
include_directories(${FFMPEG_INCLUDE_DIR})
# Repo root reaches shell users via shell_module PUBLIC includes only; a global
# root include would precede per-test fakes/ dirs and shadow their fake headers.

function(resolve_ffmpeg_library out_var lib_name)
    unset(${out_var})
    unset(${out_var} CACHE)
    find_library(${out_var}
        NAMES ${lib_name}
        PATHS "${FFMPEG_LIB_DIR}"
        NO_DEFAULT_PATH
    )

    if(NOT ${out_var})
        message(FATAL_ERROR "Missing FFmpeg library ${lib_name} under ${FFMPEG_LIB_DIR}")
    endif()
endfunction()

# 查找 FFmpeg 库文件。每次配置都清理旧缓存，避免切换 FFMPEG_ROOT 后仍然链接到历史路径。
resolve_ffmpeg_library(AVCODEC_LIB avcodec)
resolve_ffmpeg_library(AVFORMAT_LIB avformat)
resolve_ffmpeg_library(AVUTIL_LIB avutil)
resolve_ffmpeg_library(SWSCALE_LIB swscale)
resolve_ffmpeg_library(SWRESAMPLE_LIB swresample)
resolve_ffmpeg_library(AVFILTER_LIB avfilter)
resolve_ffmpeg_library(AVDEVICE_LIB avdevice)

# 创建 FFmpeg 库列表
set(FFMPEG_LIBRARIES
    ${AVCODEC_LIB}
    ${AVFORMAT_LIB}
    ${AVUTIL_LIB}
    ${SWSCALE_LIB}
    ${SWRESAMPLE_LIB}
    ${AVFILTER_LIB}
    ${AVDEVICE_LIB}
)

# 添加子目录
add_subdirectory(capture)
add_subdirectory(demuxer)
add_subdirectory(qml)
add_subdirectory(queue)
add_subdirectory(decoder)
add_subdirectory(resampler)
add_subdirectory(filter)
add_subdirectory(muxer)
add_subdirectory(event)
add_subdirectory(thread)
add_subdirectory(recorder)
add_subdirectory(encoder)
add_subdirectory(opengl)
add_subdirectory(shell)
if(SUBREC_BUILD_NEW_UI)
    add_subdirectory(ui)
    execute_process(COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE subrec_ui_git_hash OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    target_compile_definitions(ui_module PRIVATE
        SUBREC_VERSION="${PROJECT_VERSION}" SUBREC_GIT_HASH="${subrec_ui_git_hash}")
endif()

if(SUBREC_BUILD_LIFECYCLE_TESTS)
    enable_testing()
    add_subdirectory(tests/shell)
    if(SUBREC_BUILD_NEW_UI)
        # The standalone UI harness owns engine doubles named shell_module and FFRecorder.
        # Build it separately so those doubles never share the production engine's ABI.
        include(ExternalProject)
        set(subrec_ui_test_dir "${CMAKE_CURRENT_BINARY_DIR}/ui-tests")
        ExternalProject_Add(subrec_ui_test_build
            SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/ui"
            BINARY_DIR "${subrec_ui_test_dir}"
            CMAKE_GENERATOR "${CMAKE_GENERATOR}"
            CMAKE_ARGS
                "-DCMAKE_PREFIX_PATH:PATH=${CMAKE_PREFIX_PATH}"
                "-DQt6_DIR:PATH=${Qt6_DIR}"
                "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
                "-DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}"
                "-DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS}"
            BUILD_ALWAYS TRUE
            INSTALL_COMMAND "")
        add_test(NAME subrec_ui COMMAND "${CMAKE_CTEST_COMMAND}"
            --test-dir "${subrec_ui_test_dir}" --output-on-failure)
        set_tests_properties(subrec_ui PROPERTIES LABELS "ui" TIMEOUT 180
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software")
    endif()
endif()

if(SUBREC_BUILD_LIFECYCLE_TESTS AND WIN32)
    add_subdirectory(tests/gpu_preview)
    add_subdirectory(tests/capture_lifecycle)
    add_subdirectory(tests/startup_failure)
    add_subdirectory(tests/encoder_failure)
    add_subdirectory(tests/media_clock)
    add_subdirectory(tests/logging)
    add_subdirectory(tests/test_hooks)
    add_subdirectory(tests/audio_filter_dual)
    add_subdirectory(tests/muxer_lifecycle)
    add_subdirectory(tests/input_startup)
    add_subdirectory(tests/audio_timeline_mix)
    add_subdirectory(tests/wasapi_loopback)
    add_subdirectory(tests/media_conversion)
    # native_capture_probe is an interactive hardware probe, not a CTest suite.
    get_property(subrec_test_targets GLOBAL PROPERTY SUBREC_TEST_TARGETS)
    add_custom_target(subrec_tests DEPENDS ${subrec_test_targets})
endif()

# 主可执行文件
qt_add_executable(appSubRec
    main.cpp
    res.qrc
)

set_target_properties(appSubRec PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_include_directories(appSubRec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(SUBREC_BUILD_NEW_UI)
    target_compile_definitions(appSubRec PRIVATE SUBREC_HAS_NEW_UI=1)
endif()

# 链接库（包含子模块）
target_link_libraries(appSubRec
    PRIVATE
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Widgets
        shell_module
        qml_module
        opengl_module
        capture_module
        demuxer_module
        queue_module
        decoder_module
        resampler_module
        filter_module
        muxer_module
        thread_module
        event_module
        recorder_module
        encoder_module
)

if(SUBREC_BUILD_NEW_UI)
    target_link_libraries(appSubRec PRIVATE ui_module ui_moduleplugin)
endif()

# 复制 FFmpeg DLL 文件到输出目录
file(GLOB FFMPEG_DLLS CONFIGURE_DEPENDS "${FFMPEG_DLL_DIR}/*.dll")
if(NOT FFMPEG_DLLS)
    message(FATAL_ERROR "No FFmpeg DLLs found under ${FFMPEG_DLL_DIR}")
endif()

add_custom_command(TARGET appSubRec POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${FFMPEG_DLLS}
    $<TARGET_FILE_DIR:appSubRec>
    COMMENT "Copying FFmpeg DLLs to output directory"
)

if(WIN32 AND MINGW)
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(MINGW_RUNTIME_DLL_CANDIDATES
        "${MINGW_BIN_DIR}/libstdc++-6.dll"
        "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
        "${MINGW_BIN_DIR}/libwinpthread-1.dll"
    )

    set(MINGW_RUNTIME_DLLS)
    foreach(runtime_dll IN LISTS MINGW_RUNTIME_DLL_CANDIDATES)
        if(EXISTS "${runtime_dll}")
            list(APPEND MINGW_RUNTIME_DLLS "${runtime_dll}")
        endif()
    endforeach()

    if(MINGW_RUNTIME_DLLS)
        add_custom_command(TARGET appSubRec POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${MINGW_RUNTIME_DLLS}
            $<TARGET_FILE_DIR:appSubRec>
            COMMENT "Copying MinGW runtime DLLs to output directory"
        )
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS appSubRec
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${FFMPEG_DLLS}
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(MINGW_RUNTIME_DLLS)
    install(FILES ${MINGW_RUNTIME_DLLS}
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(SUBREC_BUILD_NEW_UI AND SUBREC_BUILD_LIFECYCLE_TESTS AND TARGET subrec_tests)
    add_dependencies(subrec_tests subrec_ui_test_build)
endif()
