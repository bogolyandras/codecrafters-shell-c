#ifndef BOGOLYANDRAS_COMPLETION_H
#define BOGOLYANDRAS_COMPLETION_H

#include <stddef.h>

#include "shell_builtin/completers.h"
#include "shell_internal/path_env_handler.h"
#include "str_vec.h"

typedef struct {
    const PathMap *executables; /** Executables found on PATH, not owned */
    const CompleterTable *completers; /** Registered with `complete -C`, not owned */
} CompletionContext;

/**
 * A LineCompleter (see line_editor.h).
 *
 *  - the first word is completed as a command name: the builtins and the executables on PATH,
 *    unless it contains a `/`, then it is completed as a path
 *  - every other word of a command that has a registered completer is completed by running it:
 *    `script command word previous_word`, with COMP_LINE, COMP_POINT, COMP_KEY and COMP_TYPE in
 *    its environment. Every non-empty line it prints is a candidate. Nothing else is offered
 *    then, so a completer that prints nothing leaves the line unchanged.
 *  - every other word is completed as a path, relative to the current directory or to the
 *    directory it names (`src/ma` -> `src/main.c`, `~/Doc` -> `~/Documents/`). Directories get a
 *    trailing `/`, hidden files are only offered when the typed name starts with a `.`, and
 *    characters that would split or change the word are escaped with a backslash.
 *
 * Quotes in the word being completed are not understood.
 */
int complete_command_line(const PChar *line, size_t word_start, StrVec *matches, int *whole_names,
                          void *context);

#endif //BOGOLYANDRAS_COMPLETION_H
