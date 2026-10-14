#include "shell_internal/redirection.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

#include "common.h"
#include "pstring.h"
#include "raii.h"

void redirections_free(Redirections *redirections) {
    free(redirections->stdout_target.path);
    redirections->stdout_target.path = NULL;
    free(redirections->stderr_target.path);
    redirections->stderr_target.path = NULL;
}

int redirect_open(const RedirectTarget *target) {
    const int mode = target->append ? O_APPEND : O_TRUNC;
#ifdef _WIN32
    return p_open(target->path, O_WRONLY | O_CREAT | mode | _O_NOINHERIT, 0666);
#else
    return p_open(target->path, O_WRONLY | O_CREAT | mode | O_CLOEXEC, 0666);
#endif
}

int redirect_fd_to(int source, int fd, SavedFd *saved) {
#ifdef _WIN32
    // A copy that launched programs do not inherit
    AUTO_CLOSE int saved_fd = _dup(fd);
    if (saved_fd < 0) {
        return -1;
    }
    SetHandleInformation((HANDLE)_get_osfhandle(saved_fd), HANDLE_FLAG_INHERIT, 0);
#else
    // Close-on-exec, so launched programs do not inherit the saved terminal descriptor
    AUTO_CLOSE int saved_fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (saved_fd < 0) {
        return -1;
    }
#endif

    if (dup2(source, fd) < 0) {
        return -1;
    }

    saved->fd = fd;
    saved->saved = TAKE_FD(saved_fd);
    return 0;
}

int redirect_fd(const RedirectTarget *target, int fd, SavedFd *saved) {
    AUTO_CLOSE int file = redirect_open(target);
    if (file < 0) {
        return -1;
    }

    return redirect_fd_to(file, fd, saved); // `file` is closed on return, it is no longer needed
}

void restore_fd(const SavedFd *saved) {
    if (saved->saved < 0) {
        return;
    }

    // A cleanup function cannot return an error, and the shell would otherwise silently keep
    // writing to the file, so this is reported here
    if (dup2(saved->saved, saved->fd) < 0) {
        p_fprintf(stderr, P("shell: failed to restore descriptor %d: %" PS NEWLINE), saved->fd, p_strerror(errno));
    }
    close(saved->saved);
}
