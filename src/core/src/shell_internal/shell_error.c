#include "core/shell_internal/shell_error.h"

#include <errno.h>

#include "core/common.h"
#include "core/pstring.h"

/** Prints "context: strerror(errno)" or "context: detail: strerror(errno)" to stderr */
void shell_print_error(const PChar *context, const PChar *detail) {
  if (detail != NULL) {
    p_eprintf(P("%" PS ": %" PS ": %" PS NEWLINE), context, detail, p_strerror(errno));
  } else {
    p_eprintf(P("%" PS ": %" PS NEWLINE), context, p_strerror(errno));
  }
}

/**
 * Reports a failed write to standard output (for example a full disk behind `> file`) once.
 * In a pipeline the failure is the reader having finished, which is not an error worth a message.
 */
int shell_report_write_error(const PChar *command, int in_pipeline) {
  if (!p_take_write_error(OUT_STDOUT)) {
    return 0;
  }

  if (in_pipeline) {
    return 0;
  }

  shell_print_error(command, P("write error"));
  return 1;
}
