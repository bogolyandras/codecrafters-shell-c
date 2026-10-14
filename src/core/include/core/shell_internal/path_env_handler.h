#ifndef BOGOLYANDRAS_PATH_ENV_VARIABLE_PARSER_H
#define BOGOLYANDRAS_PATH_ENV_VARIABLE_PARSER_H

#include "core/datastructs/hashmap.h"
#include "core/str_vec.h"

HASHMAP_DEFINE(PChar *, PathMap)

/**
 * Both return heap allocated objects the caller owns; release with the matching destroy function.
 * They return NULL with errno set on failure. A missing PATH is not an error: it yields no executables.
 */
StrVec* get_paths(void);
PathMap* get_executable_to_path_map(const StrVec* paths);

/**
 * Finds an executable by the name typed by the user. On Windows the lookup is case insensitive and
 * the extension may be left out (`notepad` finds `notepad.exe`). Returns the full path, owned by
 * the map, or NULL if there is no such executable.
 */
const PChar *find_executable(const PathMap *map, const PChar *name);

/** Cleanup-attribute friendly: take a pointer to the owning variable, NULL-safe */
void destroy_paths(StrVec **paths);
void destroy_executable_to_path_map(PathMap **map);

#endif //BOGOLYANDRAS_PATH_ENV_VARIABLE_PARSER_H