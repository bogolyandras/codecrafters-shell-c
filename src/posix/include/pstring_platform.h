#ifndef BOGOLYANDRAS_PSTRING_PLATFORM_H
#define BOGOLYANDRAS_PSTRING_PLATFORM_H

/*
 * The POSIX half of pstring.h (included by it, do not include directly): PChar is `char`, the
 * bytes of a UTF-8 string, so every p_* function is the plain C one. Only standard C headers
 * here: the operating system headers stay inside this module's src/. The Windows twin lives in
 * platform/win32/include.
 */

#include <stdio.h>

typedef char PChar;
typedef int PInt;

#define P(literal) literal
#define PS "s"
#define P_EOF EOF

#define p_strlen strlen
#define p_strcmp strcmp
#define p_strncmp strncmp
#define p_strchr strchr
#define p_strrchr strrchr
#define p_memcpy memcpy
#define p_memset memset
#define p_printf printf
#define p_fprintf fprintf
#define p_snprintf snprintf /** Returns the length that would have been written */
#define p_fputs fputs
#define p_fgets fgets
#define p_getchar getchar

/* C runtime calls that take a path or a name, declared in <stdlib.h> and <stdio.h> */
#define p_getenv getenv
#define p_fopen fopen /* The mode is a P("...") literal too */

/** The value of a PChar as an unsigned number, for comparisons that must not depend on signedness */
static inline unsigned int p_unit(PChar c) {
    return (unsigned char)c;
}

/** Returns non-zero if `c` is a UTF-8 continuation byte, that is not the first unit of a character */
static inline int p_is_continuation(PChar c) {
    return (p_unit(c) & 0xC0) == 0x80;
}

#endif //BOGOLYANDRAS_PSTRING_PLATFORM_H
