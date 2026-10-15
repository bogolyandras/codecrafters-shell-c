#include "core/redirection.h"
#include "redirection_win32.h"

#include <errno.h>
#include <stdint.h>
#include <windows.h>

#include "core/common.h"
#include "core/pstring.h"
#include "handle_raii.h"
#include "platform_win32.h"

static DWORD standard_handle_id(RedirectStream stream) {
    return stream == REDIRECT_STDERR ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;
}

HANDLE redirect_open(const RedirectTarget *target) {
    // `>` creates or truncates, `>>` creates or opens, and then every write goes to the end
    const DWORD access = target->append ? FILE_APPEND_DATA : GENERIC_WRITE;
    const DWORD disposition = target->append ? OPEN_ALWAYS : CREATE_ALWAYS;

    HANDLE file = CreateFileW(target->path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, disposition,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        platform_set_errno_from_last_error();
    }
    return file;
}

int redirect_to_handle(HANDLE source, RedirectStream stream, SavedStream *saved) {
    const DWORD id = standard_handle_id(stream);

    // NULL is a valid answer (the process has no such handle) and is put back as it is
    HANDLE current = GetStdHandle(id);
    if (current == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }

    if (!SetStdHandle(id, source)) {
        platform_set_errno_from_last_error();
        return -1;
    }

    saved->stream = stream;
    saved->saved = (intptr_t)current;
    return 0;
}

int redirect_stream(const RedirectTarget *target, RedirectStream stream, SavedStream *saved) {
    AUTO_CLOSE_HANDLE HANDLE file = redirect_open(target);
    if (file == INVALID_HANDLE_VALUE) {
        return -1;
    }

    if (redirect_to_handle(file, stream, saved) != 0) {
        return -1; // `file` is closed on return
    }

    TAKE_HANDLE(file); // the standard handle owns it until restore_stream
    return 0;
}

void restore_stream(const SavedStream *saved) {
    if (saved->saved == -1) {
        return;
    }

    // A cleanup function cannot return an error, and the shell would otherwise silently keep
    // writing to the file, so this is reported here
    const DWORD id = standard_handle_id(saved->stream);
    HANDLE redirected = GetStdHandle(id);
    if (!SetStdHandle(id, (HANDLE)saved->saved)) {
        platform_set_errno_from_last_error();
        p_eprintf(P("shell: failed to restore standard handle %d: %" PS NEWLINE), (int)id, p_strerror(errno));
        return;
    }

    // Only now that the old handle is back is the redirected one closed. For the end of a pipe
    // this is what lets the reader see the end of its input
    if (handle_is_usable(redirected)) {
        CloseHandle(redirected);
    }
}
