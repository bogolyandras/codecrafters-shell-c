#include "shell_internal/completion.h"

#include <errno.h>
#include <stdlib.h>

#include "shell_builtin/builtins.h"
#include "shell_internal/execution_handler.h"
#include "platform.h"
#include "raii.h"
#include "shell_internal/tilde_expansion.h"
#include "shell_internal/tokenizer.h"

static int add_match(StrVec *matches, const PChar *candidate) {
    AUTO_FREE PChar *copy = p_strdup(candidate);
    if (copy == NULL) {
        return -1;
    }
    if (StrVec_push(matches, copy) != 0) {
        return -1;
    }
    DISOWN(copy); // now owned by matches
    return 0;
}

static int complete_command_name(const PChar *typed_prefix, StrVec *matches, const CompletionContext *completion) {
    // Executable names are normalized (lower case on Windows), so the prefix is as well
    AUTO_FREE PChar *prefix = p_strdup(typed_prefix);
    if (prefix == NULL) {
        return -1;
    }
    platform_normalize_command_name(prefix);
    const size_t prefix_length = p_strlen(prefix);

    for (const PChar *const *builtin = builtin_commands; *builtin != NULL; builtin++) {
        if (p_strncmp(*builtin, prefix, prefix_length) == 0 && add_match(matches, *builtin) != 0) {
            return -1;
        }
    }

    PathMapIter iter;
    PathMap_iter_init(&iter, completion->executables);

    const PChar *name;
    const PChar **location;
    while (PathMap_iter_next(&iter, &name, &location)) {
        if (p_strncmp(name, prefix, prefix_length) == 0 && add_match(matches, name) != 0) {
            return -1;
        }
    }

    return 0;
}

/** Returns a heap copy of the text with backslash escapes resolved (`\x` becomes `x`) */
static PChar *unescape(const PChar *text, size_t length) {
    PChar *result = malloc((length + 1) * sizeof(PChar));
    if (result == NULL) {
        return NULL;
    }

    size_t out = 0;
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\\' && i + 1 < length && tokenizer_is_escapable(text[i + 1])) {
            i++;
        }
        result[out++] = text[i];
    }
    result[out] = '\0';
    return result;
}

/** Characters that would change how the shell reads a word, so they are escaped in a file name */
static int needs_escape(PChar c, int at_word_start) {
    if (c == '~') {
        return at_word_start;
    }
    return c != '\0' && p_strchr(P(" \t'\"\\$`<>|&;()*?[]#"), c) != NULL && tokenizer_is_escapable(c);
}

/** Joins the directory as typed, the escaped file name and a `/` for directories */
static PChar *build_candidate(const PChar *typed_directory, size_t typed_directory_length,
                              const PChar *name, int is_directory) {
    const size_t name_length = p_strlen(name);
    PChar *candidate = malloc((typed_directory_length + 2 * name_length + 2) * sizeof(PChar));
    if (candidate == NULL) {
        return NULL;
    }

    size_t out = 0;
    p_memcpy(candidate, typed_directory, typed_directory_length);
    out += typed_directory_length;

    for (size_t i = 0; i < name_length; i++) {
        if (needs_escape(name[i], typed_directory_length == 0 && i == 0)) {
            candidate[out++] = '\\';
        }
        candidate[out++] = name[i];
    }

    if (is_directory) {
        candidate[out++] = '/';
    }
    candidate[out] = '\0';
    return candidate;
}

static int complete_path(const PChar *word, StrVec *matches) {
    // Split the word, as typed, into the directory part (with its `/`) and the name being typed
    size_t typed_directory_length = 0;
    for (size_t i = 0; word[i] != '\0'; i++) {
        if (platform_is_path_separator(word[i])) {
            typed_directory_length = i + 1;
        }
    }
    const PChar *typed_name = word + typed_directory_length;

    AUTO_FREE PChar *directory = unescape(word, typed_directory_length);
    AUTO_FREE PChar *name_prefix = unescape(typed_name, p_strlen(typed_name));
    if (directory == NULL || name_prefix == NULL) {
        return -1;
    }

    // The directory to read: `~/x/` is relative to the home directory, no directory means `.`
    AUTO_FREE PChar *directory_to_open = NULL;
    const PChar *home = get_home_dir();
    if (home != NULL && directory[0] == '~' &&
        (platform_is_path_separator(directory[1]) || directory[1] == '\0')) {
        const size_t length = p_strlen(home) + p_strlen(directory + 1) + 1;
        directory_to_open = malloc(length * sizeof(PChar));
        if (directory_to_open == NULL) {
            return -1;
        }
        p_snprintf(directory_to_open, length, P("%" PS "%" PS), home, directory + 1);
    } else {
        directory_to_open = p_strdup(directory[0] != '\0' ? directory : P("."));
        if (directory_to_open == NULL) {
            return -1;
        }
    }

    AUTO_CLOSE_DIR PlatformDir *dir = platform_dir_open(directory_to_open);
    if (dir == NULL) {
        return 0; // Not a readable directory: nothing to offer
    }

    const size_t name_prefix_length = p_strlen(name_prefix);
    const PChar *name;
    int is_directory;
    int status;
    while ((status = platform_dir_read(dir, &name, &is_directory)) == 1) {
        if (name[0] == '.' && name_prefix[0] != '.') {
            continue; // Hidden files are only offered when asked for
        }
        if (p_strncmp(name, name_prefix, name_prefix_length) != 0) {
            continue;
        }

        AUTO_FREE PChar *candidate = build_candidate(word, typed_directory_length, name, is_directory);
        if (candidate == NULL) {
            return -1;
        }
        if (StrVec_push(matches, candidate) != 0) {
            return -1;
        }
        DISOWN(candidate); // now owned by matches
    }

    return status;
}

