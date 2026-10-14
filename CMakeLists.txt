cmake_minimum_required(VERSION 3.13)

project(codecrafters-shell C)

set(CMAKE_C_STANDARD 23) # Enable the C23 standard
# The code relies on GNU extensions (the cleanup attribute, statement expressions, __typeof__)
# and on POSIX declarations (sigaction, F_DUPFD_CLOEXEC), so keep the gnu23 dialect
set(CMAKE_C_EXTENSIONS ON)

# Layered, each library only depends on the ones above it:
#   base      portable helpers (strings, containers, RAII)
#   platform  everything that differs between POSIX and Windows, behind one set of headers
#   core      the shell logic: parsing, builtins, line editor, completion
#   app       the read-eval loop and the entry point
# The operating system is chosen here and in src/platform: no source file tests for it.
add_subdirectory(src/base)
add_subdirectory(src/platform)
add_subdirectory(src/core)
add_subdirectory(src/app)
