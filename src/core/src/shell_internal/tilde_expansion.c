#include "core/shell_internal/tilde_expansion.h"

#include <stdlib.h>

#include "core/platform.h"

const PChar *get_home_dir(void) {
    const PChar *home = platform_getenv(P("HOME"));
    if (home != NULL && home[0] != '\0') {
        return home;
    }

    return platform_default_home_dir();
}
