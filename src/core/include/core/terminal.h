#ifndef BOGOLYANDRAS_TERMINAL_H
#define BOGOLYANDRAS_TERMINAL_H

#include <stddef.h>

#include "core/pstring.h"

/**
 * Platform abstraction over the interactive terminal, used by the line editor.
 * Implemented by terminal_posix.c (termios); a Windows backend is expected to implement the
 * same functions with the console API.
 */

/** Returns non-zero if standard input is an interactive terminal */
int terminal_is_interactive(void);

/**
 * Switches standard input to raw-ish mode: no line buffering, no echo, and no signal generating
 * keys (Ctrl-C arrives as the byte 0x03). Output processing stays on, so "\n" still moves to the
 * start of the next line. Returns 0 on success, -1 with errno set on failure.
 * Must be paired with terminal_restore before running child processes or exiting.
 */
int terminal_enter_raw(void);

/** Restores the mode saved by terminal_enter_raw. Safe to call when not in raw mode */
void terminal_restore(void);

/**
 * Reads one PChar: a byte (part of a UTF-8 sequence) on POSIX, a UTF-16 code unit on Windows.
 * Returns 1 if a unit was read, 0 at end of input, -1 with errno set on failure.
 */
int terminal_read_char(PChar *out);

/** Writes `length` PChar units to the terminal. Failures are ignored: there is nowhere to report them */
void terminal_write(const PChar *text, size_t length);

#endif //BOGOLYANDRAS_TERMINAL_H
