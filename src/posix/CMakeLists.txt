# The POSIX platform module: implements the interfaces declared in core/, and has main()

# The two headers the core receives from the platform (PChar and the path separators), and nothing else
add_library(shell_platform_config INTERFACE)
target_include_directories(shell_platform_config INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

file(GLOB POSIX_SOURCES CONFIGURE_DEPENDS src/*.c)
list(REMOVE_ITEM POSIX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main_posix.c)

# Without main(), so the tests can link it
add_library(shell_posix OBJECT ${POSIX_SOURCES})
target_link_libraries(shell_posix PUBLIC shell_core_api shell_platform_config)

add_executable(shell src/main_posix.c)
target_link_libraries(shell PRIVATE shell_core shell_posix)

# Keep the binary at <build dir>/shell, where the CodeCrafters runner and CI expect it
set_target_properties(shell PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
