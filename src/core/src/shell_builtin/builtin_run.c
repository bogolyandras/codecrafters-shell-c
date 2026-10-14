#include "core/shell_builtin/builtin_run.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "core/common.h"
#include "core/platform.h"
#include "core/pstring.h"
#include "core/raii.h"
#include "core/shell_builtin/builtins.h"
#include "core/shell_internal/shell_error.h"
#include "core/shell_internal/tilde_expansion.h"

int is_builtin(const PChar *name) {
  for (const PChar *const *builtin_command = builtin_commands; *builtin_command != NULL; builtin_command++) {
    if (p_strcmp(name, *builtin_command) == 0) {
      return 1;
    }
  }

  return 0;
}

/**
 * The program a command name stands for. A name with a path separator in it (`./prog`,
 * `/bin/echo`) is a path and is used as it is, if it is an executable file; any other name is
 * looked up in the PATH index. Returns NULL if there is no such program. The result is `name`
 * itself or belongs to the index.
 */
const PChar *resolve_command(const PathMap *executable_to_path_map, const PChar *name) {
  for (const PChar *c = name; *c != '\0'; c++) {
    if (platform_is_path_separator(*c)) {
      return platform_is_executable_file(name) ? name : NULL;
    }
  }

  return find_executable(executable_to_path_map, name);
}

static void handle_type_command(const PChar *executable_name, const PathMap* executable_to_path_map) {
  if (is_builtin(executable_name)) {
    p_printf(P("%" PS " is a shell builtin" NEWLINE), executable_name);
    return;
  }

  const PChar *executable_location = resolve_command(executable_to_path_map, executable_name);
  if (executable_location != NULL) {
    p_printf(P("%" PS " is %" PS NEWLINE), executable_name, executable_location);
  } else {
    p_printf(P("%" PS ": not found" NEWLINE), executable_name);
  }
}

/**
 * `history -r file`, `history -w file` and `history -a file`: read a history file into the
 * history, write the whole history to a file, or append the entries that are not in a file yet.
 * `option` is the letter. Returns the exit status. In a pipeline nothing is done, since it would
 * change the shell.
 */
static int run_history_file(History *history, PChar *const *argv, size_t argument_count, int in_pipeline,
                            PChar option) {
  if (argument_count < 2) {
    p_fprintf(stderr, P("history: -%c: option requires an argument" NEWLINE), (int)option);
    return 1;
  }
  if (argument_count > 2) {
    p_fprintf(stderr, P("history: too many arguments" NEWLINE));
    return 1;
  }

  if (in_pipeline) {
    return 0;
  }

  if (option == 'r') {
    if (history_load(history, argv[2]) != 0) {
      p_fprintf(stderr, P("history: %" PS ": cannot read history file: %" PS NEWLINE), argv[2], p_strerror(errno));
      return 1;
    }
  } else if (history_save(history, argv[2], option == 'a') != 0) {
    p_fprintf(stderr, P("history: %" PS ": cannot write history file: %" PS NEWLINE), argv[2], p_strerror(errno));
    return 1;
  }

  return 0;
}

/**
 * `history [n]`: lists the entered commands with their numbers, or only the last `n` of them.
 * `history -r|-w|-a file` reads, writes or appends a history file. Returns the exit status.
 */
static int run_history(History *history, PChar *const *argv, size_t argument_count, int in_pipeline) {
  if (argument_count >= 1 && argv[1][0] == '-' && argv[1][2] == '\0' &&
      (argv[1][1] == 'r' || argv[1][1] == 'w' || argv[1][1] == 'a')) {
    return run_history_file(history, argv, argument_count, in_pipeline, argv[1][1]);
  }

  if (argument_count > 1) {
    p_fprintf(stderr, P("history: too many arguments" NEWLINE));
    return 1;
  }

  const size_t size = history_size(history);
  size_t first = 0;

  if (argument_count == 1) {
    const PChar *argument = argv[1];

    size_t limit = 0;
    int is_number = argument[0] != '\0';
    for (const PChar *digit = argument; *digit != '\0' && is_number; digit++) {
      if (*digit < '0' || *digit > '9') {
        is_number = 0;
      } else if (limit > (SIZE_MAX - 9) / 10) {
        limit = SIZE_MAX; // Larger than any history: it all fits
      } else {
        limit = limit * 10 + (size_t)(*digit - '0');
      }
    }

    if (!is_number) {
      if (argument[0] == '-') {
        p_fprintf(stderr, P("history: %" PS ": invalid option" NEWLINE), argument);
      } else {
        p_fprintf(stderr, P("history: %" PS ": numeric argument required" NEWLINE), argument);
      }
      return 1;
    }

    if (limit < size) {
      first = size - limit;
    }
  }

  for (size_t i = first; i < size; i++) {
    p_printf(P("%5lu  %" PS NEWLINE), (unsigned long)(i + 1), history_get(history, i));
  }

  return shell_report_write_error(P("history"), in_pipeline);
}

