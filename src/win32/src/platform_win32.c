#include "core/platform.h"
#include "platform_win32.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "core/common.h"

struct PlatformDir {
    HANDLE handle;
    WIN32_FIND_DATAW data;
    int pending; /** `data` holds an entry that was found but not returned yet */
};

const PChar *const platform_executable_extensions[] = {L".com", L".exe", L".bat", L".cmd", NULL};

void platform_set_errno_from_last_error(void) {
    switch (GetLastError()) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_NAME:
        case ERROR_BAD_PATHNAME:
            errno = ENOENT;
            break;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            errno = EACCES;
            break;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            errno = ENOMEM;
            break;
        case ERROR_BAD_EXE_FORMAT:
            errno = ENOEXEC;
            break;
        default:
            errno = EINVAL;
            break;
    }
}

/**
 * A Ctrl-C is delivered to every process attached to the console. The shell ignores it, so that
 * only the running command is interrupted. At the prompt the line editor reads it as a key.
 */
static BOOL WINAPI ignore_ctrl_c(DWORD control_type) {
    return control_type == CTRL_C_EVENT;
}

void platform_init(void) {
    // The shell text is UTF-16 in memory and UTF-8 on every stream (see pstring.h). The code
    // pages are for the programs it starts: they usually write UTF-8 when the console expects it
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    SetConsoleCtrlHandler(ignore_ctrl_c, TRUE);
}

/** Writes bytes to a handle until all are written. Returns 0, or -1 with errno set */
static int write_bytes(HANDLE handle, const char *data, size_t size) {
    size_t written = 0;
    while (written < size) {
        const size_t remaining = size - written;
        DWORD count = 0;
        if (!WriteFile(handle, data + written, remaining > MAXDWORD ? MAXDWORD : (DWORD)remaining, &count, NULL) ||
            count == 0) {
            platform_set_errno_from_last_error();
            return -1;
        }
        written += count;
    }
    return 0;
}

int platform_write(OutStream stream, const PChar *text, size_t length) {
    if (length == 0) {
        return 0;
    }

    // The standard handle at this moment, so that redirections (SetStdHandle) apply
    HANDLE handle = GetStdHandle(stream == OUT_STDERR ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (handle == NULL || handle == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }

    DWORD mode;
    if (GetConsoleMode(handle, &mode)) {
        size_t done = 0;
        while (done < length) {
            const size_t remaining = length - done;
            DWORD count = 0;
            // A console write of more than about 64 KiB can fail, so it goes in pieces
            if (!WriteConsoleW(handle, text + done, remaining > 16384 ? 16384 : (DWORD)remaining, &count, NULL) ||
                count == 0) {
                platform_set_errno_from_last_error();
                return -1;
            }
            done += count;
        }
        return 0;
    }

    AUTO_FREE PChar *terminated = malloc((length + 1) * sizeof(PChar));
    if (terminated == NULL) {
        return -1;
    }
    memcpy(terminated, text, length * sizeof(PChar));
    terminated[length] = L'\0';

    AUTO_FREE char *utf8 = p_to_utf8(terminated);
    if (utf8 == NULL) {
        return -1;
    }
    const size_t size = strlen(utf8);

    // The bytes as they are, to a pipe or a file alike: no line ending translation
    return write_bytes(handle, utf8, size);
}

char *platform_read_file(const PChar *path, size_t *size) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        platform_set_errno_from_last_error();
        return NULL;
    }

    size_t capacity = 4096;
    size_t length = 0;
    char *data = malloc(capacity);
    if (data == NULL) {
        CloseHandle(file);
        return NULL;
    }

    while (1) {
        DWORD count = 0;
        if (!ReadFile(file, data + length, (DWORD)(capacity - length - 1), &count, NULL)) {
            // A pipe that was closed at its write end is the end of the data
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                break;
            }
            platform_set_errno_from_last_error();
            free(data);
            CloseHandle(file);
            return NULL;
        }
        if (count == 0) {
            break;
        }

        length += count;
        if (length + 1 >= capacity) {
            if (capacity > MAXDWORD) {
                errno = EFBIG;
                free(data);
                CloseHandle(file);
                return NULL;
            }
            capacity *= 2;
            char *larger = realloc(data, capacity);
            if (larger == NULL) {
                free(data);
                CloseHandle(file);
                return NULL;
            }
            data = larger;
        }
    }

    CloseHandle(file);
    data[length] = '\0';
    *size = length;
    return data;
}

