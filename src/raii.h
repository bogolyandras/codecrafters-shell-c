#ifndef BOGOLYANDRAS_RAII_H
#define BOGOLYANDRAS_RAII_H

#include <stdlib.h>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/**
 * Scope-based cleanup built on the GCC/Clang `cleanup` attribute (not available in MSVC).
 * The cleanup function receives a pointer to the variable when it goes out of scope.
 *
 * Caveats:
 *  - always initialize the variable at its declaration, the cleanup runs on every exit path
 *  - cleanups do not run on exit(), _exit(), abort(), exec*() or longjmp()
 *  - when a resource outlives the scope, hand it out with TAKE() so it is not released
 */

/** Runs `fn(&variable)` when the variable goes out of scope */
#define CLEANUP(fn) __attribute__((cleanup(fn)))

static inline void raii_free_ptr(void *ptr) {
    free(*(void **)ptr);
}

/** For pointers released with free() */
#define AUTO_FREE CLEANUP(raii_free_ptr)

static inline void raii_fclose(FILE **file) {
    if (*file != NULL) {
        const int saved_errno = errno;
        fclose(*file);
        errno = saved_errno;
    }
}

/** For FILE* streams from fopen(), closed with fclose(). NULL means "nothing to close" */
#define AUTO_FCLOSE CLEANUP(raii_fclose)

/** Closes a descriptor if it is open (>= 0), without disturbing errno so failures can be reported */
static inline void raii_close_fd(int *fd) {
    if (*fd >= 0) {
        const int saved_errno = errno;
        close(*fd);
        errno = saved_errno;
    }
}

/** For file descriptors, closed with close(). A negative value means "nothing to close" */
#define AUTO_CLOSE CLEANUP(raii_close_fd)

/** Evaluates to the descriptor in `fd` and resets `fd` to -1, transferring ownership to the caller */
#define TAKE_FD(fd) ({ int raii_taken_fd_ = (fd); (fd) = -1; raii_taken_fd_; })

/** Evaluates to the value of `p` and resets `p` to NULL, transferring ownership to the caller */
#define TAKE(p) ({ __typeof__(p) raii_taken_ = (p); (p) = NULL; raii_taken_; })

/** Gives up ownership of `p` after it was successfully handed elsewhere, so the cleanup frees nothing */
#define DISOWN(p) ((void)((p) = NULL))

#endif //BOGOLYANDRAS_RAII_H
