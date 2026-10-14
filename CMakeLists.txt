cmake_minimum_required(VERSION 3.13)

project(codecrafters-shell C)

file(GLOB_RECURSE SOURCE_FILES src/*.c src/*.h)

set(CMAKE_C_STANDARD 23) # Enable the C23 standard
# The code relies on GNU extensions (the cleanup attribute, statement expressions, __typeof__)
# and on POSIX declarations (sigaction, F_DUPFD_CLOEXEC), so keep the gnu23 dialect
set(CMAKE_C_EXTENSIONS ON)

add_executable(shell ${SOURCE_FILES})

# Sources include their headers as "pstring.h" or "datastructs/hashmap.h" from anywhere under src
target_include_directories(shell PRIVATE src)

if (WIN32)
    # The entry point is wmain, and the text is UTF-16 (see src/pstring.h)
    target_compile_definitions(shell PRIVATE UNICODE _UNICODE)
    target_link_options(shell PRIVATE -municode)
endif ()
