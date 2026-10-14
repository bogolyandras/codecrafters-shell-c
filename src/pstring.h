#ifndef BOGOLYANDRAS_PSTRING_H
#define BOGOLYANDRAS_PSTRING_H

/**
 * The platform string abstraction.
 *
 * Text handled by the shell is made of `PChar`: `char` (UTF-8 bytes) on POSIX, and `wchar_t`
 * (UTF-16 code units) on Windows. Write text code against this header and the string functions
 * below, and it compiles for both:
 *
 *     PChar *copy = p_strdup(word);
 *     p_fprintf(stderr, P("%" PS ": not found" NEWLINE), copy);
 *
 *  - literals:   P("text")           narrow or wide literal
 *  - format:     PS                  the conversion for a PChar string argument, used as "%" PS
 *  - lengths and sizes are always counted in PChar units, never in bytes: multiply by
 *    sizeof(PChar) when allocating
 *  - C runtime calls that take a path or a name are reached through p_getenv, p_getcwd,
 *    p_chdir and p_open. Directory listing, process creation and the terminal live behind
 *    platform.h, execution_handler.h and terminal.h
 *  - raw byte data, and anything written to a file by another program, uses p_to_utf8 and
 *    p_from_utf8 where a narrow form is needed
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef _WIN32

#define PSTRING_IS_WIDE 1 /* Internal switch for the code below and pstring.c */

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

/* C runtime calls that take a path or a name */
#define p_getenv _wgetenv
#define p_getcwd _wgetcwd
#define p_chdir _wchdir
#define p_open _wopen
#define p_fopen _wfopen /* The mode is a P("...") literal too */

#else

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

/* C runtime calls that take a path or a name */
#define p_getenv getenv
#define p_getcwd getcwd
#define p_chdir chdir
#define p_open open
#define p_fopen fopen /* The mode is a P("...") literal too */

#endif

/** Returns a heap copy of the string, or NULL with errno set. Release with free() */
PChar *p_strdup(const PChar *text);

/** The message for an errno value, in a static buffer that is overwritten by the next call */
const PChar *p_strerror(int error_number);

/** Prints "text: strerror(errno)" and a newline to standard error, like perror() */
void p_perror(const PChar *text);

/**
 * Conversions between PChar strings and UTF-8. Both return a heap string the caller releases
 * with free(), or NULL with errno set. Invalid input is replaced by U+FFFD in the wide form.
 * On POSIX both are plain copies.
 */
PChar *p_from_utf8(const char *utf8);
char *p_to_utf8(const PChar *text);

/** The value of a PChar as an unsigned number, for comparisons that must not depend on signedness */
static inline unsigned int p_unit(PChar c) {
#ifdef PSTRING_IS_WIDE
    return (unsigned int)c;
#else
    return (unsigned char)c;
#endif
}

/**
 * Returns non-zero if `c` is not the first unit of a character: a UTF-8 continuation byte, or
 * the second half of a UTF-16 surrogate pair. Used to step back over whole characters.
 */
static inline int p_is_continuation(PChar c) {
#ifdef PSTRING_IS_WIDE
    return sizeof(PChar) == 2 && p_unit(c) >= 0xDC00 && p_unit(c) <= 0xDFFF;
#else
    return (p_unit(c) & 0xC0) == 0x80;
#endif
}

#endif //BOGOLYANDRAS_PSTRING_H
