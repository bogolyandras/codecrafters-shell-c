#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "common.h"
#include "platform.h"
#include "pstring.h"
#include "shell_internal/path_env_handler.h"
#include "shell_internal/execution_handler.h"
#include "shell_builtin/builtins.h"
#include "shell_builtin/completers.h"
#include "shell_builtin/history.h"
#include "shell_builtin/jobs.h"
#include "shell_builtin/variables.h"
#include "shell_internal/completion.h"
#include "shell_internal/line_editor.h"
#include "shell_internal/redirection.h"
#include "shell_internal/tokenizer.h"
#include "shell_internal/tilde_expansion.h"
#include "raii.h"

/** What the builtins need to know about the shell */
typedef struct {
  const PathMap *executables;
  History *history;
  JobTable *jobs;
  CompleterTable *completers;
  VariableTable *variables;
} ShellContext;

/** Prints "context: strerror(errno)" or "context: detail: strerror(errno)" to stderr */
static void print_error(const PChar *context, const PChar *detail) {
  if (detail != NULL) {
    p_fprintf(stderr, P("%" PS ": %" PS ": %" PS NEWLINE), context, detail, p_strerror(errno));
  } else {
    p_fprintf(stderr, P("%" PS ": %" PS NEWLINE), context, p_strerror(errno));
  }
}

/**
 * Reports a failed write to standard output (for example a full disk behind `> file`) once.
 * In a pipeline the failure is the reader having finished, which is not an error worth a message.
 */
static int report_write_error(const PChar *command, int in_pipeline) {
  if (!ferror(stdout)) {
    return 0;
  }

  clearerr(stdout);
  if (in_pipeline) {
    return 0;
  }

  print_error(command, P("write error"));
  return 1;
}

static int is_builtin(const PChar *name) {
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
static const PChar *resolve_command(const PathMap *executable_to_path_map, const PChar *name) {
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

  return report_write_error(P("history"), in_pipeline);
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
        print_error(P("complete"), names[n]);
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
      print_error(P("complete"), NULL);
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

  return status | report_write_error(P("complete"), in_pipeline);
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

/** The value of a variable for the tokenizer: the shell's own variables first, then the environment */
static const PChar *lookup_variable(const PChar *name, void *context) {
  const Variable *variable = variables_find(context, name);
  if (variable != NULL) {
    return variable->value; // NULL for a variable that was declared without a value
  }
  return p_getenv(name);
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
        print_error(P("declare"), NULL);
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
    return status | report_write_error(P("declare"), in_pipeline);
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
        print_error(P("declare"), NULL);
        return 1;
      }
      p_memcpy(name, argument, (size_t)(equals - argument));
      name[equals - argument] = '\0';
    } else {
      name = p_strdup(argument);
      if (name == NULL) {
        print_error(P("declare"), NULL);
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
      print_error(P("declare"), name);
      status = 1;
    }
  }

  return status;
}

/**
 * Runs a builtin and returns its exit status. `exit` is handled by the caller when it is a
 * command of its own; in a pipeline it, like `cd`, must not affect the shell and does nothing.
 */
static int run_builtin(PChar *const *argv, int in_pipeline, void *context) {
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
    return report_write_error(P("echo"), in_pipeline);
  }

  if (p_strcmp(name, P("type")) == 0) {
    if (argument_count < 1) {
      p_fprintf(stderr, P("type: missing operand" NEWLINE));
      return 1;
    }
    handle_type_command(argv[1], shell->executables);
    return report_write_error(P("type"), in_pipeline);
  }

  if (p_strcmp(name, P("pwd")) == 0) {
    PChar cwd[PATH_BUFFER_SIZE];
    if (p_getcwd(cwd, PATH_BUFFER_SIZE) == NULL) {
      print_error(P("pwd"), NULL);
      return 1;
    }
    p_printf(P("%" PS NEWLINE), cwd);
    return report_write_error(P("pwd"), in_pipeline);
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
    return report_write_error(P("jobs"), in_pipeline);
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
      print_error(P("cd"), directory_to_change_to);
      return 1;
    }
    return 0;
  }

  return 127;
}

