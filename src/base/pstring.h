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

/* PChar, P(), PS and the p_* functions that differ per platform */
#include "pstring_platform.h"

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

#endif //BOGOLYANDRAS_PSTRING_H
