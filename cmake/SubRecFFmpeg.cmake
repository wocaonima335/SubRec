# Shared FFmpeg root resolution for SubRec and its tests.
#
# FFmpeg is NOT bundled in this repository. Provide a prebuilt shared FFmpeg
# (with include/, lib/ and bin/ directories) through either:
#   -DFFMPEG_ROOT=<path>            (CMake option), or
#   SUBREC_FFMPEG_ROOT=<path>       (environment variable)
# See THIRD_PARTY_LICENSES.md for license requirements of the FFmpeg build.

macro(subrec_require_ffmpeg_root)
    set(FFMPEG_ROOT "" CACHE PATH "FFmpeg root directory (contains include/, lib/, bin/)")

    if(NOT FFMPEG_ROOT AND DEFINED ENV{SUBREC_FFMPEG_ROOT})
        set(FFMPEG_ROOT "$ENV{SUBREC_FFMPEG_ROOT}" CACHE PATH
            "FFmpeg root directory (contains include/, lib/, bin/)" FORCE)
    endif()

    if(NOT FFMPEG_ROOT)
        message(FATAL_ERROR
            "FFMPEG_ROOT is not set.\n"
            "SubRec does not bundle FFmpeg. Download a shared FFmpeg build "
            "(with include/, lib/ and bin/) and pass it to CMake:\n"
            "  cmake -S . -B build -DFFMPEG_ROOT=<path-to-ffmpeg>\n"
            "or set the SUBREC_FFMPEG_ROOT environment variable.\n"
            "See README.md (Building) for details.")
    endif()

    get_filename_component(FFMPEG_ROOT "${FFMPEG_ROOT}" ABSOLUTE)

    foreach(_subrec_ffmpeg_dir "${FFMPEG_ROOT}/include" "${FFMPEG_ROOT}/lib")
        if(NOT EXISTS "${_subrec_ffmpeg_dir}")
            message(FATAL_ERROR
                "FFMPEG_ROOT is set to '${FFMPEG_ROOT}' but '${_subrec_ffmpeg_dir}' does not exist.\n"
                "FFMPEG_ROOT must contain include/, lib/ and bin/ of a shared FFmpeg build.")
        endif()
    endforeach()
    unset(_subrec_ffmpeg_dir)
endmacro()
