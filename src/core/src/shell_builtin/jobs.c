#include "core/shell_builtin/jobs.h"

#include <stdlib.h>

#include "core/common.h"
#include "core/raii.h"

int jobs_init(JobTable *table) {
    return JobVec_init(&table->jobs);
}

void jobs_free(JobTable *table) {
    for (size_t i = 0; i < JobVec_size(&table->jobs); i++) {
        free(JobVec_get(&table->jobs, i)->command);
    }

    JobVec_free(&table->jobs);
}

int jobs_add(JobTable *table, const BackgroundProcess *process, const PChar *command, int *number) {
    AUTO_FREE PChar *copy = p_strdup(command);
    if (copy == NULL) {
        return -1;
    }

    int highest = 0;
    for (size_t i = 0; i < JobVec_size(&table->jobs); i++) {
        const int existing = JobVec_get_const(&table->jobs, i)->number;
        if (existing > highest) {
            highest = existing;
        }
    }

    const Job job = {highest + 1, *process, copy, 0, 0};
    if (JobVec_push(&table->jobs, job) != 0) {
        return -1;
    }
    DISOWN(copy); // now owned by the table

    *number = job.number;
    return 0;
}

void jobs_poll(JobTable *table) {
    for (size_t i = 0; i < JobVec_size(&table->jobs); i++) {
        Job *job = JobVec_get(&table->jobs, i);
        if (job->finished) {
            continue;
        }

        int status = 0;
        const int result = poll_background(&job->process, &status);
        if (result == 0) {
            job->finished = 1;
            job->status = status;
        } else if (result < 0) {
            // Nothing more can be learned about it: it is not there any more
            job->finished = 1;
            job->status = 0;
        }
    }
}

size_t jobs_print(const JobTable *table, int only_finished) {
    const size_t count = JobVec_size(&table->jobs);
    size_t printed = 0;

    for (size_t i = 0; i < count; i++) {
        const Job *job = JobVec_get_const(&table->jobs, i);
        if (only_finished && !job->finished) {
            continue;
        }

        PChar marker = ' ';
        if (i + 1 == count) {
            marker = '+';
        } else if (i + 2 == count) {
            marker = '-';
        }

        PChar status_text[32];
        const PChar *status = P("Running");
        if (job->finished) {
            if (job->status == 0) {
                status = P("Done");
            } else {
                p_snprintf(status_text, sizeof status_text / sizeof status_text[0], P("Exit %d"), job->status);
                status = status_text;
            }
        }

        p_printf(P("[%d]%c  %-24" PS "%" PS "%" PS NEWLINE), job->number, (int)marker, status, job->command,
                 job->finished ? P("") : P(" &"));
        printed++;
    }

    return printed;
}

void jobs_remove_finished(JobTable *table) {
    Job *items = table->jobs.base.data;
    const size_t count = JobVec_size(&table->jobs);

    size_t kept = 0;
    for (size_t i = 0; i < count; i++) {
        if (items[i].finished) {
            free(items[i].command);
        } else {
            items[kept++] = items[i];
        }
    }

    table->jobs.base.size = kept;
}
