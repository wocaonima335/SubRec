# No execution required, so this check also works on cross-built binaries.
file(STRINGS "${BINARY}" hook_names REGEX "SUBREC_TEST_VIDEO_|\\[TestHook\\]")
if(hook_names)
    message(FATAL_ERROR "Stripped binary contains video startup hooks: ${hook_names}")
endif()
message(STATUS "No video startup hook names or logs in ${BINARY}")
