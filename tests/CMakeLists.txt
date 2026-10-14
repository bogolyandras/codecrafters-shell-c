# Behaviour tests. They run the built shell and look at what it prints, so they cover the
# portable core and the platform module together.
#
#   cases/common/*.in|.expect   every platform: builtins and shell syntax only
#   cases/posix/*.in|.expect    POSIX only: they use programs such as cat and sleep
#   pty_editor.py               POSIX only: the line editor and TAB completion through a pseudo terminal
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

if (NOT WIN32)
    add_shell_cases(${CMAKE_CURRENT_SOURCE_DIR}/cases/posix posix)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if (Python3_Interpreter_FOUND)
        # -I: isolated mode, so that a stray module in the working directory cannot shadow the standard ones
        add_test(NAME posix.pty_editor
                COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/pty_editor.py $<TARGET_FILE:shell>)
    else ()
        message(STATUS "Python 3 not found: the pseudo terminal tests are not registered")
    endif ()
endif ()
