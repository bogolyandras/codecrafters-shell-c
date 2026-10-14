#ifndef BOGOLYANDRAS_FD_RAII_H
#define BOGOLYANDRAS_FD_RAII_H

/* Scope-based closing of C runtime file descriptors, for this platform module only (see core/raii.h) */

#include <errno.h>
#include <io.h>

#include "core/raii.h"

/** Closes a descriptor if it is open (>= 0), without disturbing errno so failures can be reported */
static inline void raii_close_fd(int *fd) {
    if (*fd >= 0) {
        const int saved_errno = errno;
        _close(*fd);
        errno = saved_errno;
    }
}

/** For file descriptors, closed with _close(). A negative value means "nothing to close" */
#define AUTO_CLOSE CLEANUP(raii_close_fd)

/** Evaluates to the descriptor in `fd` and resets `fd` to -1, transferring ownership to the caller */
#define TAKE_FD(fd) ({ int raii_taken_fd_ = (fd); (fd) = -1; raii_taken_fd_; })

#endif //BOGOLYANDRAS_FD_RAII_H
