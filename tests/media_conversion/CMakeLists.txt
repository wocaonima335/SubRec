cmake_minimum_required(VERSION 3.21)
project(media_conversion_tests LANGUAGES CXX)
enable_testing()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/SubRecFFmpeg.cmake")
subrec_require_ffmpeg_root()
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../capture/media" capture-media)
add_executable(media_conversion_tests media_conversion_tests.cpp)
target_link_libraries(media_conversion_tests PRIVATE capture_media_conversion)
target_compile_features(media_conversion_tests PRIVATE cxx_std_17)
set_target_properties(media_conversion_tests PROPERTIES CXX_EXTENSIONS OFF)
if(MINGW)
    target_compile_options(media_conversion_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
# Test-only runtime copies in the build tree, never an install/deployment step.
foreach(dll IN ITEMS avutil-60 swscale-9 swresample-6)
    add_custom_command(TARGET media_conversion_tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${FFMPEG_ROOT}/bin/${dll}.dll" "$<TARGET_FILE_DIR:media_conversion_tests>"
        VERBATIM)
endforeach()
add_test(NAME media_conversion COMMAND media_conversion_tests)
set_tests_properties(media_conversion PROPERTIES TIMEOUT 30)
