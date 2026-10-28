execute_process(COMMAND "${TEST_EXE}" "${TEST_MODE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 3)
if(NOT "${result}" STREQUAL "${EXPECTED_CODE}")
    message(FATAL_ERROR "expected exit ${EXPECTED_CODE}, got ${result}: ${output}\n${error}")
endif()
message(STATUS "PASS ${TEST_MODE}: exit=${result}: ${error}")
