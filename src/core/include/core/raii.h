#ifndef BOGOLYANDRAS_RAII_H
#define BOGOLYANDRAS_RAII_H

#include <stdlib.h>

#include <errno.h>
#include <stdio.h>

/**
 * Scope-based cleanup built on the GCC/Clang `cleanup` attribute (not available in MSVC).
 * The cleanup function receives a pointer to the variable when it goes out of scope.
 *
 * File descriptors are not here: closing one needs an operating system header, so the
 * AUTO_CLOSE and TAKE_FD helpers live in each platform module (fd_raii.h).
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

/** Evaluates to the value of `p` and resets `p` to NULL, transferring ownership to the caller */
#define TAKE(p) ({ __typeof__(p) raii_taken_ = (p); (p) = NULL; raii_taken_; })

/** Gives up ownership of `p` after it was successfully handed elsewhere, so the cleanup frees nothing */
#define DISOWN(p) ((void)((p) = NULL))

#endif //BOGOLYANDRAS_RAII_H
