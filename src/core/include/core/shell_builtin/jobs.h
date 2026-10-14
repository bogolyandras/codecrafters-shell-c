#ifndef BOGOLYANDRAS_JOBS_H
#define BOGOLYANDRAS_JOBS_H

#include "core/datastructs/vector.h"
#include "core/execution_handler.h"
#include "core/pstring.h"

/** A command started with `&` */
typedef struct {
    int number; /** The `[n]` of the job, unique among the jobs in the table */
    BackgroundProcess process;
    PChar *command; /** Owned: the command line as typed, without the final `&` */
    int finished; /** Non-zero once the process was seen to end */
    int status; /** The exit status, valid when `finished` */
} Job;

VECTOR_DEFINE(Job, JobVec)

/** The jobs of the shell, oldest first */
typedef struct {
    JobVec jobs;
} JobTable;

/** Returns 0, or -1 with errno set. A table whose init failed is safe to free */
int jobs_init(JobTable *table);

/** Releases the table. The processes are not waited for or stopped */
void jobs_free(JobTable *table);

/**
 * Adds a started process. `command` is copied. The job number is one more than the highest
 * number in the table (1 for an empty table) and is stored in `number`.
 * Returns 0, or -1 with errno set (the process is then not in the table).
 */
int jobs_add(JobTable *table, const BackgroundProcess *process, const PChar *command, int *number);

/** Looks at every running job without blocking and marks those that ended as finished */
void jobs_poll(JobTable *table);

/**
 * Prints the jobs, oldest first, as `[1]+  Running                 sleep 10 &`: the number, `+`
 * for the most recently started job, `-` for the one before it and a blank for the others, two
 * spaces, the status padded to 24 and the command. A finished job is `Done` or `Exit N`, without
 * the `&`. With `only_finished` the running jobs are left out (the markers are the same).
 * Returns the number of jobs printed.
 */
size_t jobs_print(const JobTable *table, int only_finished);

/** Forgets the jobs that are finished. Their number can be used again once the table is empty */
void jobs_remove_finished(JobTable *table);

#endif //BOGOLYANDRAS_JOBS_H
