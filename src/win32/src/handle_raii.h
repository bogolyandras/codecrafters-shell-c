#ifndef BOGOLYANDRAS_HANDLE_RAII_H
#define BOGOLYANDRAS_HANDLE_RAII_H

/* Scope-based closing of Windows handles, for this platform module only (see core/raii.h) */

#include <windows.h>

#include "core/raii.h"

/** True for a handle that can be used: not NULL (no handle) and not INVALID_HANDLE_VALUE (failure) */
static inline int handle_is_usable(HANDLE handle) {
    return handle != NULL && handle != INVALID_HANDLE_VALUE;
}

/** Closes a handle if it is usable. Does not touch errno, so failures can be reported */
static inline void raii_close_handle(HANDLE *handle) {
    if (handle_is_usable(*handle)) {
        CloseHandle(*handle);
    }
}

/** For HANDLEs, closed with CloseHandle(). INVALID_HANDLE_VALUE or NULL mean "nothing to close" */
#define AUTO_CLOSE_HANDLE CLEANUP(raii_close_handle)

/** Evaluates to the handle in `h` and resets `h` to INVALID_HANDLE_VALUE, transferring ownership */
#define TAKE_HANDLE(h) ({ HANDLE raii_taken_handle_ = (h); (h) = INVALID_HANDLE_VALUE; raii_taken_handle_; })

#endif //BOGOLYANDRAS_HANDLE_RAII_H
