# No execution required, so this check also works on cross-built binaries.
file(STRINGS "${BINARY}" hook_names REGEX "SUBREC_TEST_(VIDEO_|MUX_)|\\[TestHook\\]")
if(hook_names)
    message(FATAL_ERROR "Stripped binary contains test hooks: ${hook_names}")
endif()
message(STATUS "No test hook names or logs in ${BINARY}")
