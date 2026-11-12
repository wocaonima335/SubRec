# CI passes the same display version to CMake and Inno Setup. Local builds keep
# the current baseline version; Windows resources require numeric components.
set(SUBREC_VERSION "0.2.0-beta" CACHE STRING "SubRec display version (X.Y.Z[-prerelease])")
if(NOT SUBREC_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$")
    message(FATAL_ERROR "Invalid SUBREC_VERSION: expected X.Y.Z[-prerelease]")
endif()
set(SUBREC_VERSION_NUMERIC "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
set(SUBREC_VERSION_RESOURCE "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")
foreach(component IN ITEMS "${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}" "${CMAKE_MATCH_3}")
    if(component GREATER 65535)
        message(FATAL_ERROR "SUBREC_VERSION components must fit Windows VERSIONINFO (0..65535)")
    endif()
endforeach()
