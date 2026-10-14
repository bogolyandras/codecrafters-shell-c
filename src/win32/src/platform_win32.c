#include "core/platform.h"
#include "platform_win32.h"

#include <errno.h>
#include <stdlib.h>
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

    // Flush after every print
    setbuf(stdout, NULL);
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
