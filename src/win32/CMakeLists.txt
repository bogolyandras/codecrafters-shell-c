# The Windows platform module: implements the interfaces declared in core/, and has wmain()

# The two headers the core receives from the platform (PChar and the path separators), and nothing else
add_library(shell_platform_config INTERFACE)
target_include_directories(shell_platform_config INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The entry point is wmain, and the text is UTF-16 (see core/pstring.h)
target_compile_definitions(shell_platform_config INTERFACE UNICODE _UNICODE)

file(GLOB WIN32_SOURCES CONFIGURE_DEPENDS src/*.c)
list(REMOVE_ITEM WIN32_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main_win32.c)

# Without wmain(), so the tests can link it
add_library(shell_win32 OBJECT ${WIN32_SOURCES})
target_link_libraries(shell_win32 PUBLIC shell_core_api shell_platform_config)

add_executable(shell src/main_win32.c)
target_link_libraries(shell PRIVATE shell_core shell_win32)
target_link_options(shell PRIVATE -municode)

# Keep the binary at <build dir>/shell(.exe), where CI expects it
set_target_properties(shell PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
