if(NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT OR NOT DEFINED VERSION)
    message(FATAL_ERROR "WriteBuildInfo.cmake requires SOURCE_DIR, OUTPUT, and VERSION")
endif()

if(NOT GIT_EXECUTABLE)
    find_package(Git QUIET)
endif()

set(hash "unknown")
set(dirty "")
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE hash
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE git_result
    )
    if(NOT git_result EQUAL 0 OR hash STREQUAL "")
        set(hash "unknown")
    else()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
            WORKING_DIRECTORY "${SOURCE_DIR}"
            OUTPUT_VARIABLE status_text
            ERROR_QUIET
            RESULT_VARIABLE status_result
        )
        if(status_result EQUAL 0 AND NOT status_text STREQUAL "")
            set(dirty ".dirty")
        endif()
    endif()
endif()

set(content "#pragma once\ninline constexpr const char* kSmcVersion = \"${VERSION}+${hash}${dirty}\";\n")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" current)
    if(current STREQUAL content)
        return()
    endif()
endif()

get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(WRITE "${OUTPUT}" "${content}")
