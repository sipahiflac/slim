# Applies PATCH to the working directory unless it is already applied, so
# re-running FetchContent's patch step stays harmless.
execute_process(COMMAND "${GIT}" apply --reverse --check "${PATCH}"
                RESULT_VARIABLE alreadyApplied OUTPUT_QUIET ERROR_QUIET)
if(NOT alreadyApplied EQUAL 0)
    execute_process(COMMAND "${GIT}" apply "${PATCH}" RESULT_VARIABLE failed)
    if(failed)
        message(FATAL_ERROR "Could not apply ${PATCH}")
    endif()
endif()