#define COMPLETE_USAGE "complete: usage: complete [-p] [-r] [-C command] [name ...]" NEWLINE

/** Prints the word in single quotes, with the quotes inside it written as '\'' */
static void print_single_quoted(const PChar *text) {
  size_t quotes = 0;
  for (const PChar *c = text; *c != '\0'; c++) {
    quotes += *c == '\'';
  }

  AUTO_FREE PChar *quoted = malloc((p_strlen(text) + 3 * quotes + 3) * sizeof(PChar));
  if (quoted == NULL) {
    p_printf(P("%" PS), text); // Out of memory: show it unquoted rather than not at all
    return;
  }

  size_t out = 0;
  quoted[out++] = '\'';
  for (const PChar *c = text; *c != '\0'; c++) {
    if (*c == '\'') {
      p_memcpy(quoted + out, P("'\\''"), 4);
      out += 4;
    } else {
      quoted[out++] = *c;
    }
  }
  quoted[out++] = '\'';
  quoted[out] = '\0';

  p_printf(P("%" PS), quoted);
}

/** Prints how `command` is registered, like `complete -p` does. Returns 0, or 1 if it is not */
static int print_completer(const CompleterTable *completers, const PChar *command) {
  const PChar *script = completers_find(completers, command);
  if (script == NULL) {
    p_fprintf(stderr, P("complete: %" PS ": no completion specification" NEWLINE), command);
    return 1;
  }

  p_printf(P("complete -C "));
  print_single_quoted(script);
  p_printf(P(" %" PS NEWLINE), command);
  return 0;
}

static int compare_entries(const void *a, const void *b) {
  const CompleterEntry *left = *(const CompleterEntry *const *)a;
  const CompleterEntry *right = *(const CompleterEntry *const *)b;
  return p_strcmp(left->command, right->command);
}

/**
 * `complete -C script command...` registers a completer, `complete -p [command...]` shows the
 * registered ones (all of them without a command, and also for a bare `complete`), and
 * `complete -r [command...]` removes them (all without a command). Returns the exit status: 1 for
 * a command without a registration, 2 for a misuse. Registering and removing do nothing in a
 * pipeline, since they would change the shell.
 */
static int run_complete(CompleterTable *completers, PChar *const *argv, size_t argument_count, int in_pipeline) {
  int print = 0;
  int remove = 0;
  const PChar *script = NULL;

  size_t i = 1;
  for (; i <= argument_count && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
    const PChar *option = argv[i];
    if (p_strcmp(option, P("--")) == 0) {
      i++;
      break;
    }
    if (p_strcmp(option, P("-p")) == 0) {
      print = 1;
    } else if (p_strcmp(option, P("-r")) == 0) {
      remove = 1;
    } else if (p_strcmp(option, P("-C")) == 0) {
      if (i + 1 > argument_count) {
        p_fprintf(stderr, P("complete: -C: option requires an argument" NEWLINE));
        return 2;
      }
      script = argv[++i];
    } else {
      p_fprintf(stderr, P("complete: %" PS ": invalid option" NEWLINE COMPLETE_USAGE), option);
      return 2;
    }
  }

  PChar *const *names = argv + i;
  size_t name_count = 0;
  while (names[name_count] != NULL) {
    name_count++;
  }

  const int options = print + remove + (script != NULL);
  if (options > 1) {
    p_fprintf(stderr, P("complete: only one of -p, -r and -C can be used" NEWLINE COMPLETE_USAGE));
    return 2;
  }

  if (script != NULL) {
    if (name_count == 0) {
      p_fprintf(stderr, P("complete: -C: a command name is required" NEWLINE COMPLETE_USAGE));
      return 2;
    }
    if (in_pipeline) {
      return 0;
    }

    for (size_t n = 0; n < name_count; n++) {
      if (completers_set(completers, names[n], script) != 0) {
        shell_print_error(P("complete"), names[n]);
        return 1;
      }
    }
    return 0;
  }

  if (remove) {
    if (in_pipeline) {
      return 0;
    }
    if (name_count == 0) {
      completers_clear(completers);
      return 0;
    }

    int status = 0;
    for (size_t n = 0; n < name_count; n++) {
      if (completers_remove(completers, names[n]) != 0) {
        p_fprintf(stderr, P("complete: %" PS ": no completion specification" NEWLINE), names[n]);
        status = 1;
      }
    }
    return status;
  }

  if (!print && name_count > 0) {
    p_fprintf(stderr, P("complete: a completer is needed: use -C script command" NEWLINE COMPLETE_USAGE));
    return 2;
  }

  int status = 0;
  if (name_count > 0) {
    for (size_t n = 0; n < name_count; n++) {
      status |= print_completer(completers, names[n]);
    }
  } else {
    // All of them, in alphabetical order
    const size_t count = completers_size(completers);
    AUTO_FREE const CompleterEntry **sorted = malloc((count + 1) * sizeof(*sorted));
    if (sorted == NULL) {
      shell_print_error(P("complete"), NULL);
      return 1;
    }
    for (size_t n = 0; n < count; n++) {
      sorted[n] = completers_get(completers, n);
    }
    qsort(sorted, count, sizeof(*sorted), compare_entries);
    for (size_t n = 0; n < count; n++) {
      print_completer(completers, sorted[n]->command);
    }
  }

  return status | shell_report_write_error(P("complete"), in_pipeline);
}

