cmake_minimum_required(VERSION 3.16)
project(native_capture_probe LANGUAGES CXX)

if(NOT WIN32 OR NOT MINGW)
    message(FATAL_ERROR "This standalone probe requires the Windows MinGW kit.")
endif()

add_executable(native_capture_probe main.cpp)
target_compile_features(native_capture_probe PRIVATE cxx_std_17)
target_compile_definitions(native_capture_probe PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0602)
target_compile_options(native_capture_probe PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(native_capture_probe PRIVATE ole32 uuid propsys d3d11 dxgi)
# Use the kit's runtime via PATH. No Qt, FFmpeg, deployment or DLL copying.
