#ifndef BOGOLYANDRAS_PLATFORM_H
#define BOGOLYANDRAS_PLATFORM_H

/**
 * The parts of the operating system the shell logic needs, other than the terminal
 * (terminal.h) and process creation (execution_handler.h). Implemented by platform_posix.c and
 * platform_win32.c; both files are always part of the build and compile to nothing on the other
 * platform.
 */

#include "pstring.h"
#include "raii.h"

/** Prepares the standard streams and the process for the shell. Call first in the entry point */
void platform_init(void);

/* ---- directories ------------------------------------------------------------------------ */

typedef struct PlatformDir PlatformDir;

/** Opens a directory for listing. Returns NULL with errno set on failure */
PlatformDir *platform_dir_open(const PChar *path);

/**
 * Reads the next entry, skipping `.` and `..`. Returns 1 and sets `name` (valid until the next
 * call) and `is_directory`, 0 at the end of the directory, or -1 with errno set on failure.
 * A symbolic link to a directory counts as a directory.
 */
int platform_dir_read(PlatformDir *dir, const PChar **name, int *is_directory);

void platform_dir_close(PlatformDir *dir);

static inline void platform_dir_cleanup(PlatformDir **dir) {
    if (*dir != NULL) {
        platform_dir_close(*dir);
    }
}

/** For PlatformDir* from platform_dir_open, closed with platform_dir_close */
#define AUTO_CLOSE_DIR CLEANUP(platform_dir_cleanup)

/* ---- executables ------------------------------------------------------------------------ */

/** Returns non-zero if the path is a regular file the shell can run */
int platform_is_executable_file(const PChar *path);

/**
 * Extensions that make a file executable and that may be left out when typing a command,
 * NULL terminated, in lookup order. Empty on POSIX, where the file permissions decide.
 */
extern const PChar *const platform_executable_extensions[];

/**
 * Rewrites a command name in place into the form used as a key of the executable map:
 * unchanged on POSIX, lower case on Windows where file names are case insensitive.
 */
void platform_normalize_command_name(PChar *name);

/** Returns non-zero if `c` separates path components: `/`, and also `\` on Windows */
int platform_is_path_separator(PChar c);

#ifdef _WIN32
/** Sets errno from the calling thread's last Windows error */
void platform_set_errno_from_last_error(void);
#endif

#endif //BOGOLYANDRAS_PLATFORM_H