/** Prints the text in double quotes, with `"`, `\`, `$` and a backtick escaped by a backslash */
static void print_double_quoted(const PChar *text) {
  AUTO_FREE PChar *quoted = malloc((2 * p_strlen(text) + 3) * sizeof(PChar));
  if (quoted == NULL) {
    p_printf(P("\"%" PS "\""), text); // Out of memory: show it unescaped rather than not at all
    return;
  }

  size_t out = 0;
  quoted[out++] = '"';
  for (const PChar *c = text; *c != '\0'; c++) {
    if (*c == '"' || *c == '\\' || *c == '$' || *c == '`') {
      quoted[out++] = '\\';
    }
    quoted[out++] = *c;
  }
  quoted[out++] = '"';
  quoted[out] = '\0';

  p_printf(P("%" PS), quoted);
}

/** Prints how `name` is declared, like `declare -p` does. Returns 0, or 1 if there is no such variable */
static int print_variable(const VariableTable *variables, const PChar *name) {
  const Variable *variable = variables_find(variables, name);
  if (variable != NULL) {
    if (variable->value == NULL) {
      p_printf(P("declare -- %" PS NEWLINE), variable->name);
    } else {
      p_printf(P("declare -- %" PS "="), variable->name);
      print_double_quoted(variable->value);
      p_printf(P(NEWLINE));
    }
    return 0;
  }

  // Not a shell variable, but one that came with the environment: it is exported
  const PChar *from_environment = variable_name_is_valid(name) ? p_getenv(name) : NULL;
  if (from_environment != NULL) {
    p_printf(P("declare -x %" PS "="), name);
    print_double_quoted(from_environment);
    p_printf(P(NEWLINE));
    return 0;
  }

  p_fprintf(stderr, P("declare: %" PS ": not found" NEWLINE), name);
  return 1;
}

static int compare_variables(const void *a, const void *b) {
  const Variable *left = *(const Variable *const *)a;
  const Variable *right = *(const Variable *const *)b;
  return p_strcmp(left->name, right->name);
}

#define DECLARE_USAGE "declare: usage: declare [-p] [name[=value] ...]" NEWLINE

/**
 * `declare name=value...` creates or changes shell variables (`declare name` only declares it),
 * `declare -p [name...]` shows them as `declare -- name="value"`, all of them alphabetically
 * without a name. A name that is not an identifier is reported and skipped, and the status is 1.
 * Variables are not changed in a pipeline, since that would change the shell.
 */
static int run_declare(VariableTable *variables, PChar *const *argv, size_t argument_count, int in_pipeline) {
  int print = 0;

  size_t i = 1;
  for (; i <= argument_count && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
    if (p_strcmp(argv[i], P("--")) == 0) {
      i++;
      break;
    }
    if (p_strcmp(argv[i], P("-p")) == 0) {
      print = 1;
    } else {
      p_fprintf(stderr, P("declare: %" PS ": invalid option" NEWLINE DECLARE_USAGE), argv[i]);
      return 2;
    }
  }

  int status = 0;

  if (print) {
    if (argv[i] == NULL) {
      // Everything, in alphabetical order
      const size_t count = variables_size(variables);
      AUTO_FREE const Variable **sorted = malloc((count + 1) * sizeof(*sorted));
      if (sorted == NULL) {
        shell_print_error(P("declare"), NULL);
        return 1;
      }
      for (size_t n = 0; n < count; n++) {
        sorted[n] = variables_get(variables, n);
      }
      qsort(sorted, count, sizeof(*sorted), compare_variables);
      for (size_t n = 0; n < count; n++) {
        print_variable(variables, sorted[n]->name);
      }
    }

    for (; argv[i] != NULL; i++) {
      status |= print_variable(variables, argv[i]);
    }
    return status | shell_report_write_error(P("declare"), in_pipeline);
  }

  if (argv[i] == NULL) {
    return 0; // Nothing to declare
  }

  for (; argv[i] != NULL; i++) {
    const PChar *argument = argv[i];
    const PChar *equals = p_strchr(argument, '=');

    AUTO_FREE PChar *name = NULL;
    if (equals != NULL) {
      name = malloc(((size_t)(equals - argument) + 1) * sizeof(PChar));
      if (name == NULL) {
        shell_print_error(P("declare"), NULL);
        return 1;
      }
      p_memcpy(name, argument, (size_t)(equals - argument));
      name[equals - argument] = '\0';
    } else {
      name = p_strdup(argument);
      if (name == NULL) {
        shell_print_error(P("declare"), NULL);
        return 1;
      }
    }

    if (!variable_name_is_valid(name)) {
      p_fprintf(stderr, P("declare: `%" PS "': not a valid identifier" NEWLINE), argument);
      status = 1;
      continue;
    }

    if (in_pipeline) {
      continue; // Validated, but a command of a pipeline cannot change the shell
    }

    if (variables_set(variables, name, equals != NULL ? equals + 1 : NULL) != 0) {
      shell_print_error(P("declare"), name);
      status = 1;
    }
  }

  return status;
}

