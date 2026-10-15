#include "core/redirection.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "core/common.h"
#include "core/pstring.h"
#include "fd_raii.h"

static int stream_fd(RedirectStream stream) {
    return stream == REDIRECT_STDERR ? STDERR_FILENO : STDOUT_FILENO;
}

int redirect_stream(const RedirectTarget *target, RedirectStream stream, SavedStream *saved) {
    const int fd = stream_fd(stream);
    const int mode = target->append ? O_APPEND : O_TRUNC;

    // Close-on-exec, so launched programs do not inherit the file, only the redirected stream
    AUTO_CLOSE int file = open(target->path, O_WRONLY | O_CREAT | mode | O_CLOEXEC, 0666);
    if (file < 0) {
        return -1;
    }

    // Close-on-exec as well, so launched programs do not inherit the saved terminal descriptor
    AUTO_CLOSE int saved_fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (saved_fd < 0) {
        return -1;
    }

    if (dup2(file, fd) < 0) {
        return -1;
    }

    saved->stream = stream;
    saved->saved = TAKE_FD(saved_fd);
    return 0; // `file` is closed on return, it is no longer needed
}

void restore_stream(const SavedStream *saved) {
    if (saved->saved < 0) {
        return;
    }

    // A cleanup function cannot return an error, and the shell would otherwise silently keep
    // writing to the file, so this is reported here
    const int fd = stream_fd(saved->stream);
    if (dup2(saved->saved, fd) < 0) {
        p_eprintf(P("shell: failed to restore descriptor %d: %" PS NEWLINE), fd, p_strerror(errno));
    }
    close(saved->saved);
}
