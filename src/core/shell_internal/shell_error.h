#ifndef BOGOLYANDRAS_SHELL_ERROR_H
#define BOGOLYANDRAS_SHELL_ERROR_H

#include "pstring.h"

/** Prints "context: strerror(errno)" or "context: detail: strerror(errno)" to stderr */
void shell_print_error(const PChar *context, const PChar *detail);

/**
 * Reports a failed write to standard output (for example a full disk behind `> file`) once, and
 * returns 1 for it, otherwise 0. In a pipeline the failure is the reader having finished, which is
 * not an error worth a message.
 */
int shell_report_write_error(const PChar *command, int in_pipeline);

#endif //BOGOLYANDRAS_SHELL_ERROR_H
