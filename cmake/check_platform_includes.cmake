cmake_minimum_required(VERSION 3.13) # Script mode: this also enables IN_LIST

# Checks which operating system headers each module includes, whatever system this runs on.
# Run as: cmake -DSOURCE_DIR=<repo>/src -P check_platform_includes.cmake
#
#   src/core   no operating system header at all (the compiler enforces it too, see
#              cmake/forbidden_headers, but only for the headers of the system it runs on)
#   src/posix  no Windows header
#   src/win32  no POSIX-only header

set(WINDOWS_HEADERS windows.h io.h direct.h process.h winsock2.h ws2tcpip.h shlobj.h)
set(POSIX_HEADERS unistd.h termios.h pwd.h dirent.h poll.h sys/wait.h sys/ioctl.h sys/select.h
                  sys/socket.h sys/mman.h)
# Headers of both worlds in a C runtime that has them, but that the core must still not use
set(SHARED_OS_HEADERS fcntl.h sys/stat.h)

function(check_module module forbidden)
    file(GLOB_RECURSE files "${SOURCE_DIR}/${module}/*.c" "${SOURCE_DIR}/${module}/*.h")
    foreach (file IN LISTS files)
        file(STRINGS "${file}" includes REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")
        foreach (line IN LISTS includes)
            string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"].*$" "\\1" header "${line}")
            if (header IN_LIST forbidden)
                file(RELATIVE_PATH shown "${SOURCE_DIR}" "${file}")
                list(APPEND violations "${shown}: includes <${header}>")
            endif ()
        endforeach ()
    endforeach ()
    set(violations "${violations}" PARENT_SCOPE)
endfunction()

set(violations "")
check_module(core "${WINDOWS_HEADERS};${POSIX_HEADERS};${SHARED_OS_HEADERS}")
check_module(posix "${WINDOWS_HEADERS}")
check_module(win32 "${POSIX_HEADERS}")

if (violations)
    list(JOIN violations "\n  " text)
    message(FATAL_ERROR "Operating system headers in the wrong module:\n  ${text}")
endif ()
