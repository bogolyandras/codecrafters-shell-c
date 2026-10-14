# Everything that differs between operating systems. The shared headers are in include/, and each
# system has a directory of the same shape with its sources and its two private-by-name headers
# (platform_config.h, pstring_platform.h), so no source file needs to test for the system.
if (WIN32)
    set(PLATFORM_DIR win32)
else ()
    set(PLATFORM_DIR posix)
endif ()

# Just the per system headers and definitions, so that shell_base can use them without a cycle
add_library(shell_platform_config INTERFACE)
target_include_directories(shell_platform_config INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/${PLATFORM_DIR}/include)
if (WIN32)
    # The entry point is wmain, and the text is UTF-16 (see src/base/pstring.h)
    target_compile_definitions(shell_platform_config INTERFACE UNICODE _UNICODE)
endif ()

file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS *.c ${PLATFORM_DIR}/*.c)

add_library(shell_platform STATIC ${PLATFORM_SOURCES})
target_include_directories(shell_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(shell_platform PUBLIC shell_base shell_platform_config)
