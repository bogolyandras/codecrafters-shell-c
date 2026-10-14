#ifndef BOGOLYANDRAS_LINE_EDITOR_H
#define BOGOLYANDRAS_LINE_EDITOR_H

#include <stddef.h>

#include "core/shell_builtin/history.h"
#include "core/pstring.h"
#include "core/str_vec.h"

#define LINE_OK 1
#define LINE_EOF 0
#define LINE_ERROR -1
#define LINE_TOO_LONG -2 /** Only reported for non-interactive input; the rest of the line is dropped */

/**
 * Called when TAB is pressed. `line` is the text typed so far (NUL terminated) and `word_start`
 * the index where the word under completion begins. The callback appends every candidate for
 * that word, complete and starting with line + word_start, to `matches` as heap strings the
 * vector owns. A candidate ending in `/` is a directory: it is completed without a trailing
 * space, and lists show only its last path component, unless the callback sets
 * `*whole_names` (initially 0) to non-zero: the candidates are then listed as they are, which is
 * what a candidate such as `origin/main` from an external completer needs.
 * Returns 0 on success, -1 on failure.
 */
typedef int (*LineCompleter)(const PChar *line, size_t word_start, StrVec *matches, int *whole_names,
                             void *context);

/**
 * Prints the prompt and reads one line into `buffer` (at most size - 1 PChar units, without the
 * trailing newline; `size` counts units, not bytes). `completer` may be NULL to disable TAB completion.
 *
 * On an interactive terminal this is a small line editor: printable characters, Backspace,
 * Ctrl-U (clear line), Ctrl-C (cancel line), Ctrl-D (end of input on an empty line), TAB
 * completion, and the Up and Down arrows to recall the lines of `history` (which may be NULL).
 * Other escape sequences are ignored. Otherwise the line is read with plain stdio.
 *
 * Returns LINE_OK, LINE_EOF, LINE_ERROR (errno set) or LINE_TOO_LONG.
 */
int line_read(const PChar *prompt, PChar *buffer, size_t size, const History *history,
              LineCompleter completer, void *context);

#endif //BOGOLYANDRAS_LINE_EDITOR_H
