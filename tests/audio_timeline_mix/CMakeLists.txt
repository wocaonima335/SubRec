cmake_minimum_required(VERSION 3.21)
project(audio_timeline_mix_tests LANGUAGES CXX)
enable_testing()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/SubRecFFmpeg.cmake")
subrec_require_ffmpeg_root()
add_executable(audio_timeline_mix_tests audio_timeline_mix_tests.cpp ../../filter/audio_timeline_mixer.cpp ../../filter/audio_mix_dump.cpp)
target_compile_features(audio_timeline_mix_tests PRIVATE cxx_std_17)
target_include_directories(audio_timeline_mix_tests PRIVATE ../.. "${FFMPEG_ROOT}/include")
target_link_libraries(audio_timeline_mix_tests PRIVATE
    "${FFMPEG_ROOT}/lib/avfilter.lib" "${FFMPEG_ROOT}/lib/avutil.lib" "${FFMPEG_ROOT}/lib/swresample.lib")
if(WIN32)
    file(GLOB runtime_dlls "${FFMPEG_ROOT}/bin/*.dll")
    add_custom_command(TARGET audio_timeline_mix_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${runtime_dlls} $<TARGET_FILE_DIR:audio_timeline_mix_tests>)
endif()
add_test(NAME audio_timeline_mix COMMAND audio_timeline_mix_tests)
set_tests_properties(audio_timeline_mix PROPERTIES TIMEOUT 30)

# AAC round-trip uses the same production mixer and native encoder settings.
add_executable(audio_mix_aac_tests audio_mix_aac_tests.cpp ../../filter/audio_timeline_mixer.cpp ../../filter/audio_mix_dump.cpp)
target_compile_features(audio_mix_aac_tests PRIVATE cxx_std_17)
target_include_directories(audio_mix_aac_tests PRIVATE ../.. "${FFMPEG_ROOT}/include")
target_link_libraries(audio_mix_aac_tests PRIVATE
    "${FFMPEG_ROOT}/lib/avfilter.lib" "${FFMPEG_ROOT}/lib/avcodec.lib"
    "${FFMPEG_ROOT}/lib/avutil.lib" "${FFMPEG_ROOT}/lib/swresample.lib")
if(WIN32)
    add_custom_command(TARGET audio_mix_aac_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${runtime_dlls} $<TARGET_FILE_DIR:audio_mix_aac_tests>)
endif()
add_test(NAME audio_mix_aac COMMAND audio_mix_aac_tests)
set_tests_properties(audio_mix_aac PROPERTIES TIMEOUT 60 LABELS "audio;mix;aac;synthetic")
