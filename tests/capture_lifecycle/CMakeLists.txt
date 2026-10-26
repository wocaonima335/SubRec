# Fake-opener control-plane regression harness, NOT full R-01/media acceptance.
# The root CMakeLists.txt must call enable_testing() (done in the root project).
# Linked into the app build tree; no separate configure or app executable needed.
add_executable(capture_lifecycle_tests capture_lifecycle_tests.cpp)

target_include_directories(capture_lifecycle_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${FFMPEG_INCLUDE_DIR}
)

target_link_libraries(capture_lifecycle_tests PRIVATE
    recorder_module
    capture_module
    queue_module
    muxer_module
    demuxer_module
    decoder_module
    resampler_module
    filter_module
    thread_module
    event_module
    encoder_module
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${FFMPEG_LIBRARIES}
)

target_compile_features(capture_lifecycle_tests PRIVATE cxx_std_17)

add_test(NAME capture_lifecycle_tests COMMAND capture_lifecycle_tests)
set_tests_properties(capture_lifecycle_tests PROPERTIES
    TIMEOUT 45
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:capture_lifecycle_tests>"
    LABELS "capture;lifecycle;fake-opener"
)

# Let CTest run without building/deploying appSubRec first. Preserve the
# caller's PATH, prepending the configured Qt/FFmpeg/compiler runtime folders.
if(WIN32)
    get_filename_component(_lifecycle_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_lifecycle_path "$<TARGET_FILE_DIR:Qt6::Core>;${FFMPEG_DLL_DIR};${_lifecycle_compiler_bin};$ENV{PATH}")
    string(REPLACE ";" "\\;" _lifecycle_path "${_lifecycle_path}")
    set_tests_properties(capture_lifecycle_tests PROPERTIES
        ENVIRONMENT "PATH=${_lifecycle_path}"
    )
endif()