/**
 * Runs a builtin and returns its exit status. `exit` is handled by the caller when it is a
 * command of its own; in a pipeline it, like `cd`, must not affect the shell and does nothing.
 */
int run_builtin(PChar *const *argv, int in_pipeline, void *context) {
  ShellContext *shell = context;
  const PChar *name = argv[0];

  size_t argument_count = 0;
  while (argv[1 + argument_count] != NULL) {
    argument_count++;
  }

  if (p_strcmp(name, P("exit")) == 0) {
    return 0;
  }

  if (p_strcmp(name, P("echo")) == 0) {
    // Arguments are joined by one space, with nothing after the last one
    for (size_t i = 1; argv[i] != NULL; i++) {
      p_printf(i > 1 ? P(" %" PS) : P("%" PS), argv[i]);
    }
    p_printf(P(NEWLINE));
    return shell_report_write_error(P("echo"), in_pipeline);
  }

  if (p_strcmp(name, P("type")) == 0) {
    if (argument_count < 1) {
      p_fprintf(stderr, P("type: missing operand" NEWLINE));
      return 1;
    }
    handle_type_command(argv[1], shell->executables);
    return shell_report_write_error(P("type"), in_pipeline);
  }

  if (p_strcmp(name, P("pwd")) == 0) {
    PChar cwd[PATH_BUFFER_SIZE];
    if (p_getcwd(cwd, PATH_BUFFER_SIZE) == NULL) {
      shell_print_error(P("pwd"), NULL);
      return 1;
    }
    p_printf(P("%" PS NEWLINE), cwd);
    return shell_report_write_error(P("pwd"), in_pipeline);
  }

  if (p_strcmp(name, P("history")) == 0) {
    return run_history(shell->history, argv, argument_count, in_pipeline);
  }

  if (p_strcmp(name, P("declare")) == 0) {
    return run_declare(shell->variables, argv, argument_count, in_pipeline);
  }

  if (p_strcmp(name, P("complete")) == 0) {
    return run_complete(shell->completers, argv, argument_count, in_pipeline);
  }

  if (p_strcmp(name, P("jobs")) == 0) {
    if (argument_count > 0) {
      p_fprintf(stderr, P("jobs: too many arguments" NEWLINE));
      return 1;
    }

    // A command of a pipeline sees the jobs as they were: only the shell itself can wait for them
    // and forget the finished ones
    if (!in_pipeline) {
      jobs_poll(shell->jobs);
    }
    jobs_print(shell->jobs, 0);
    if (!in_pipeline) {
      jobs_remove_finished(shell->jobs);
    }
    return shell_report_write_error(P("jobs"), in_pipeline);
  }

  if (p_strcmp(name, P("cd")) == 0) {
    if (argument_count > 1) {
      p_fprintf(stderr, P("cd: too many operands" NEWLINE));
      return 1;
    }

    const PChar *directory_to_change_to;
    if (argument_count < 1) {
      directory_to_change_to = get_home_dir();
      if (directory_to_change_to == NULL) {
        p_fprintf(stderr, P("cd: HOME not set" NEWLINE));
        return 1;
      }
    } else {
      directory_to_change_to = argv[1];
    }

    if (in_pipeline) {
      return 0; // A command of a pipeline cannot change the directory of the shell
    }

    if (p_chdir(directory_to_change_to) != 0) {
      shell_print_error(P("cd"), directory_to_change_to);
      return 1;
    }
    return 0;
  }

  return 127;
}
