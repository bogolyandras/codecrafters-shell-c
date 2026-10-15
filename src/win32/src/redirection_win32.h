#ifndef BOGOLYANDRAS_REDIRECTION_WIN32_H
#define BOGOLYANDRAS_REDIRECTION_WIN32_H

/* Windows only, shared by the files in this directory: what execution_win32.c needs in addition
 * to the redirect_stream / restore_stream of core/redirection.h */

#include <windows.h>

#include "core/redirection.h"

/**
 * Opens the target file with CreateFileW (created if missing, then truncated or appended to).
 * Returns the handle, not inheritable, or INVALID_HANDLE_VALUE with errno set.
 */
HANDLE redirect_open(const RedirectTarget *target);

/**
 * Makes `source` the standard handle of `stream` (SetStdHandle) and fills `saved` so that
 * restore_stream can put the previous one back. On success the standard handle owns `source`:
 * restore_stream closes it, and the caller must not. Returns 0, or -1 with errno set, in which
 * case nothing changed and the caller still owns `source`.
 */
int redirect_to_handle(HANDLE source, RedirectStream stream, SavedStream *saved);

#endif //BOGOLYANDRAS_REDIRECTION_WIN32_H