/**
 * The text of a command line that ends with `&`, for the job list: the line without the `&` and
 * the blanks around it. Release with free(). NULL with errno set on failure.
 */
static PChar *background_command_text(const PChar *input) {
  size_t length = p_strlen(input);
  while (length > 0 && (input[length - 1] == ' ' || input[length - 1] == '\t')) {
    length--;
  }
  if (length > 0 && input[length - 1] == '&') {
    length--;
  }
  while (length > 0 && (input[length - 1] == ' ' || input[length - 1] == '\t')) {
    length--;
  }

  PChar *text = malloc((length + 1) * sizeof(PChar));
  if (text == NULL) {
    return NULL;
  }
  p_memcpy(text, input, length);
  text[length] = '\0';
  return text;
}

/** The history file the shell appends to when it ends, if HISTFILE is set */
typedef struct {
  History *history;
  const PChar *path; /** NULL when there is no history file */
} HistoryFile;

static void append_history_at_exit(const HistoryFile *file) {
  if (file->path != NULL && history_save(file->history, file->path, 1) != 0) {
    p_fprintf(stderr, P("history: %" PS ": cannot write history file: %" PS NEWLINE), file->path, p_strerror(errno));
  }
}

/** Builds a NULL terminated argument array pointing into the tokens. Release with free() */
static PChar **build_argv(StrVec *tokens) {
  const size_t count = StrVec_size(tokens);

  PChar **argv = malloc(sizeof(PChar *) * (count + 1));
  if (argv == NULL) {
    return NULL;
  }

  for (size_t i = 0; i < count; i++) {
    argv[i] = *StrVec_get(tokens, i);
  }
  argv[count] = NULL;

  return argv;
}

/** The argument arrays and stages of a pipeline being prepared */
typedef struct {
  PipelineStage *stages;
  PChar ***argv_storage; /* One argument array per stage, each released with free() */
  size_t count;
} PreparedPipeline;

static void prepared_pipeline_free(PreparedPipeline *pipeline) {
  if (pipeline->argv_storage != NULL) {
    for (size_t i = 0; i < pipeline->count; i++) {
      free(pipeline->argv_storage[i]);
    }
  }

  free(pipeline->argv_storage);
  free(pipeline->stages);
}

/** Runs the commands connected by pipes. Reports problems itself */
static void execute_pipeline(CommandVec *commands, ShellContext *shell) {
  CLEANUP(prepared_pipeline_free) PreparedPipeline pipeline = {0};
  pipeline.count = CommandVec_size(commands);

  pipeline.stages = calloc(pipeline.count, sizeof(PipelineStage));
  pipeline.argv_storage = calloc(pipeline.count, sizeof(PChar **));
  if (pipeline.stages == NULL || pipeline.argv_storage == NULL) {
    print_error(P("shell"), P("failed to prepare the pipeline"));
    return;
  }

  for (size_t i = 0; i < pipeline.count; i++) {
    Command *command = CommandVec_get(commands, i);

    if (StrVec_size(&command->tokens) == 0) {
      p_fprintf(stderr, P("shell: syntax error: missing command in the pipeline" NEWLINE));
      return;
    }

    pipeline.argv_storage[i] = build_argv(&command->tokens);
    if (pipeline.argv_storage[i] == NULL) {
      print_error(P("shell"), P("failed to prepare the pipeline"));
      return;
    }

    const PChar *name = pipeline.argv_storage[i][0];
    const PChar *path = NULL;
    if (!is_builtin(name)) {
      path = resolve_command(shell->executables, name);
      if (path == NULL) {
        p_fprintf(stderr, P("%" PS ": not found" NEWLINE), name);
        return;
      }
    }

    pipeline.stages[i].argv = pipeline.argv_storage[i];
    pipeline.stages[i].path = path;
    pipeline.stages[i].redirections = &command->redirections;
  }

  if (run_pipeline(pipeline.stages, pipeline.count, run_builtin, shell) < 0) {
    print_error(P("shell"), P("failed to run the pipeline"));
  }
}

