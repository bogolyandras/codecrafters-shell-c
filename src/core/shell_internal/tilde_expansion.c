#include "shell_internal/tilde_expansion.h"

#include <stdlib.h>

#include "platform.h"

const PChar *get_home_dir(void) {
    const PChar *home = p_getenv(P("HOME"));
    if (home != NULL && home[0] != '\0') {
        return home;
    }

    return platform_default_home_dir();
}
