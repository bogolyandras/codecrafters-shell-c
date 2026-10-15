#include "core/pstring.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "core/common.h"

/* The POSIX half of pstring.c: PChar is `char`, already UTF-8, so the conversions are copies */

const PChar *p_strerror(int error_number) {
    return strerror(error_number);
}

void p_perror(const PChar *text) {
    p_eprintf(P("%" PS ": %" PS NEWLINE), text, strerror(errno));
}

PChar *p_from_utf8(const char *utf8) {
    return p_strdup(utf8);
}

char *p_to_utf8(const PChar *text) {
    return p_strdup(text);
}

PChar *p_getcwd(PChar *buffer, size_t size) {
    return getcwd(buffer, size);
}

int p_chdir(const PChar *path) {
    return chdir(path);
}
