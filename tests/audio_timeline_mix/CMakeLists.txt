cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(audio_timeline_mix_tests LANGUAGES CXX)
    enable_testing()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/SubRecFFmpeg.cmake")
subrec_require_ffmpeg_root()
add_executable(audio_timeline_mix_tests audio_timeline_mix_tests.cpp ../../queue/media_clock.cpp ../../filter/audio_timeline_mixer.cpp ../../filter/audio_mix_dump.cpp)
target_compile_features(audio_timeline_mix_tests PRIVATE cxx_std_17)
target_include_directories(audio_timeline_mix_tests PRIVATE ../.. "${FFMPEG_ROOT}/include")
target_link_libraries(audio_timeline_mix_tests PRIVATE
    "${FFMPEG_ROOT}/lib/avfilter.lib" "${FFMPEG_ROOT}/lib/avutil.lib" "${FFMPEG_ROOT}/lib/swresample.lib")
# Test PATH is populated by subrec_configure_tests(). Do not add per-target
# POST_BUILD DLL copies here: parallel Windows builds raced while several
# targets copied the same FFmpeg DLLs into one directory.
add_test(NAME audio_timeline_mix COMMAND audio_timeline_mix_tests)
set_property(TEST audio_timeline_mix APPEND PROPERTY LABELS audio mix synthetic)
set_tests_properties(audio_timeline_mix PROPERTIES TIMEOUT 30)

# AAC round-trip uses the same production mixer and native encoder settings.
add_executable(audio_mix_aac_tests audio_mix_aac_tests.cpp ../../queue/media_clock.cpp ../../filter/audio_timeline_mixer.cpp ../../filter/audio_mix_dump.cpp)
target_compile_features(audio_mix_aac_tests PRIVATE cxx_std_17)
target_include_directories(audio_mix_aac_tests PRIVATE ../.. "${FFMPEG_ROOT}/include")
target_link_libraries(audio_mix_aac_tests PRIVATE
    "${FFMPEG_ROOT}/lib/avfilter.lib" "${FFMPEG_ROOT}/lib/avcodec.lib"
    "${FFMPEG_ROOT}/lib/avutil.lib" "${FFMPEG_ROOT}/lib/swresample.lib")
add_test(NAME audio_mix_aac COMMAND audio_mix_aac_tests)
set_tests_properties(audio_mix_aac PROPERTIES TIMEOUT 60 LABELS "audio;mix;aac;synthetic")

add_executable(audio_mix_drift_tests audio_mix_drift_tests.cpp ../../queue/media_clock.cpp ../../filter/audio_timeline_mixer.cpp ../../filter/audio_mix_dump.cpp)
target_compile_features(audio_mix_drift_tests PRIVATE cxx_std_17)
target_include_directories(audio_mix_drift_tests PRIVATE ../.. "${FFMPEG_ROOT}/include")
target_link_libraries(audio_mix_drift_tests PRIVATE
    "${FFMPEG_ROOT}/lib/avfilter.lib" "${FFMPEG_ROOT}/lib/avutil.lib" "${FFMPEG_ROOT}/lib/swresample.lib")
add_test(NAME audio_mix_drift COMMAND audio_mix_drift_tests)
set_tests_properties(audio_mix_drift PROPERTIES TIMEOUT 120 LABELS "audio;mix;drift;synthetic")

add_executable(audio_single_clock_tests audio_single_clock_tests.cpp ../../queue/media_clock.cpp ../../queue/video_tick_scheduler.cpp
    ../../filter/audio_timeline_mixer.cpp ../../filter/audio_mix_dump.cpp)
target_compile_features(audio_single_clock_tests PRIVATE cxx_std_17)
target_include_directories(audio_single_clock_tests PRIVATE ../.. "${FFMPEG_ROOT}/include")
target_link_libraries(audio_single_clock_tests PRIVATE
    "${FFMPEG_ROOT}/lib/avfilter.lib" "${FFMPEG_ROOT}/lib/avutil.lib" "${FFMPEG_ROOT}/lib/swresample.lib")
add_test(NAME audio_video_resume_beacons COMMAND audio_single_clock_tests resume_beacons)
set_tests_properties(audio_video_resume_beacons PROPERTIES TIMEOUT 30 LABELS "ci;audio;clock;regression")
add_test(NAME audio_single_source_clock COMMAND audio_single_clock_tests)
set_tests_properties(audio_single_source_clock PROPERTIES TIMEOUT 30 LABELS "audio;mix;clock;synthetic")

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/SubRecTests.cmake")
subrec_configure_tests()
# Native AAC decoding is test-only; the production encoder is always present.
set_property(TEST audio_mix_aac APPEND PROPERTY LABELS needs_ffmpeg_test_components)
