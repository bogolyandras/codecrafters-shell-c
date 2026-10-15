#include "core/platform.h"

#include <dirent.h>
#include <pwd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/common.h"
#include "fd_raii.h"

struct PlatformDir {
    DIR *dir;
    PChar *path; /** Kept to resolve entries whose type the directory does not tell */
};

const PChar *const platform_executable_extensions[] = {NULL};

void platform_init(void) {
}

int platform_write(OutStream stream, const PChar *text, size_t length) {
    const int fd = stream == OUT_STDERR ? STDERR_FILENO : STDOUT_FILENO;

    size_t written = 0;
    while (written < length) {
        const ssize_t count = write(fd, text + written, length - written);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        written += (size_t)count;
    }
    return 0;
}

char *platform_read_file(const PChar *path, size_t *size) {
    AUTO_CLOSE int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return NULL;
    }

    size_t capacity = 4096;
    size_t length = 0;
    AUTO_FREE char *data = malloc(capacity);
    if (data == NULL) {
        return NULL;
    }

    while (1) {
        const ssize_t count = read(fd, data + length, capacity - length - 1);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return NULL;
        }
        if (count == 0) {
            break;
        }

        length += (size_t)count;
        if (length + 1 >= capacity) {
            if (capacity > SIZE_MAX / 2) {
                errno = EFBIG;
                return NULL;
            }
            capacity *= 2;
            char *larger = realloc(data, capacity);
            if (larger == NULL) {
                return NULL;
            }
            data = larger;
        }
    }

    data[length] = '\0';
    *size = length;
    return TAKE(data);
}

int platform_write_file(const PChar *path, const char *data, size_t size, int append) {
    AUTO_CLOSE int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC | (append ? O_APPEND : O_TRUNC), 0666);
    if (fd < 0) {
        return -1;
    }

    size_t written = 0;
    while (written < size) {
        const ssize_t count = write(fd, data + written, size - written);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        written += (size_t)count;
    }

    // A delayed write error (a full disk, a network file system) is reported by close
    return close(TAKE_FD(fd));
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
