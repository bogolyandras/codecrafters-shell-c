#include "shell_internal/tilde_expansion.h"

#include <stdlib.h>

#ifndef _WIN32
#include <pwd.h>
#include <unistd.h>
#endif

static const PChar *non_empty(const PChar *text) {
    return text != NULL && text[0] != '\0' ? text : NULL;
}

const PChar *get_home_dir(void) {
    const PChar *home = non_empty(p_getenv(P("HOME")));
    if (home != NULL) {
        return home;
    }

#ifdef _WIN32
    return non_empty(p_getenv(P("USERPROFILE")));
#else
    const struct passwd *pw = getpwuid(getuid());
    return pw != NULL ? non_empty(pw->pw_dir) : NULL;
#endif
}
