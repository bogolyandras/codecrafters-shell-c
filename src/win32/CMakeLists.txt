# The Windows platform module: implements the interfaces declared in core/, and has wmain()

# The two headers the core receives from the platform (PChar and the path separators), and nothing else
add_library(shell_platform_config INTERFACE)
# Alias: a misspelt `shell::` name is a configure error, not a silent `-l` link flag
add_library(shell::platform_config ALIAS shell_platform_config)
target_include_directories(shell_platform_config INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The entry point is wmain, and the text is UTF-16 (see core/pstring.h)
target_compile_definitions(shell_platform_config INTERFACE UNICODE _UNICODE)

# The implementation, without wmain() so that the tests can link it. The entry point is in app/,
# which this (non recursive) glob does not see.
file(GLOB WIN32_SOURCES CONFIGURE_DEPENDS src/*.c)
add_library(shell_win32 OBJECT ${WIN32_SOURCES})
target_link_libraries(shell_win32 PUBLIC shell::core_api shell::platform_config)

add_executable(shell app/main_win32.c)
target_link_libraries(shell PRIVATE shell_core shell_win32)
# MinGW needs -municode to start at wmain. With the MSVC runtime (clang or clang-cl targeting
# Windows MSVC) the entry point is chosen from wmain by the C runtime, and the flag is unknown.
if (MINGW)
    target_link_options(shell PRIVATE -municode)
endif ()

# Keep the binary at <build dir>/shell(.exe), where CI expects it
set_target_properties(shell PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
