#ifndef BOGOLYANDRAS_BUILTIN_RUN_H
#define BOGOLYANDRAS_BUILTIN_RUN_H

#include "core/pstring.h"
#include "core/shell_builtin/completers.h"
#include "core/shell_builtin/history.h"
#include "core/shell_builtin/jobs.h"
#include "core/shell_builtin/variables.h"
#include "core/shell_internal/path_env_handler.h"

/** What the builtins need to know about the shell */
typedef struct {
    const PathMap *executables;
    History *history;
    JobTable *jobs;
    CompleterTable *completers;
    VariableTable *variables;
} ShellContext;

/** Returns non-zero if `name` is one of the builtin commands */
int is_builtin(const PChar *name);

/**
 * The program a command name stands for. A name with a path separator in it (`./prog`,
 * `/bin/echo`) is a path and is used as it is, if it is an executable file; any other name is
 * looked up in the PATH index. Returns NULL if there is no such program. The result is `name`
 * itself or belongs to the index.
 */
const PChar *resolve_command(const PathMap *executable_to_path_map, const PChar *name);

/**
 * Runs a builtin and returns its exit status. `context` is a ShellContext. `exit` is handled by
 * the caller when it is a command of its own; in a pipeline it, like `cd`, must not affect the
 * shell and does nothing. Matches the BuiltinRunner callback of execution_handler.h.
 */
int run_builtin(PChar *const *argv, int in_pipeline, void *context);

#endif //BOGOLYANDRAS_BUILTIN_RUN_H
