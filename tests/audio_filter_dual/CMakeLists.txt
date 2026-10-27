# FFAFilter audio-graph test with synthetic frames: no devices, no recording, no app run.
# Built inside the app build tree (root CMakeLists.txt, SUBREC_BUILD_LIFECYCLE_TESTS) so it
# links the very filter_module/queue_module the app uses.
add_executable(audio_filter_dual_tests audio_filter_dual_tests.cpp)

target_include_directories(audio_filter_dual_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${FFMPEG_INCLUDE_DIR}
)

# filter_module references FFADecoder, and decoder_module links recorder_module, so use the
# same (already proven on PC2) module list as capture_lifecycle instead of a minimal one.
target_link_libraries(audio_filter_dual_tests PRIVATE
    filter_module
    recorder_module
    capture_module
    queue_module
    muxer_module
    demuxer_module
    decoder_module
    resampler_module
    thread_module
    event_module
    encoder_module
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${FFMPEG_LIBRARIES}
)

target_compile_features(audio_filter_dual_tests PRIVATE cxx_std_17)

add_test(NAME audio_filter_dual COMMAND audio_filter_dual_tests)
set_tests_properties(audio_filter_dual PROPERTIES
    TIMEOUT 30
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:audio_filter_dual_tests>"
    LABELS "audio;filter;synthetic"
)

# Same runtime lookup as capture_lifecycle: Qt, FFmpeg and compiler runtime folders on PATH.
if(WIN32)
    get_filename_component(_audio_filter_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_audio_filter_path "$<TARGET_FILE_DIR:Qt6::Core>;${FFMPEG_DLL_DIR};${_audio_filter_compiler_bin};$ENV{PATH}")
    string(REPLACE ";" "\\;" _audio_filter_path "${_audio_filter_path}")
    set_tests_properties(audio_filter_dual PROPERTIES
        ENVIRONMENT "PATH=${_audio_filter_path}"
    )
endif()

# Hardware-independent production timeline/amix/limiter regression tests.
add_executable(audio_timeline_mix_tests ../audio_timeline_mix/audio_timeline_mix_tests.cpp
    ../../filter/audio_timeline_mixer.cpp ../../filter/audio_mix_dump.cpp)
target_compile_features(audio_timeline_mix_tests PRIVATE cxx_std_17)
target_include_directories(audio_timeline_mix_tests PRIVATE ../.. ${FFMPEG_INCLUDE_DIR})
target_link_libraries(audio_timeline_mix_tests PRIVATE ${FFMPEG_LIBRARIES})
add_test(NAME audio_timeline_mix COMMAND audio_timeline_mix_tests)
set_tests_properties(audio_timeline_mix PROPERTIES TIMEOUT 30 LABELS "audio;mix;synthetic")
if(WIN32)
    set_tests_properties(audio_timeline_mix PROPERTIES ENVIRONMENT "PATH=${_audio_filter_path}")
endif()

add_executable(audio_mix_aac_tests ../audio_timeline_mix/audio_mix_aac_tests.cpp
    ../../filter/audio_timeline_mixer.cpp ../../filter/audio_mix_dump.cpp)
target_compile_features(audio_mix_aac_tests PRIVATE cxx_std_17)
target_include_directories(audio_mix_aac_tests PRIVATE ../.. ${FFMPEG_INCLUDE_DIR})
target_link_libraries(audio_mix_aac_tests PRIVATE ${FFMPEG_LIBRARIES})
add_test(NAME audio_mix_aac COMMAND audio_mix_aac_tests)
set_tests_properties(audio_mix_aac PROPERTIES TIMEOUT 60 LABELS "audio;mix;aac;synthetic")
if(WIN32)
    set_tests_properties(audio_mix_aac PROPERTIES ENVIRONMENT "PATH=${_audio_filter_path}")
endif()

add_executable(audio_mix_drift_tests ../audio_timeline_mix/audio_mix_drift_tests.cpp
    ../../filter/audio_timeline_mixer.cpp ../../filter/audio_mix_dump.cpp)
target_compile_features(audio_mix_drift_tests PRIVATE cxx_std_17)
target_include_directories(audio_mix_drift_tests PRIVATE ../.. ${FFMPEG_INCLUDE_DIR})
target_link_libraries(audio_mix_drift_tests PRIVATE ${FFMPEG_LIBRARIES})
add_test(NAME audio_mix_drift COMMAND audio_mix_drift_tests)
set_tests_properties(audio_mix_drift PROPERTIES TIMEOUT 120 LABELS "audio;mix;drift;synthetic")
if(WIN32)
    set_tests_properties(audio_mix_drift PROPERTIES ENVIRONMENT "PATH=${_audio_filter_path}")
endif()
