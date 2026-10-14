#include "core/platform.h"

#include <dirent.h>
#include <pwd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/common.h"

struct PlatformDir {
    DIR *dir;
    PChar *path; /** Kept to resolve entries whose type the directory does not tell */
};

const PChar *const platform_executable_extensions[] = {NULL};

void platform_init(void) {
    // Flush after every printf
    setbuf(stdout, NULL);
}

PlatformDir *platform_dir_open(const PChar *path) {
    PlatformDir *dir = calloc(1, sizeof(PlatformDir));
    if (dir == NULL) {
        return NULL;
    }

    dir->dir = opendir(path);
    if (dir->dir == NULL) {
        const int saved_errno = errno;
        free(dir);
        errno = saved_errno;
        return NULL;
    }

    dir->path = p_strdup(path);
    if (dir->path == NULL) {
        const int saved_errno = errno;
        closedir(dir->dir);
        free(dir);
        errno = saved_errno;
        return NULL;
    }

    return dir;
}

int platform_dir_read(PlatformDir *dir, const PChar **name, int *is_directory) {
    while (1) {
        errno = 0; // readdir only sets errno on failure, and returns NULL for the end too
        const struct dirent *entry = readdir(dir->dir);
        if (entry == NULL) {
            return errno != 0 ? -1 : 0;
        }

        if (p_strcmp(entry->d_name, P(".")) == 0 || p_strcmp(entry->d_name, P("..")) == 0) {
            continue;
        }

        int directory = entry->d_type == DT_DIR;
        if (entry->d_type == DT_LNK || entry->d_type == DT_UNKNOWN) {
            PChar full_path[PATH_BUFFER_SIZE];
            struct stat st;
            const int length = p_snprintf(full_path, PATH_BUFFER_SIZE, P("%" PS "/%" PS), dir->path, entry->d_name);
            directory = length > 0 && (size_t)length < PATH_BUFFER_SIZE &&
                        stat(full_path, &st) == 0 && S_ISDIR(st.st_mode);
        }

        *name = entry->d_name;
        *is_directory = directory;
        return 1;
    }
}

void platform_dir_close(PlatformDir *dir) {
    closedir(dir->dir);
    free(dir->path);
    free(dir);
}

int platform_is_executable_file(const PChar *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

void platform_normalize_command_name(PChar *name) {
    (void)name;
}

int platform_is_path_separator(PChar c) {
    return c == '/';
}


const PChar *platform_default_home_dir(void) {
    const struct passwd *pw = getpwuid(getuid());
    return pw != NULL && pw->pw_dir != NULL && pw->pw_dir[0] != '\0' ? pw->pw_dir : NULL;
}

int platform_backslash_escapes(PChar c) {
    (void)c;
    return 1;
}

int platform_double_quote_backslash_escapes(PChar c) {
    return c == '"' || c == '\\' || c == '$' || c == '`';
}
