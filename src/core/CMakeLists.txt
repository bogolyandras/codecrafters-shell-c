# The shell logic. Headers are included as "shell_internal/tokenizer.h" or "shell_builtin/history.h"
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS *.c)

add_library(shell_core STATIC ${CORE_SOURCES})
target_include_directories(shell_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(shell_core PUBLIC shell_platform)