static int is_blank(PChar c) {
    return c == ' ' || c == '\t';
}

/** Returns a heap copy of line[begin, end) */
static PChar *copy_range(const PChar *line, size_t begin, size_t end) {
    PChar *copy = malloc((end - begin + 1) * sizeof(PChar));
    if (copy == NULL) {
        return NULL;
    }
    p_memcpy(copy, line + begin, end - begin);
    copy[end - begin] = '\0';
    return copy;
}

/** Adds every non-empty line of `output` as a candidate. Returns 0, or -1 on failure */
static int add_output_lines(StrVec *matches, const PChar *output) {
    const PChar *line_start = output;
    while (*line_start != '\0') {
        const PChar *line_end = line_start;
        while (*line_end != '\0' && *line_end != '\n') {
            line_end++;
        }

        size_t length = (size_t)(line_end - line_start);
        if (length > 0 && line_start[length - 1] == '\r') {
            length--;
        }

        if (length > 0) {
            AUTO_FREE PChar *candidate = malloc((length + 1) * sizeof(PChar));
            if (candidate == NULL) {
                return -1;
            }
            p_memcpy(candidate, line_start, length);
            candidate[length] = '\0';
            if (StrVec_push(matches, candidate) != 0) {
                return -1;
            }
            DISOWN(candidate); // now owned by matches
        }

        line_start = *line_end == '\n' ? line_end + 1 : line_end;
    }

    return 0;
}

/**
 * Asks the registered completer for the candidates of the word at `word_start`. The command is
 * the first word of the pipeline stage, which starts at `command_begin`.
 */
static int complete_with_script(const PChar *line, size_t command_begin, size_t command_end,
                                size_t word_start, const PChar *script, StrVec *matches) {
    AUTO_FREE PChar *command = copy_range(line, command_begin, command_end);
    if (command == NULL) {
        return -1;
    }

    // The word before the one being completed (the command itself, when it is the second word)
    size_t previous_end = word_start;
    while (previous_end > command_begin && is_blank(line[previous_end - 1])) {
        previous_end--;
    }
    size_t previous_begin = previous_end;
    while (previous_begin > command_begin && !is_blank(line[previous_begin - 1])) {
        previous_begin--;
    }
    AUTO_FREE PChar *previous_word = copy_range(line, previous_begin, previous_end);
    if (previous_word == NULL) {
        return -1;
    }

    // The cursor is always at the end of the line: the editor only appends
    AUTO_FREE char *line_utf8 = p_to_utf8(line);
    if (line_utf8 == NULL) {
        return -1;
    }
    PChar comp_point[32];
    p_snprintf(comp_point, sizeof comp_point / sizeof comp_point[0], P("%lu"),
               (unsigned long)strlen(line_utf8));

    // 9 is TAB: the completion key and the "normal completion" type of bash
    const EnvVar environment[] = {
        {P("COMP_LINE"), line},
        {P("COMP_POINT"), comp_point},
        {P("COMP_KEY"), P("9")},
        {P("COMP_TYPE"), P("9")},
        {NULL, NULL},
    };

    PChar *const args[] = {(PChar *)script, command, (PChar *)(line + word_start), previous_word, NULL};

    AUTO_FREE PChar *output = NULL;
    if (run_capture(script, args, environment, &output) < 0) {
        return 0; // The script could not be run: nothing to offer, which rings the bell
    }

    return add_output_lines(matches, output);
}

int complete_command_line(const PChar *line, size_t word_start, StrVec *matches, int *whole_names,
                          void *context) {
    const CompletionContext *completion = context;
    const PChar *word = line + word_start;

    // A command name is expected at the start of the line and after a `|` (quotes are not considered)
    size_t command_start = 0;
    for (size_t i = 0; i < word_start; i++) {
        if (line[i] == '|') {
            command_start = i + 1;
        }
    }

    size_t command_begin = command_start;
    while (command_begin < word_start && is_blank(line[command_begin])) {
        command_begin++;
    }
    const int is_first_word = command_begin == word_start;

    if (is_first_word && p_strchr(word, '/') == NULL) {
        return complete_command_name(word, matches, completion);
    }

    if (!is_first_word && completion->completers != NULL) {
        size_t command_end = command_begin;
        while (command_end < word_start && !is_blank(line[command_end])) {
            command_end++;
        }

        AUTO_FREE PChar *command = copy_range(line, command_begin, command_end);
        if (command == NULL) {
            return -1;
        }
        const PChar *script = completers_find(completion->completers, command);
        if (script != NULL) {
            *whole_names = 1;
            return complete_with_script(line, command_begin, command_end, word_start, script, matches);
        }
    }

    return complete_path(word, matches);
}