int platform_write_file(const PChar *path, const char *data, size_t size, int append) {
    // Like a redirection: `>` creates or truncates, `>>` creates or opens and writes at the end
    const DWORD access = append ? FILE_APPEND_DATA : GENERIC_WRITE;
    const DWORD disposition = append ? OPEN_ALWAYS : CREATE_ALWAYS;

    HANDLE file = CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, disposition,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        platform_set_errno_from_last_error();
        return -1;
    }

    size_t written = 0;
    while (written < size) {
        const size_t remaining = size - written;
        DWORD count = 0;
        if (!WriteFile(file, data + written, remaining > MAXDWORD ? MAXDWORD : (DWORD)remaining, &count, NULL)) {
            platform_set_errno_from_last_error();
            CloseHandle(file);
            return -1;
        }
        written += count;
    }

    if (!CloseHandle(file)) {
        platform_set_errno_from_last_error();
        return -1;
    }
    return 0;
}

PlatformDir *platform_dir_open(const PChar *path) {
    const size_t path_length = wcslen(path);

    // The pattern is the directory followed by `\*`
    PChar *pattern = malloc((path_length + 3) * sizeof(PChar));
    if (pattern == NULL) {
        return NULL;
    }
    memcpy(pattern, path, path_length * sizeof(PChar));
    size_t length = path_length;
    if (length > 0 && !platform_is_path_separator(pattern[length - 1])) {
        pattern[length++] = L'\\';
    }
    pattern[length++] = L'*';
    pattern[length] = L'\0';

    PlatformDir *dir = calloc(1, sizeof(PlatformDir));
    if (dir == NULL) {
        free(pattern);
        return NULL;
    }

    dir->handle = FindFirstFileW(pattern, &dir->data);
    free(pattern);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        platform_set_errno_from_last_error();
        free(dir);
        return NULL;
    }

    dir->pending = 1;
    return dir;
}

int platform_dir_read(PlatformDir *dir, const PChar **name, int *is_directory) {
    while (1) {
        if (dir->pending) {
            dir->pending = 0;
        } else if (!FindNextFileW(dir->handle, &dir->data)) {
            if (GetLastError() == ERROR_NO_MORE_FILES) {
                return 0;
            }
            platform_set_errno_from_last_error();
            return -1;
        }

        if (wcscmp(dir->data.cFileName, L".") == 0 || wcscmp(dir->data.cFileName, L"..") == 0) {
            continue;
        }

        *name = dir->data.cFileName;
        *is_directory = (dir->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        return 1;
    }
}

void platform_dir_close(PlatformDir *dir) {
    FindClose(dir->handle);
    free(dir);
}

int platform_is_executable_file(const PChar *path) {
    const DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return 0;
    }

    const PChar *extension = wcsrchr(path, L'.');
    if (extension == NULL) {
        return 0;
    }

    for (const PChar *const *candidate = platform_executable_extensions; *candidate != NULL; candidate++) {
        if (_wcsicmp(extension, *candidate) == 0) {
            return 1;
        }
    }

    return 0;
}

void platform_normalize_command_name(PChar *name) {
    _wcslwr(name);
}

int platform_is_path_separator(PChar c) {
    return c == L'/' || c == L'\\';
}


const PChar *platform_default_home_dir(void) {
    const PChar *profile = p_getenv(L"USERPROFILE");
    return profile != NULL && profile[0] != L'\0' ? profile : NULL;
}

/* `$` is here so that a literal `$` can be written (`\$name`) now that `$name` is expanded. It
 * does mean that `C:\$Recycle.Bin` has to be quoted. */
int platform_backslash_escapes(PChar c) {
    return c == L' ' || c == L'\t' || c == L'\'' || c == L'"' || c == L'>' || c == L'|' || c == L'&' ||
           c == L'~' || c == L'$';
}

int platform_double_quote_backslash_escapes(PChar c) {
    return c == L'"' || c == L'$';
}
