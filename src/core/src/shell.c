#include "core/shell.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "core/common.h"
#include "core/platform.h"
#include "core/pstring.h"
#include "core/raii.h"
#include "core/execution_handler.h"
#include "core/redirection.h"
#include "core/shell_builtin/builtin_run.h"
#include "core/shell_builtin/completers.h"
#include "core/shell_builtin/history.h"
#include "core/shell_builtin/jobs.h"
#include "core/shell_builtin/variables.h"
#include "core/shell_internal/completion.h"
#include "core/shell_internal/line_editor.h"
#include "core/shell_internal/path_env_handler.h"
#include "core/shell_internal/shell_error.h"
#include "core/shell_internal/tokenizer.h"

/** The value of a variable for the tokenizer: the shell's own variables first, then the environment */
static const PChar *lookup_variable(const PChar *name, void *context) {
  const Variable *variable = variables_find(context, name);
  if (variable != NULL) {
    return variable->value; // NULL for a variable that was declared without a value
  }
  return p_getenv(name);
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
    p_eprintf(P("history: %" PS ": cannot write history file: %" PS NEWLINE), file->path, p_strerror(errno));
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
    shell_print_error(P("shell"), P("failed to prepare the pipeline"));
    return;
  }

  for (size_t i = 0; i < pipeline.count; i++) {
    Command *command = CommandVec_get(commands, i);

    if (StrVec_size(&command->tokens) == 0) {
      p_eprintf(P("shell: syntax error: missing command in the pipeline" NEWLINE));
      return;
    }

    pipeline.argv_storage[i] = build_argv(&command->tokens);
    if (pipeline.argv_storage[i] == NULL) {
      shell_print_error(P("shell"), P("failed to prepare the pipeline"));
      return;
    }

    const PChar *name = pipeline.argv_storage[i][0];
    const PChar *path = NULL;
    if (!is_builtin(name)) {
      path = resolve_command(shell->executables, name);
      if (path == NULL) {
        p_eprintf(P("%" PS ": not found" NEWLINE), name);
        return;
      }
    }

    pipeline.stages[i].argv = pipeline.argv_storage[i];
    pipeline.stages[i].path = path;
    pipeline.stages[i].redirections = &command->redirections;
  }

  if (run_pipeline(pipeline.stages, pipeline.count, run_builtin, shell) < 0) {
    shell_print_error(P("shell"), P("failed to run the pipeline"));
  }
}

