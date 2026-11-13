# Apply PATCH to the fetched source FetchContent runs this in, once: a
# PATCH_COMMAND that is run again finds it already applied. See
# cmake/patches/.
execute_process(COMMAND git apply --reverse --check "${PATCH}" RESULT_VARIABLE not_applied
    OUTPUT_QUIET ERROR_QUIET)
if(not_applied)
    execute_process(COMMAND git apply "${PATCH}" RESULT_VARIABLE failed)
    if(failed)
        message(FATAL_ERROR "Could not apply ${PATCH}")
    endif()
endif()
