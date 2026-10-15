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
/* What reading a character returns at the end of input: wider than PChar so that it cannot
 * be mistaken for a real character (a plain char EOF is the same as the byte 0xFF) */
#define P_EOF WEOF

/* The compiler has no format check for wide format strings */
#define P_PRINTF_FORMAT(format_index, first_argument)

/* Sizes and copies buffers (tokenizer, line editor, strdup). Not strlen on Windows: it counts
 * bytes up to the first 0 byte, and L"ab" is 61 00 62 00, so it would stop after one.
 */
#define p_strlen wcslen
/* Equality of whole names: hashmap keys, builtin names, completer lookup, "." and "..". memcmp
 * is no substitute: it needs a length and does not stop at the terminator.
 */
#define p_strcmp wcscmp
/* Prefix test for completion (command names, file names, common prefix). strcmp compares whole
 * strings, and memcmp would read past a candidate shorter than the prefix; strncmp stops at
 * the terminator.
 */
#define p_strncmp wcsncmp
/* Finds one unit: '=' in declare, the PATH list separator, '}' and the closing quote in the
 * tokenizer, '\n' in a line, "is c one of these" in completion. strstr wants a string, memchr
 * wants a length and does not stop at the terminator.
 */
#define p_strchr wcschr
/* Unused: nothing in core, posix or win32 calls it. */
#define p_strrchr wcsrchr
/* Copies a known number of units: a slice of PATH with no terminator yet, a key plus its
 * extension, text into the edit buffer. strcpy needs a terminated source, strncpy pads with
 * zeros and may leave no terminator.
 */
#define p_memcpy wmemcpy
/* Pads the line with spaces when the editor redraws a shorter line. No string function does
 * this, and plain memset sets bytes, which is wrong for wchar_t (hence wmemset).
 */
#define p_memset wmemset
/* Bounded formatting: job status, path joins, the COMP_POINT number. sprintf cannot be told
 * the buffer size. The result detects truncation: a path that does not fit is skipped.
 */
#define p_vsnprintf vswprintf
#define p_snprintf swprintf /** Returns a negative value when the output does not fit */

/* Text streams carry UTF-8 on every platform. Output goes through platform_write (see
 * core/platform.h), which converts, so the C runtime's wide stdio is not used for it: it converts
 * one UTF-16 unit at a time and mangles surrogate pairs */
/** Reads at most size - 1 units like fgets. A character cut by the limit becomes U+FFFD */
PChar *p_fgets(PChar *buffer, int size, FILE *stream);
/** Reads one character. A character outside the BMP is returned as its first surrogate unit */
PInt p_getchar(void);

/* C runtime calls that take a path or a name, declared in <stdlib.h> and <wchar.h> */
/* HOME, PATH, HISTFILE and the fallback for declared variables. Not getenv on Windows: the
 * name and value are wide there, and the narrow one uses the ANSI code page.
 */
#define p_getenv _wgetenv

/** The value of a PChar as an unsigned number, for comparisons that must not depend on signedness */
static inline unsigned int p_unit(PChar c) {
    return (unsigned int)c;
}

/** Returns non-zero if `c` is the second half of a UTF-16 surrogate pair, not the first unit of a character */
static inline int p_is_continuation(PChar c) {
    return sizeof(PChar) == 2 && p_unit(c) >= 0xDC00 && p_unit(c) <= 0xDFFF;
}

#endif //BOGOLYANDRAS_PSTRING_PLATFORM_H
