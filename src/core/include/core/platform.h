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

/* ---- environment ------------------------------------------------------------------------ */

/**
 * The value of an environment variable, or NULL if it is not set. The text belongs to the
 * platform module: it stays valid until the variable changes, and is never freed by the caller.
 * An empty value is "" (not NULL). Names are case insensitive on Windows.
 */
const PChar *platform_getenv(const PChar *name);

/* ---- standard output -------------------------------------------------------------------- */

/**
 * Writes `length` units of text to standard output or standard error, as UTF-8 bytes (straight
 * to the console as wide text when it is one on Windows). Nothing is buffered: the text has
 * reached the stream when the call returns. Returns 0, or -1 with errno set.
 *
 * The stream is whatever descriptor 1 or 2 is at that moment, so redirections made by
 * redirect_stream apply. Everything the shell prints goes through here (p_printf and friends).
 */
int platform_write(OutStream stream, const PChar *text, size_t length);

/* ---- files ------------------------------------------------------------------------------ */

/**
 * Reads a whole file. Returns a heap buffer (release with free()) holding `*size` bytes followed
 * by a 0 that is not counted, or NULL with errno set (ENOENT when it does not exist). The bytes
 * are returned as they are: no line ending or encoding conversion.
 */
char *platform_read_file(const PChar *path, size_t *size);

/**
 * Writes `size` bytes as they are, creating the file when it is missing. Replaces the content,
 * or adds to the end when `append` is non-zero. Returns 0, or -1 with errno set.
 */
int platform_write_file(const PChar *path, const char *data, size_t size, int append);

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
