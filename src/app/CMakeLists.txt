# The program: the read-eval loop and the entry point of the platform
if (WIN32)
    set(ENTRY_POINT main_win32.c)
else ()
    set(ENTRY_POINT main_posix.c)
endif ()

add_executable(shell shell.c ${ENTRY_POINT})
target_link_libraries(shell PRIVATE shell_core)

if (WIN32)
    target_link_options(shell PRIVATE -municode)
endif ()

# Keep the binary at <build dir>/shell, where the CodeCrafters runner and CI expect it
set_target_properties(shell PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
