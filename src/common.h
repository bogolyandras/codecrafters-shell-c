#ifndef BOGOLYANDRAS_COMMON_H
#define BOGOLYANDRAS_COMMON_H

/* Narrow literals on purpose: they concatenate with, and compare against, both narrow and wide text */
#ifdef _WIN32
    #define PATH_LIST_SEPARATOR ';'
    #define PATH_SEPARATOR '\\'
    #define NEWLINE "\n"
#else
    #define PATH_LIST_SEPARATOR ':'
    #define PATH_SEPARATOR '/'
    #define NEWLINE "\n"
#endif

/** Size, in PChar units, of the buffers that hold a file system path */
#define PATH_BUFFER_SIZE 4096

#endif //BOGOLYANDRAS_COMMON_H
