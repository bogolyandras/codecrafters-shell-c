cmake_minimum_required(VERSION 3.13)

project(codecrafters-shell C)

set(CMAKE_C_STANDARD 23) # Enable the C23 standard
# The code relies on GNU extensions (the cleanup attribute, statement expressions, __typeof__)
# and on POSIX declarations (sigaction, F_DUPFD_CLOEXEC), so keep the gnu23 dialect
set(CMAKE_C_EXTENSIONS ON)

# Modules:
#   src/core   everything portable, including the interfaces to the operating system
#              (core/platform.h, terminal.h, redirection.h, execution_handler.h). It includes
#              no operating system header, and the compiler is made to enforce that.
#   src/posix  implements those interfaces with POSIX, and has main()
#   src/win32  implements them with the Win32 API, and has wmain()
# Exactly one of the two platform modules is built. No source file tests for the system.
add_subdirectory(src/core)
if (WIN32)
    add_subdirectory(src/win32)
else ()
    add_subdirectory(src/posix)
endif ()

# Fails the build when an operating system header is included in a module it does not belong to
add_custom_target(check_platform_includes ALL
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/src
                -P ${CMAKE_SOURCE_DIR}/cmake/check_platform_includes.cmake
        COMMENT "Checking which operating system headers each module includes"
        VERBATIM)

# Room for the tests: add_subdirectory(tests) here, linking shell_core and the platform module
# (shell_posix / shell_win32), which are libraries without main()
