#ifndef BOGOLYANDRAS_TILDE_EXPANSION_H
#define BOGOLYANDRAS_TILDE_EXPANSION_H

#include "core/pstring.h"

/**
 * Returns the user's home directory ($HOME, then the passwd entry on POSIX or %USERPROFILE% on
 * Windows), or NULL if unknown. Not owned by the caller
 */
const PChar *get_home_dir(void);

#endif //BOGOLYANDRAS_TILDE_EXPANSION_H
