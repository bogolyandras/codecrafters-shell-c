# Portable helpers. Needs the platform's PChar and path constants (shell_platform_config)
file(GLOB BASE_SOURCES CONFIGURE_DEPENDS *.c datastructs/*.c)

add_library(shell_base STATIC ${BASE_SOURCES})

# Headers are included as "pstring.h" or "datastructs/hashmap.h"
target_include_directories(shell_base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(shell_base PUBLIC shell_platform_config)
