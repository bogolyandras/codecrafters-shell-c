#include "core/redirection.h"
#include "redirection_win32.h"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <windows.h>

#include "core/common.h"
#include "core/pstring.h"
#include "fd_raii.h"
#include "platform_win32.h"

static int stream_fd(RedirectStream stream) {
    return _fileno(stream == REDIRECT_STDERR ? stderr : stdout);
}

int redirect_open(const RedirectTarget *target) {
    // Not inheritable: launched programs get the redirected stream, not this file handle
    SECURITY_ATTRIBUTES attributes = {sizeof attributes, NULL, FALSE};

    // `>` creates or truncates, `>>` creates or opens, and then every write goes to the end
    const DWORD access = target->append ? FILE_APPEND_DATA : GENERIC_WRITE;
    const DWORD disposition = target->append ? OPEN_ALWAYS : CREATE_ALWAYS;

    HANDLE file = CreateFileW(target->path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, disposition,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        platform_set_errno_from_last_error();
        return -1;
    }

    // Text mode like the descriptors the C runtime opens itself
    const int fd = _open_osfhandle((intptr_t)file, _O_TEXT | (target->append ? _O_APPEND : 0));
    if (fd < 0) {
        CloseHandle(file);
        return -1;
    }
    return fd;
}

int redirect_to_fd(int source, RedirectStream stream, SavedStream *saved) {
    const int fd = stream_fd(stream);

    // A copy that launched programs do not inherit, so they do not hold on to the saved descriptor
    AUTO_CLOSE int saved_fd = _dup(fd);
    if (saved_fd < 0) {
        return -1;
    }
    SetHandleInformation((HANDLE)_get_osfhandle(saved_fd), HANDLE_FLAG_INHERIT, 0);

    if (_dup2(source, fd) < 0) {
        return -1;
    }

    saved->stream = stream;
    saved->saved = TAKE_FD(saved_fd);
    return 0;
}

int redirect_stream(const RedirectTarget *target, RedirectStream stream, SavedStream *saved) {
    AUTO_CLOSE int file = redirect_open(target);
    if (file < 0) {
        return -1;
    }

    return redirect_to_fd(file, stream, saved); // `file` is closed on return, it is no longer needed
}

void restore_stream(const SavedStream *saved) {
    if (saved->saved < 0) {
        return;
    }

    // A cleanup function cannot return an error, and the shell would otherwise silently keep
    // writing to the file, so this is reported here
    const int fd = stream_fd(saved->stream);
    if (_dup2(saved->saved, fd) < 0) {
        p_fprintf(stderr, P("shell: failed to restore descriptor %d: %" PS NEWLINE), fd, p_strerror(errno));
    }
    _close(saved->saved);
}
