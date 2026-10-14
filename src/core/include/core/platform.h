#ifndef BOGOLYANDRAS_PLATFORM_H
#define BOGOLYANDRAS_PLATFORM_H

/**
 * The parts of the operating system the shell logic needs, other than the terminal
 * (terminal.h), redirection (redirection.h) and process creation (execution_handler.h). Declared
 * here by the core, implemented by the platform module the build selects (src/posix or
 * src/win32): the core includes no operating system header.
 */

#include "core/pstring.h"
#include "core/raii.h"

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

/* ---- the user and the shell language ---------------------------------------------------- */

/** The home directory when HOME is not set: the profile directory. NULL if unknown or empty */
const PChar *platform_default_home_dir(void);

/**
 * Returns non-zero if a backslash outside quotes makes `c` literal. Every character on POSIX;
 * on Windows only the ones that mean something to the shell, since a backslash there is a path
 * separator (`cd C:\Users\me`).
 */
int platform_backslash_escapes(PChar c);

/** Returns non-zero if a backslash inside double quotes makes `c` literal (otherwise it is kept) */
int platform_double_quote_backslash_escapes(PChar c);

#endif //BOGOLYANDRAS_PLATFORM_H
