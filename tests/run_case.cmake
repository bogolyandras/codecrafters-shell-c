# Runs one test case: feeds <CASE>.in to the shell on standard input and checks the output.
# Run as: cmake -DSHELL=<shell> -DCASE=<path without extension> -DWORK_DIR=<dir> -P run_case.cmake
#
# <CASE>.expect holds one expectation per line (blank lines and lines starting with # are ignored):
#   text                 the text appears somewhere in standard output or standard error
#   !text                the text does not appear
#   file:NAME:text       the file NAME, created in the working directory, contains the text
# Avoid `;`, `[` and `]` in expectations: CMake lists give them a meaning.
#
# It is plain CMake so that it runs unchanged on every platform, with nothing else installed.

cmake_minimum_required(VERSION 3.21)

foreach (required SHELL CASE WORK_DIR)
    if (NOT DEFINED ${required})
        message(FATAL_ERROR "run_case.cmake: -D${required}=... is required")
    endif ()
endforeach ()

# A fresh directory for every run, so that files of an earlier run cannot satisfy file: lines
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

execute_process(
        COMMAND "${SHELL}"
        WORKING_DIRECTORY "${WORK_DIR}"
        INPUT_FILE "${CASE}.in"
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
        RESULT_VARIABLE exit_status
        TIMEOUT 60)

set(everything "${standard_output}${standard_error}")
set(failures "")

# The shell ends at `exit` or at the end of its input: anything else is a crash (a sanitizer
# report ends the process with a non-zero status, as does a signal)
if (NOT exit_status STREQUAL "0")
    list(APPEND failures "the shell exited with '${exit_status}' instead of 0")
endif ()

file(STRINGS "${CASE}.expect" expectations)
foreach (expectation IN LISTS expectations)
    if (expectation STREQUAL "" OR expectation MATCHES "^#")
        continue()
    endif ()

    if (expectation MATCHES "^file:([^:]+):(.*)$")
        set(file_name "${CMAKE_MATCH_1}")
        set(wanted "${CMAKE_MATCH_2}")
        if (NOT EXISTS "${WORK_DIR}/${file_name}")
            list(APPEND failures "file '${file_name}' was not created")
        else ()
            file(READ "${WORK_DIR}/${file_name}" file_content)
            string(FIND "${file_content}" "${wanted}" position)
            if (position EQUAL -1)
                list(APPEND failures "file '${file_name}' does not contain '${wanted}'")
            endif ()
        endif ()
    elseif (expectation MATCHES "^!(.+)$")
        set(unwanted "${CMAKE_MATCH_1}")
        string(FIND "${everything}" "${unwanted}" position)
        if (NOT position EQUAL -1)
            list(APPEND failures "the output contains '${unwanted}'")
        endif ()
    else ()
        string(FIND "${everything}" "${expectation}" position)
        if (position EQUAL -1)
            list(APPEND failures "the output does not contain '${expectation}'")
        endif ()
    endif ()
endforeach ()

if (failures)
    list(JOIN failures "\n  " text)
    message(FATAL_ERROR "${CASE}:\n  ${text}\n--- output ---\n${everything}\n--------------")
endif ()
