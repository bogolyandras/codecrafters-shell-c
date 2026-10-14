#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell_internal/path_env_handler.h"
#include "common.h"
#include "platform.h"
#include "raii.h"

void destroy_paths(StrVec **paths) {
    if (*paths == NULL) {
        return;
    }

    StrVec_free_owned(*paths);
    free(*paths);
    *paths = NULL;
}

void destroy_executable_to_path_map(PathMap **map) {
    if (*map == NULL) {
        return;
    }

    PathMap_free(*map);
    free(*map);
    *map = NULL;
}

StrVec* get_paths(void) {
    // Zeroed so destroy_paths is safe even if StrVec_init fails
    CLEANUP(destroy_paths) StrVec *paths = calloc(1, sizeof(StrVec));
    if (paths == NULL) {
        return NULL;
    }
    if (StrVec_init(paths) != 0) {
        return NULL;
    }

    const PChar *path_environment_variable = p_getenv(P("PATH"));
    if (path_environment_variable == NULL) {
        return TAKE(paths); // No PATH means no external commands, not an error
    }

    // Split on the separator. An empty entry means "the current directory" in a PATH, but the
    // shell skips it, like the executables of a missing directory
    const PChar *entry = path_environment_variable;
    while (1) {
        const PChar *separator = p_strchr(entry, PATH_LIST_SEPARATOR);
        const size_t entry_length = separator != NULL ? (size_t)(separator - entry) : p_strlen(entry);

        if (entry_length > 0) {
            AUTO_FREE PChar *path_dup = malloc((entry_length + 1) * sizeof(PChar));
            if (path_dup == NULL) {
                return NULL;
            }
            p_memcpy(path_dup, entry, entry_length);
            path_dup[entry_length] = '\0';

            if (StrVec_push(paths, path_dup) != 0) {
                return NULL;
            }
            DISOWN(path_dup); // now owned by paths
        }

        if (separator == NULL) {
            break;
        }
        entry = separator + 1;
    }

    return TAKE(paths);
}

static void free_string_value(void *value) {
    free(*(PChar **)value);
}

PathMap* get_executable_to_path_map(const StrVec* paths) {
    // Zeroed so destroy_executable_to_path_map is safe even if PathMap_init fails
    CLEANUP(destroy_executable_to_path_map) PathMap* executable_to_path_map = calloc(1, sizeof(PathMap));
    if (executable_to_path_map == NULL) {
        return NULL;
    }
    if (PathMap_init(executable_to_path_map, 128, free_string_value) != 0) {
        return NULL;
    }

    for (size_t i = 0; i < StrVec_size(paths); i++) {
        const PChar* directory_path = *StrVec_get_const(paths, i);
        AUTO_CLOSE_DIR PlatformDir *dir = platform_dir_open(directory_path);

        if (dir == NULL) {
            continue; // Unreadable or missing PATH entries are skipped, like other shells do
        }

        const PChar *name;
        int is_directory;
        while (platform_dir_read(dir, &name, &is_directory) == 1) {
            if (is_directory) {
                continue;
            }

            PChar full_path[PATH_BUFFER_SIZE];
            const int full_path_length = p_snprintf(full_path, PATH_BUFFER_SIZE, P("%" PS "%c%" PS),
                                                    directory_path, PATH_SEPARATOR, name);
            if (full_path_length < 0 || (size_t)full_path_length >= PATH_BUFFER_SIZE) {
                continue; // Path too long to be used
            }

            if (!platform_is_executable_file(full_path)) {
                continue;
            }

            AUTO_FREE PChar *key = p_strdup(name);
            if (key == NULL) {
                return NULL;
            }
            platform_normalize_command_name(key);

            if (PathMap_get(executable_to_path_map, key) != NULL) {
                continue; // An earlier PATH entry wins
            }

            AUTO_FREE PChar *full_path_dup = p_strdup(full_path);
            if (full_path_dup == NULL) {
                return NULL;
            }
            if (PathMap_put(executable_to_path_map, key, full_path_dup) != 0) {
                return NULL;
            }
            DISOWN(full_path_dup); // now owned by the map
        }
    }

    return TAKE(executable_to_path_map);
}

const PChar *find_executable(const PathMap *map, const PChar *name) {
    size_t longest_extension = 0;
    for (const PChar *const *extension = platform_executable_extensions; *extension != NULL; extension++) {
        const size_t length = p_strlen(*extension);
        if (length > longest_extension) {
            longest_extension = length;
        }
    }

    const size_t name_length = p_strlen(name);
    AUTO_FREE PChar *key = malloc((name_length + longest_extension + 1) * sizeof(PChar));
    if (key == NULL) {
        return NULL;
    }
    p_memcpy(key, name, name_length + 1);
    platform_normalize_command_name(key);

    const PChar *const *found = PathMap_get_const(map, key);
    if (found != NULL) {
        return *found;
    }

    for (const PChar *const *extension = platform_executable_extensions; *extension != NULL; extension++) {
        p_memcpy(key + name_length, *extension, p_strlen(*extension) + 1);

        found = PathMap_get_const(map, key);
        if (found != NULL) {
            return *found;
        }
    }

    return NULL;
}
