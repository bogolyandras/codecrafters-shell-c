#ifndef BOGOLYANDRAS_REDIRECTION_H
#define BOGOLYANDRAS_REDIRECTION_H

#include "pstring.h"

/** Where a descriptor is redirected to */
typedef struct {
    PChar *path; /** Owned, NULL when not redirected */
    int append; /** Non-zero to append to the file (`>>`), zero to truncate it (`>`) */
} RedirectTarget;

/** The redirections of one command line */
typedef struct {
    RedirectTarget stdout_target; /** `>`, `1>`, `>>` or `1>>` */
    RedirectTarget stderr_target; /** `2>` or `2>>` */
} Redirections;

/** A redirected descriptor together with a copy of what it pointed to before, or -1 if untouched */
typedef struct {
    int fd;
    int saved;
} SavedFd;

/** Releases the strings of the redirections. NULL-safe, cleanup-attribute friendly */
void redirections_free(Redirections *redirections);

/**
 * Opens the target file (created if missing, then truncated or appended to depending on the
 * target) for writing. Returns the descriptor, which is not inherited by launched programs, or
 * -1 with errno set.
 */
int redirect_open(const RedirectTarget *target);

/**
 * Points the descriptor `fd` to the open descriptor `source`, which stays open and owned by the
 * caller, and fills `saved` so `fd` can be restored. Returns 0 on success, -1 with errno set on
 * failure, in which case `fd` is left untouched.
 */
int redirect_fd_to(int source, int fd, SavedFd *saved);

/** redirect_open followed by redirect_fd_to, for a redirection to a file */
int redirect_fd(const RedirectTarget *target, int fd, SavedFd *saved);

/**
 * Restores a descriptor redirected by redirect_fd and closes the copy. Does nothing if untouched.
 * It is meant as a cleanup function and cannot return an error: a failure is reported on stderr.
 */
void restore_fd(const SavedFd *saved);

#endif //BOGOLYANDRAS_REDIRECTION_H
