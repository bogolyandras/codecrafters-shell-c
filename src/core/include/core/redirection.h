#ifndef BOGOLYANDRAS_REDIRECTION_H
#define BOGOLYANDRAS_REDIRECTION_H

#include "core/pstring.h"

/** Where a standard stream is redirected to */
typedef struct {
    PChar *path; /** Owned, NULL when not redirected */
    int append; /** Non-zero to append to the file (`>>`), zero to truncate it (`>`) */
} RedirectTarget;

/** The redirections of one command line */
typedef struct {
    RedirectTarget stdout_target; /** `>`, `1>`, `>>` or `1>>` */
    RedirectTarget stderr_target; /** `2>` or `2>>` */
} Redirections;

/** The standard streams a command can redirect */
typedef enum {
    REDIRECT_STDOUT,
    REDIRECT_STDERR
} RedirectStream;

/**
 * A redirected stream together with what is needed to put it back. Start it with
 * SAVED_STREAM_INIT and treat the rest as opaque: it is the platform module's business.
 */
typedef struct {
    RedirectStream stream;
    int saved; /** Opaque. Negative while the stream is untouched */
} SavedStream;

#define SAVED_STREAM_INIT(which) {(which), -1}

/** Releases the strings of the redirections. NULL-safe, cleanup-attribute friendly */
void redirections_free(Redirections *redirections);

/*
 * Implemented by the platform module (the core only calls them):
 */

/**
 * Opens the target file (created if missing, then truncated or appended to depending on the
 * target) and makes `stream` write to it, remembering in `saved` what it was. The file is not
 * inherited by launched programs, but the redirected stream is. Returns 0, or -1 with errno set,
 * in which case the stream is left untouched.
 */
int redirect_stream(const RedirectTarget *target, RedirectStream stream, SavedStream *saved);

/**
 * Puts a stream redirected by redirect_stream back. Does nothing if untouched. It is meant as a
 * cleanup function and cannot return an error: a failure is reported on stderr.
 */
void restore_stream(const SavedStream *saved);

#endif //BOGOLYANDRAS_REDIRECTION_H
