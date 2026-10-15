# Behaviour tests. They run the built shell and look at what it prints, so they cover the
# portable core and the platform module together. The Python tests are kept under legacy/ until
# the tests are redesigned.
#
#   cases/common/*.in|.expect   every platform: builtins and shell syntax only
#   cases/posix/*.in|.expect    POSIX only: they use programs such as cat and sleep
#   cases/win32/*.in|.expect    Windows only: they use programs such as cmd and findstr
#   legacy/pty_editor.py        POSIX only, Python: the line editor and TAB completion through a pseudo terminal
#   legacy/codecrafters/        POSIX only, Python: the 76 CodeCrafters stages, reconstructed (see AGENTS.md)
#
# See run_case.cmake for the format of a case. Run them with `ctest --test-dir <build dir>`.

function(add_shell_cases directory prefix)
    file(GLOB inputs CONFIGURE_DEPENDS "${directory}/*.in")
    foreach (input IN LISTS inputs)
        get_filename_component(name "${input}" NAME_WE)
        get_filename_component(case_path "${input}" DIRECTORY)
        add_test(NAME "${prefix}.${name}"
                COMMAND ${CMAKE_COMMAND}
                        -DSHELL=$<TARGET_FILE:shell>
                        -DCASE=${case_path}/${name}
                        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/work/${prefix}.${name}
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)
    endforeach ()
endfunction()

add_shell_cases(${CMAKE_CURRENT_SOURCE_DIR}/cases/common common)

if (WIN32)
    add_shell_cases(${CMAKE_CURRENT_SOURCE_DIR}/cases/win32 win32)
endif ()

if (NOT WIN32)
    add_shell_cases(${CMAKE_CURRENT_SOURCE_DIR}/cases/posix posix)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if (Python3_Interpreter_FOUND)
        # -I: isolated mode, so that a stray module in the working directory cannot shadow the standard ones
        add_test(NAME posix.pty_editor
                COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/legacy/pty_editor.py $<TARGET_FILE:shell>)

        # The CodeCrafters stages, reconstructed (see legacy/codecrafters/run.py): one test per stage, so a
        # failure names the stage. The list comes from the stages themselves
        set(CC_RUNNER ${CMAKE_CURRENT_SOURCE_DIR}/legacy/codecrafters/run.py)
        execute_process(COMMAND ${Python3_EXECUTABLE} -I ${CC_RUNNER} --list
                OUTPUT_VARIABLE CC_STAGE_LIST RESULT_VARIABLE CC_LIST_RESULT)
        if (CC_LIST_RESULT EQUAL 0)
            string(REPLACE "\n" ";" CC_STAGE_LINES "${CC_STAGE_LIST}")
            foreach (line IN LISTS CC_STAGE_LINES)
                if (line MATCHES "^([a-z0-9]+)\t(.*)$")
                    set(slug "${CMAKE_MATCH_1}")
                    add_test(NAME codecrafters.${slug}
                            COMMAND ${Python3_EXECUTABLE} -I ${CC_RUNNER} $<TARGET_FILE:shell> --stage ${slug})
                    # 77 is what the runner exits with when a stage cannot run here (no C compiler)
                    set_tests_properties(codecrafters.${slug} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
                endif ()
            endforeach ()
        else ()
            message(WARNING "Could not list the CodeCrafters stages: they are not registered")
        endif ()
    else ()
        message(STATUS "Python 3 not found: the pseudo terminal tests are not registered")
    endif ()
endif ()
