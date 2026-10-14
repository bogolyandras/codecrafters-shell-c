#ifndef BOGOLYANDRAS_REDIRECTION_WIN32_H
#define BOGOLYANDRAS_REDIRECTION_WIN32_H

/* Windows only, shared by the files in this directory: what execution_win32.c needs in addition
 * to the redirect_stream / restore_stream of core/redirection.h */

#include "core/redirection.h"

/**
 * Opens the target file with CreateFileW and returns it as a C runtime descriptor (text mode, not
 * inherited by launched programs), or -1 with errno set.
 */
int redirect_open(const RedirectTarget *target);

/**
 * Points `stream` at the open descriptor `source`, which stays open and owned by the caller, and
 * fills `saved` so the stream can be restored. Returns 0, or -1 with errno set, in which case
 * the stream is left untouched.
 */
int redirect_to_fd(int source, RedirectStream stream, SavedStream *saved);

#endif //BOGOLYANDRAS_REDIRECTION_WIN32_H
