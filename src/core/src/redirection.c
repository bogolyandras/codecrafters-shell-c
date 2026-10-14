#include "core/redirection.h"

#include <stdlib.h>

void redirections_free(Redirections *redirections) {
    free(redirections->stdout_target.path);
    redirections->stdout_target.path = NULL;
    free(redirections->stderr_target.path);
    redirections->stderr_target.path = NULL;
}
