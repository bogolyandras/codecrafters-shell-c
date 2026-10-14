#ifndef BOGOLYANDRAS_EXECUTION_HANDLER_H
#define BOGOLYANDRAS_EXECUTION_HANDLER_H

#include <stddef.h>

#include "core/pstring.h"
#include "core/redirection.h"

/**
 * Runs the executable in a child process and waits for it.
 * Returns the exit status of the child (128 + signal number if it was killed by a signal;
 * 127 if the executable could not be started), or -1 with errno set if forking or waiting failed.
 */
int do_execution(const PChar* executable_full_path, PChar *const* args);

/** A program started with start_background and not waited for */
typedef struct {
    unsigned long id; /** The process id, as shown to the user */
    void *handle; /** The process handle on Windows, unused on POSIX */
} BackgroundProcess;

/**
 * Starts the executable like do_execution does, with the current standard descriptors, but does
 * not wait for it. Its standard input is the null device, so it cannot compete with the shell for
 * the terminal, and on POSIX it is in a process group of its own, so Ctrl-C of a foreground
 * command does not reach it. Returns 0 and fills `process`, or -1 with errno set.
 */
int start_background(const PChar *executable_full_path, PChar *const *args, BackgroundProcess *process);

/**
 * Checks without blocking whether the process has ended. Returns 1 if it is still running, 0 if
 * it has ended (`status` is set like the result of do_execution and the process is released, so
 * it must not be polled again), or -1 with errno set.
 */
int poll_background(BackgroundProcess *process, int *status);

/** An environment variable set for one child process only */
typedef struct {
    const PChar *name; /** NULL ends an array of these */
    const PChar *value;
} EnvVar;

/**
 * Runs the executable like do_execution does, but its standard input and standard error are the
 * null device (a message in the middle of the line being edited would garble it) and its
 * standard output is collected instead of shown. `environment` (NULL, or terminated by an entry
 * without a name) is added to the child's environment. On success `*output` is a heap copy of
 * what the child wrote, as text (release with free()), and the exit status is returned. A child
 * that writes more than 1 MiB, or on POSIX stays silent and alive for 5 seconds, is killed and the
 * output so far is kept. Returns -1 with errno set if the child could not be started or waited for.
 */
int run_capture(const PChar *executable_full_path, PChar *const *args, const EnvVar *environment,
                PChar **output);

/** One command of a pipeline */
typedef struct {
    PChar *const *argv; /** NULL terminated, argv[0] is the name as typed */
    const PChar *path; /** The executable to run, or NULL if the command is a builtin */
    const Redirections *redirections; /** Applied after the pipes, so a file wins over a pipe */
} PipelineStage;

/**
 * Runs a builtin as one stage of a pipeline and returns its exit status. `in_pipeline` is always
 * non-zero here: the builtin must not change the state of the shell (`cd`, `exit`).
 */
typedef int (*BuiltinRunner)(PChar *const *argv, int in_pipeline, void *context);

/**
 * Runs the stages at the same time, each one's standard output feeding the next one's standard
 * input, and waits for all of them. Builtin stages are run through `run_builtin`: in a forked
 * child on POSIX, and in the shell process on Windows (after the programs have been started, so a
 * large output cannot block). Builtins do not read their input, and it is closed for them.
 *
 * Returns the exit status of the last stage, or -1 with errno set if a stage could not be set up
 * (the stages already started are waited for).
 */
int run_pipeline(const PipelineStage *stages, size_t count, BuiltinRunner run_builtin, void *context);

#endif //BOGOLYANDRAS_EXECUTION_HANDLER_H
