#ifndef BOGOLYANDRAS_PSTRING_PLATFORM_H
#define BOGOLYANDRAS_PSTRING_PLATFORM_H

/*
 * The Windows half of pstring.h (included by it, do not include directly): PChar is `wchar_t`,
 * UTF-16 code units. The POSIX twin lives in posix/include. Only standard C headers here: the
 * operating system headers stay inside this module's src/.
 */

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

typedef wchar_t PChar;
typedef wint_t PInt; /** Wide enough for any PChar and P_EOF */

#define P(literal) P_WIDE_LITERAL(literal) /* Expands macros such as NEWLINE first */
#define P_WIDE_LITERAL(literal) L##literal
#define PS "ls"
#define P_EOF WEOF

#define p_strlen wcslen
#define p_strcmp wcscmp
#define p_strncmp wcsncmp
#define p_strchr wcschr
#define p_strrchr wcsrchr
#define p_memcpy wmemcpy
#define p_memset wmemset
#define p_snprintf swprintf /** Returns a negative value when the output does not fit */

/*
 * Text streams carry UTF-8 on every platform. On Windows these are functions that convert
 * with p_from_utf8 / p_to_utf8 (or write wide text straight to a console) instead of the
 * C runtime's wide stdio, which converts one UTF-16 unit at a time and mangles surrogate pairs.
 */
int p_printf(const PChar *format, ...);
int p_fprintf(FILE *stream, const PChar *format, ...);
int p_fputs(const PChar *text, FILE *stream);
/** Reads at most size - 1 units like fgets. A character cut by the limit becomes U+FFFD */
PChar *p_fgets(PChar *buffer, int size, FILE *stream);
/** Reads one character. A character outside the BMP is returned as its first surrogate unit */
PInt p_getchar(void);

/* C runtime calls that take a path or a name, declared in <stdlib.h> and <wchar.h> */
#define p_getenv _wgetenv
#define p_fopen _wfopen /* The mode is a P("...") literal too */

/** The value of a PChar as an unsigned number, for comparisons that must not depend on signedness */
static inline unsigned int p_unit(PChar c) {
    return (unsigned int)c;
}

/** Returns non-zero if `c` is the second half of a UTF-16 surrogate pair, not the first unit of a character */
static inline int p_is_continuation(PChar c) {
    return sizeof(PChar) == 2 && p_unit(c) >= 0xDC00 && p_unit(c) <= 0xDFFF;
}

#endif //BOGOLYANDRAS_PSTRING_PLATFORM_H
