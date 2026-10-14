cmake_minimum_required(VERSION 3.21) # 3.21 is the first to know CMAKE_C_STANDARD 23

project(codecrafters-shell C)

set(CMAKE_C_STANDARD 23) # Enable the C23 standard
set(CMAKE_C_STANDARD_REQUIRED ON) # An older compiler is an error, not a silent fall back
# The code relies on GNU extensions (the cleanup attribute, statement expressions, __typeof__)
# and on POSIX declarations (sigaction, F_DUPFD_CLOEXEC), so keep the gnu23 dialect
set(CMAKE_C_EXTENSIONS ON)

# Fails the build when an operating system header is included in a module it does not belong to.
# Defined before the modules, which make their compilation depend on it.
add_custom_target(check_platform_includes ALL
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}/src
                -P ${PROJECT_SOURCE_DIR}/cmake/check_platform_includes.cmake
        COMMENT "Checking which operating system headers each module includes"
        VERBATIM)

# Modules:
#   src/core   everything portable, including the interfaces to the operating system
#              (core/platform.h, terminal.h, redirection.h, execution_handler.h). It includes
#              no operating system header, and the compiler is made to enforce that.
#   src/posix  implements those interfaces with POSIX, and has main() in app/
#   src/win32  implements them with the Win32 API, and has wmain() in app/
# Exactly one of the two platform modules is built. No source file tests for the system.
#
# Targets are used through their namespaced aliases (shell::core_api, shell::platform_config):
# a misspelt `shell::` name is a configure error, where a plain name would become a `-l` flag.
add_subdirectory(src/core)
if (WIN32)
    add_subdirectory(src/win32)
else ()
    add_subdirectory(src/posix)
endif ()

# No object file is compiled before the include check has passed, also when only one target is built
add_dependencies(shell_core check_platform_includes)

# Room for the tests: add_subdirectory(tests) here, linking shell_core and the platform module
# (shell_posix / shell_win32), which are libraries without main()
