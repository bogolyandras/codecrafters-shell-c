#ifndef BOGOLYANDRAS_PLATFORM_CONFIG_H
#define BOGOLYANDRAS_PLATFORM_CONFIG_H

/* The constants that differ per platform. The POSIX twin lives in posix/include.
 * Narrow literals on purpose: they concatenate with, and compare against, both narrow and wide text */

#define PATH_LIST_SEPARATOR ';'
#define PATH_SEPARATOR '\\'
#define NEWLINE "\n"

#endif //BOGOLYANDRAS_PLATFORM_CONFIG_H
