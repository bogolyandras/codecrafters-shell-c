# The portable shell. Headers are included as "core/pstring.h", "core/shell_internal/tokenizer.h", ...
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS src/*.c)

# Usage requirements only: what a platform module needs to implement the interfaces in core/
add_library(shell_core_api INTERFACE)
add_library(shell::core_api ALIAS shell_core_api)
target_include_directories(shell_core_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# An object library: the core and the platform module call each other, which static libraries
# cannot link reliably, and the object files go straight into the executable (and the tests)
add_library(shell_core OBJECT ${CORE_SOURCES})
# shell::platform_config is defined by the platform module, which is added after this directory
target_link_libraries(shell_core PUBLIC shell::core_api shell::platform_config)

# The core must not include operating system headers: these empty-by-error stand-ins come first
# on its include path, so including one is a compile error. The platform modules do not get them.
target_include_directories(shell_core BEFORE PRIVATE ${PROJECT_SOURCE_DIR}/cmake/forbidden_headers)
