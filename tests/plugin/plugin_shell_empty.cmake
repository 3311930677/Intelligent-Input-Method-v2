if(NOT DEFINED SHELL OR NOT DEFINED ROOT)
    message(FATAL_ERROR "SHELL and ROOT are required")
endif()
if(EXISTS "${ROOT}")
    message(FATAL_ERROR "plugin shell empty-store fixture unexpectedly exists: ${ROOT}")
endif()
execute_process(
    COMMAND "${SHELL}" "${ROOT}" list
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE diagnostic
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "plugin shell list failed: ${diagnostic}")
endif()
string(JSON schema ERROR_VARIABLE json_error GET "${output}" schema_version)
if(json_error OR NOT schema EQUAL 1)
    message(FATAL_ERROR "plugin shell schema is invalid: ${output}")
endif()
string(JSON plugin_count LENGTH "${output}" plugins)
string(JSON recovery_count LENGTH "${output}" recovery)
if(NOT plugin_count EQUAL 0 OR NOT recovery_count EQUAL 0)
    message(FATAL_ERROR "empty plugin store did not return empty arrays: ${output}")
endif()
execute_process(
    COMMAND "${SHELL}" "${ROOT}" cleanup 0 retained_staging "${ROOT}/missing" "" ""
    RESULT_VARIABLE invalid_result
)
if(invalid_result EQUAL 0)
    message(FATAL_ERROR "invalid recovery selection unexpectedly succeeded")
endif()