#ifdef _WIN32
/* The Windows entry point of a console program with wide characters, built with -municode */
int wmain(void) {
#else
int main(void) {
#endif

  platform_init();

  CLEANUP(destroy_executable_to_path_map) PathMap* executable_to_path_map = NULL;
  {
    // The directories are only needed to index the executables, so they are released right after
    CLEANUP(destroy_paths) StrVec* paths = get_paths();
    if (paths == NULL) {
      print_error(P("shell"), P("failed to read the PATH environment variable"));
      return 1;
    }

    executable_to_path_map = get_executable_to_path_map(paths);
    if (executable_to_path_map == NULL) {
      print_error(P("shell"), P("failed to index the executables in PATH"));
      return 1;
    }
  }

  /* The commands of the line being run, with the strings they own. Cleared at each iteration */
  CLEANUP(CommandVec_free_owned) CommandVec commands = {0};
  if (CommandVec_init(&commands) != 0) {
    print_error(P("shell"), P("failed to initialize the command list"));
    return 1;
  }

  /* The commands started with `&`, finished ones are reported before the next prompt */
  CLEANUP(jobs_free) JobTable jobs = {0};
  if (jobs_init(&jobs) != 0) {
    print_error(P("shell"), P("failed to initialize the job list"));
    return 1;
  }

  /* The commands entered so far, for the `history` builtin and the arrow keys */
  CLEANUP(history_free) History history = {0};
  if (history_init(&history) != 0) {
    print_error(P("shell"), P("failed to initialize the history"));
    return 1;
  }

  // HISTFILE names the file the history is read from at start-up and appended to at exit
  const PChar *history_file = p_getenv(P("HISTFILE"));
  if (history_file != NULL && history_file[0] == '\0') {
    history_file = NULL;
  }
  if (history_file != NULL && history_load(&history, history_file) != 0 && errno != ENOENT) {
    p_fprintf(stderr, P("history: %" PS ": cannot read history file: %" PS NEWLINE), history_file, p_strerror(errno));
  }

  // Declared after the history, so it runs before the history is released, on every way out
  CLEANUP(append_history_at_exit) HistoryFile history_at_exit = {&history, history_file};

  /* The completers registered with `complete -C`, for the session only */
  CLEANUP(completers_free) CompleterTable completers = {0};
  if (completers_init(&completers) != 0) {
    print_error(P("shell"), P("failed to initialize the completer table"));
    return 1;
  }

  /* The variables made with `declare`, for the session only */
  CLEANUP(variables_free) VariableTable variables = {0};
  if (variables_init(&variables) != 0) {
    print_error(P("shell"), P("failed to initialize the variable table"));
    return 1;
  }

  ShellContext shell = {executable_to_path_map, &history, &jobs, &completers, &variables};
  const CompletionContext completion_context = {executable_to_path_map, &completers};

  while(1) {
    // Jobs that ended since the last prompt are reported once and forgotten
    jobs_poll(&jobs);
    if (jobs_print(&jobs, 1) > 0) {
      jobs_remove_finished(&jobs);
    }

    PChar input[1024];
    const int line_status = line_read(P("$ "), input, sizeof input / sizeof input[0], &history,
                                      complete_command_line, (void *)&completion_context);
    if (line_status == LINE_EOF) {
      p_printf(P("Exiting shell" NEWLINE));
      return 0;
    }
    if (line_status == LINE_ERROR) {
      print_error(P("read"), NULL);
      return 1;
    }
    if (line_status == LINE_TOO_LONG) {
      p_fprintf(stderr, P("shell: input line too long" NEWLINE));
      continue;
    }

    // Every line that is not blank is remembered, including the one that is about to be run, so
    // that `history` lists itself
    for (const PChar *p = input; *p != '\0'; p++) {
      if (*p != ' ' && *p != '\t') {
        if (history_add(&history, input) != 0) {
          print_error(P("shell"), P("failed to record the command in the history"));
        }
        break;
      }
    }

    CommandVec_clear_owned(&commands);

    if (tokenize(input, &commands, lookup_variable, &variables) != 0) {
      if (errno == TOKENIZE_BAD_SUBSTITUTION) {
        p_fprintf(stderr, P("shell: bad substitution" NEWLINE));
      } else if (errno == TOKENIZE_UNTERMINATED_QUOTE) {
        p_fprintf(stderr, P("shell: unterminated quote" NEWLINE));
      } else if (errno == TOKENIZE_MISSING_REDIRECT_TARGET) {
        p_fprintf(stderr, P("shell: syntax error: expected a file name after '>'" NEWLINE));
      } else if (errno == TOKENIZE_EMPTY_COMMAND) {
        p_fprintf(stderr, P("shell: syntax error: expected a command around '|' and '&'" NEWLINE));
      } else if (errno == TOKENIZE_MISPLACED_AMPERSAND) {
        p_fprintf(stderr, P("shell: syntax error: '&' can only end the line" NEWLINE));
      } else {
        print_error(P("shell"), P("failed to parse the command"));
      }
      continue;
    }

    const size_t command_count = CommandVec_size(&commands);
    if (command_count == 0) {
      continue;
    }

    if (command_count > 1) {
      if (CommandVec_get(&commands, command_count - 1)->background) {
        p_fprintf(stderr, P("shell: a pipeline cannot run in the background" NEWLINE));
        continue;
      }
      execute_pipeline(&commands, &shell);
      continue;
    }

    Command *command = CommandVec_get(&commands, 0);
    const Redirections *redirections = &command->redirections;

    // The descriptors stay redirected until the end of this iteration, whichever way it is left.
    // Standard output goes first, so a failure to open the standard error file is still reported
    // on the terminal.
    CLEANUP(restore_fd) SavedFd saved_stdout = {STDOUT_FILENO, -1};
    CLEANUP(restore_fd) SavedFd saved_stderr = {STDERR_FILENO, -1};
    if (redirections->stdout_target.path != NULL &&
        redirect_fd(&redirections->stdout_target, STDOUT_FILENO, &saved_stdout) != 0) {
      print_error(redirections->stdout_target.path, NULL);
      continue;
    }
    if (redirections->stderr_target.path != NULL &&
        redirect_fd(&redirections->stderr_target, STDERR_FILENO, &saved_stderr) != 0) {
      print_error(redirections->stderr_target.path, NULL);
      continue;
    }

    if (StrVec_size(&command->tokens) == 0) {
      continue; // A redirection alone still creates or truncates the file
    }

    const PChar *first_parameter = *StrVec_get(&command->tokens, 0);

    if (command->background && is_builtin(first_parameter)) {
      p_fprintf(stderr, P("%" PS ": a builtin cannot run in the background" NEWLINE), first_parameter);
      continue;
    }

    if (p_strcmp(first_parameter, P("exit")) == 0) {
      break;
    }

    AUTO_FREE PChar **argv = build_argv(&command->tokens);
    if (argv == NULL) {
      print_error(P("shell"), P("failed to allocate the arguments"));
      continue;
    }

    if (is_builtin(first_parameter)) {
      run_builtin(argv, 0, &shell);
      continue;
    }

    const PChar *executable_location = resolve_command(executable_to_path_map, first_parameter);
    if (executable_location == NULL) {
      p_fprintf(stderr, P("%" PS ": not found" NEWLINE), first_parameter);
      continue;
    }

    if (command->background) {
      BackgroundProcess process = {0};
      if (start_background(executable_location, argv, &process) != 0) {
        print_error(first_parameter, NULL);
        continue;
      }

      AUTO_FREE PChar *text = background_command_text(input);
      int job_number = 0;
      if (text == NULL || jobs_add(&jobs, &process, text, &job_number) != 0) {
        // The process runs, but it cannot be listed
        print_error(P("shell"), P("failed to record the job"));
        continue;
      }

      p_printf(P("[%d] %lu" NEWLINE), job_number, process.id);
      continue;
    }

    if (do_execution(executable_location, argv) < 0) {
      print_error(first_parameter, NULL);
    }
  }

  return 0;
}
