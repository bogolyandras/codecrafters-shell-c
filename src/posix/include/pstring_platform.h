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
/* What reading a character returns at the end of input: wider than PChar so that it cannot
 * be mistaken for a real character (a plain char EOF is the same as the byte 0xFF) */
#define P_EOF EOF

/* Lets the compiler check p_printf and p_eprintf like printf. Windows has no equivalent for wide text */
#define P_PRINTF_FORMAT(format_index, first_argument) __attribute__((format(printf, format_index, first_argument)))

/* Sizes and copies buffers (tokenizer, line editor, strdup). Not strlen on Windows: it counts
 * bytes up to the first 0 byte, and L"ab" is 61 00 62 00, so it would stop after one.
 */
#define p_strlen strlen
/* Equality of whole names: hashmap keys, builtin names, completer lookup, "." and "..". memcmp
 * is no substitute: it needs a length and does not stop at the terminator.
 */
#define p_strcmp strcmp
/* Prefix test for completion (command names, file names, common prefix). strcmp compares whole
 * strings, and memcmp would read past a candidate shorter than the prefix; strncmp stops at
 * the terminator.
 */
#define p_strncmp strncmp
/* Finds one unit: '=' in declare, the PATH list separator, '}' and the closing quote in the
 * tokenizer, '\n' in a line, "is c one of these" in completion. strstr wants a string, memchr
 * wants a length and does not stop at the terminator.
 */
#define p_strchr strchr
/* Unused: nothing in core, posix or win32 calls it. */
#define p_strrchr strrchr
/* Copies a known number of units: a slice of PATH with no terminator yet, a key plus its
 * extension, text into the edit buffer. strcpy needs a terminated source, strncpy pads with
 * zeros and may leave no terminator.
 */
#define p_memcpy memcpy
/* Pads the line with spaces when the editor redraws a shorter line. No string function does
 * this, and plain memset sets bytes, which is wrong for wchar_t (hence wmemset).
 */
#define p_memset memset
/* Line input. The text is already UTF-8, so the plain C functions are right. Output goes through
 * platform_write (see core/platform.h) and is formatted with p_vsnprintf */
/* Bounded formatting: job status, path joins, the COMP_POINT number. sprintf cannot be told
 * the buffer size. The result detects truncation: a path that does not fit is skipped.
 */
#define p_vsnprintf vsnprintf
#define p_snprintf snprintf /** Returns the length that would have been written */
#define p_fgets fgets
#define p_getchar getchar


/** The value of a PChar as an unsigned number, for comparisons that must not depend on signedness */
static inline unsigned int p_unit(PChar c) {
    return (unsigned char)c;
}

/** Returns non-zero if `c` is a UTF-8 continuation byte, that is not the first unit of a character */
static inline int p_is_continuation(PChar c) {
    return (p_unit(c) & 0xC0) == 0x80;
}

#endif //BOGOLYANDRAS_PSTRING_PLATFORM_H