int shell_run(void) {
  platform_init();

  CLEANUP(destroy_executable_to_path_map) PathMap* executable_to_path_map = NULL;
  {
    // The directories are only needed to index the executables, so they are released right after
    CLEANUP(destroy_paths) StrVec* paths = get_paths();
    if (paths == NULL) {
      shell_print_error(P("shell"), P("failed to read the PATH environment variable"));
      return 1;
    }

    executable_to_path_map = get_executable_to_path_map(paths);
    if (executable_to_path_map == NULL) {
      shell_print_error(P("shell"), P("failed to index the executables in PATH"));
      return 1;
    }
  }

  /* The commands of the line being run, with the strings they own. Cleared at each iteration */
  CLEANUP(CommandVec_free_owned) CommandVec commands = {0};
  if (CommandVec_init(&commands) != 0) {
    shell_print_error(P("shell"), P("failed to initialize the command list"));
    return 1;
  }

  /* The commands started with `&`, finished ones are reported before the next prompt */
  CLEANUP(jobs_free) JobTable jobs = {0};
  if (jobs_init(&jobs) != 0) {
    shell_print_error(P("shell"), P("failed to initialize the job list"));
    return 1;
  }

  /* The commands entered so far, for the `history` builtin and the arrow keys */
  CLEANUP(history_free) History history = {0};
  if (history_init(&history) != 0) {
    shell_print_error(P("shell"), P("failed to initialize the history"));
    return 1;
  }

  // HISTFILE names the file the history is read from at start-up and appended to at exit
  const PChar *history_file = p_getenv(P("HISTFILE"));
  if (history_file != NULL && history_file[0] == '\0') {
    history_file = NULL;
  }
  if (history_file != NULL && history_load(&history, history_file) != 0 && errno != ENOENT) {
    p_eprintf(P("history: %" PS ": cannot read history file: %" PS NEWLINE), history_file, p_strerror(errno));
  }

  // Declared after the history, so it runs before the history is released, on every way out
  CLEANUP(append_history_at_exit) HistoryFile history_at_exit = {&history, history_file};

  /* The completers registered with `complete -C`, for the session only */
  CLEANUP(completers_free) CompleterTable completers = {0};
  if (completers_init(&completers) != 0) {
    shell_print_error(P("shell"), P("failed to initialize the completer table"));
    return 1;
  }

  /* The variables made with `declare`, for the session only */
  CLEANUP(variables_free) VariableTable variables = {0};
  if (variables_init(&variables) != 0) {
    shell_print_error(P("shell"), P("failed to initialize the variable table"));
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
      shell_print_error(P("read"), NULL);
      return 1;
    }
    if (line_status == LINE_TOO_LONG) {
      p_eprintf(P("shell: input line too long" NEWLINE));
      continue;
    }

    // Every line that is not blank is remembered, including the one that is about to be run, so
    // that `history` lists itself
    for (const PChar *p = input; *p != '\0'; p++) {
      if (*p != ' ' && *p != '\t') {
        if (history_add(&history, input) != 0) {
          shell_print_error(P("shell"), P("failed to record the command in the history"));
        }
        break;
      }
    }

    CommandVec_clear_owned(&commands);

    if (tokenize(input, &commands, lookup_variable, &variables) != 0) {
      if (errno == TOKENIZE_BAD_SUBSTITUTION) {
        p_eprintf(P("shell: bad substitution" NEWLINE));
      } else if (errno == TOKENIZE_UNTERMINATED_QUOTE) {
        p_eprintf(P("shell: unterminated quote" NEWLINE));
      } else if (errno == TOKENIZE_MISSING_REDIRECT_TARGET) {
        p_eprintf(P("shell: syntax error: expected a file name after '>'" NEWLINE));
      } else if (errno == TOKENIZE_EMPTY_COMMAND) {
        p_eprintf(P("shell: syntax error: expected a command around '|' and '&'" NEWLINE));
      } else if (errno == TOKENIZE_MISPLACED_AMPERSAND) {
        p_eprintf(P("shell: syntax error: '&' can only end the line" NEWLINE));
      } else {
        shell_print_error(P("shell"), P("failed to parse the command"));
      }
      continue;
    }

    const size_t command_count = CommandVec_size(&commands);
    if (command_count == 0) {
      continue;
    }

    if (command_count > 1) {
      if (CommandVec_get(&commands, command_count - 1)->background) {
        p_eprintf(P("shell: a pipeline cannot run in the background" NEWLINE));
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
    CLEANUP(restore_stream) SavedStream saved_stdout = SAVED_STREAM_INIT(REDIRECT_STDOUT);
    CLEANUP(restore_stream) SavedStream saved_stderr = SAVED_STREAM_INIT(REDIRECT_STDERR);
    if (redirections->stdout_target.path != NULL &&
        redirect_stream(&redirections->stdout_target, REDIRECT_STDOUT, &saved_stdout) != 0) {
      shell_print_error(redirections->stdout_target.path, NULL);
      continue;
    }
    if (redirections->stderr_target.path != NULL &&
        redirect_stream(&redirections->stderr_target, REDIRECT_STDERR, &saved_stderr) != 0) {
      shell_print_error(redirections->stderr_target.path, NULL);
      continue;
    }

    if (StrVec_size(&command->tokens) == 0) {
      continue; // A redirection alone still creates or truncates the file
    }

    const PChar *first_parameter = *StrVec_get(&command->tokens, 0);

    if (command->background && is_builtin(first_parameter)) {
      p_eprintf(P("%" PS ": a builtin cannot run in the background" NEWLINE), first_parameter);
      continue;
    }

    if (p_strcmp(first_parameter, P("exit")) == 0) {
      break;
    }

    AUTO_FREE PChar **argv = build_argv(&command->tokens);
    if (argv == NULL) {
      shell_print_error(P("shell"), P("failed to allocate the arguments"));
      continue;
    }

    if (is_builtin(first_parameter)) {
      run_builtin(argv, 0, &shell);
      continue;
    }

    const PChar *executable_location = resolve_command(executable_to_path_map, first_parameter);
    if (executable_location == NULL) {
      p_eprintf(P("%" PS ": not found" NEWLINE), first_parameter);
      continue;
    }

    if (command->background) {
      BackgroundProcess *process = start_background(executable_location, argv);
      if (process == NULL) {
        shell_print_error(first_parameter, NULL);
        continue;
      }

      // The table owns the process once it is added; the id is read first for that reason
      const unsigned long process_id = background_process_id(process);

      AUTO_FREE PChar *text = background_command_text(input);
      int job_number = 0;
      if (text == NULL || jobs_add(&jobs, process, text, &job_number) != 0) {
        // The process runs, but it cannot be listed
        background_process_free(process);
        shell_print_error(P("shell"), P("failed to record the job"));
        continue;
      }

      p_printf(P("[%d] %lu" NEWLINE), job_number, process_id);
      continue;
    }

    if (do_execution(executable_location, argv) < 0) {
      shell_print_error(first_parameter, NULL);
    }
  }

  return 0;
}
