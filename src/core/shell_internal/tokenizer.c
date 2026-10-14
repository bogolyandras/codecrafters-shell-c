#include "shell_internal/tokenizer.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "raii.h"
#include "shell_builtin/variables.h"
#include "shell_internal/tilde_expansion.h"

static int is_blank(PChar c) {
    return c == ' ' || c == '\t';
}

int tokenizer_is_escapable(PChar c) {
    return platform_backslash_escapes(c);
}

/** Inside double quotes a backslash only escapes these characters, otherwise it is literal */
static int is_double_quote_escapable(PChar c) {
    return platform_double_quote_backslash_escapes(c);
}

/** The characters of the word being built. It grows, because a variable can be longer than its name */
typedef struct {
    PChar *data; /** Owned, `capacity` units */
    size_t length;
    size_t capacity;
} WordBuffer;

static void word_buffer_free(WordBuffer *word) {
    free(word->data);
}

/** Makes room for `extra` more units and the terminator. Returns 0, or -1 with ENOMEM */
static int word_reserve(WordBuffer *word, size_t extra) {
    const size_t needed = word->length + extra + 1;
    if (needed <= word->capacity) {
        return 0;
    }

    size_t capacity = word->capacity > 0 ? word->capacity : 64;
    while (capacity < needed) {
        capacity *= 2;
    }

    PChar *grown = realloc(word->data, capacity * sizeof(PChar));
    if (grown == NULL) {
        return -1;
    }
    word->data = grown;
    word->capacity = capacity;
    return 0;
}

static int word_append(WordBuffer *word, const PChar *text, size_t length) {
    if (word_reserve(word, length) != 0) {
        return -1;
    }
    p_memcpy(word->data + word->length, text, length);
    word->length += length;
    return 0;
}

static int word_append_char(WordBuffer *word, PChar c) {
    return word_append(word, &c, 1);
}

/** The variables of the line: `lookup` is NULL when there are none (everything is unset) */
typedef struct {
    TokenizerVariableLookup lookup;
    void *context;
} Variables;

/**
 * Expands the `$name` or `${name}` at `*cursor` (which is at the `$`) into `word` and moves the
 * cursor past it. A `$` that does not start a name stays a literal `$`. Returns 0, or -1 with
 * errno set (TOKENIZE_BAD_SUBSTITUTION or ENOMEM).
 */
static int expand_variable(const PChar **cursor, WordBuffer *word, const Variables *variables) {
    const PChar *p = *cursor + 1;
    const PChar *name_start = p;
    const PChar *name_end;
    const PChar *after;

    if (*p == '{') {
        name_start = ++p;
        name_end = p_strchr(p, '}');
        if (name_end == NULL || name_end == name_start || !variable_is_name_start(*name_start)) {
            errno = TOKENIZE_BAD_SUBSTITUTION; // `${`, `${}`, `${1}`
            return -1;
        }
        for (const PChar *c = name_start; c < name_end; c++) {
            if (!variable_is_name_char(*c)) {
                errno = TOKENIZE_BAD_SUBSTITUTION; // `${a-b}`
                return -1;
            }
        }
        after = name_end + 1;
    } else if (variable_is_name_start(*p)) {
        name_end = p;
        while (variable_is_name_char(*name_end)) {
            name_end++;
        }
        after = name_end;
    } else {
        *cursor += 1;
        return word_append_char(word, '$'); // `$` alone, `$1`, `$?`: not a variable
    }

    const size_t name_length = (size_t)(name_end - name_start);
    if (variables->lookup != NULL) {
        AUTO_FREE PChar *name = malloc((name_length + 1) * sizeof(PChar));
        if (name == NULL) {
            return -1;
        }
        p_memcpy(name, name_start, name_length);
        name[name_length] = '\0';

        const PChar *value = variables->lookup(name, variables->context);
        if (value != NULL && word_append(word, value, p_strlen(value)) != 0) {
            return -1;
        }
    }

    *cursor = after;
    return 0;
}

/**
 * Appends the characters of one word, with quotes, escapes and variables resolved, to `word` and
 * advances `cursor` to the end of the word: a blank, an unquoted `>`, `|` or `&`, or the end of
 * the input. `*quoted` is set to non-zero if the word had a quote in it. Returns 0 on success,
 * -1 with errno set: TOKENIZE_UNTERMINATED_QUOTE, TOKENIZE_BAD_SUBSTITUTION or ENOMEM.
 */
static int parse_word(const PChar **cursor, WordBuffer *word, const Variables *variables, int *quoted) {
    const PChar *p = *cursor;

    while (*p != '\0' && !is_blank(*p) && *p != '>' && *p != '|' && *p != '&') {
        if (*p == '\'') {
            *quoted = 1;
            const PChar *closing_quote = p_strchr(p + 1, '\'');
            if (closing_quote == NULL) {
                errno = TOKENIZE_UNTERMINATED_QUOTE;
                return -1;
            }

            if (word_append(word, p + 1, (size_t)(closing_quote - (p + 1))) != 0) {
                return -1;
            }
            p = closing_quote + 1;
        } else if (*p == '"') {
            *quoted = 1;
            p++;
            while (*p != '"') {
                if (*p == '\0') {
                    errno = TOKENIZE_UNTERMINATED_QUOTE;
                    return -1;
                }

                if (*p == '\\' && is_double_quote_escapable(p[1])) {
                    // The backslash is removed, the next character is literal
                    if (word_append_char(word, p[1]) != 0) {
                        return -1;
                    }
                    p += 2;
                } else if (*p == '$') {
                    if (expand_variable(&p, word, variables) != 0) {
                        return -1;
                    }
                } else {
                    if (word_append_char(word, *p++) != 0) {
                        return -1;
                    }
                }
            }
            p++; // Skip the closing quote
        } else if (*p == '\\' && p[1] != '\0' && tokenizer_is_escapable(p[1])) {
            // The backslash is removed, the next character is literal
            if (word_append_char(word, p[1]) != 0) {
                return -1;
            }
            p += 2;
        } else if (*p == '$') {
            if (expand_variable(&p, word, variables) != 0) {
                return -1;
            }
        } else {
            if (word_append_char(word, *p++) != 0) {
                return -1;
            }
        }
    }

    *cursor = p;
    return 0;
}

static void command_free(Command *command) {
    StrVec_free_owned(&command->tokens);
    redirections_free(&command->redirections);
}

/** Leaves the command empty and usable. Even when it fails the command is safe to command_free */
static int command_init(Command *command) {
    memset(command, 0, sizeof *command);
    return StrVec_init(&command->tokens);
}

static int command_is_empty(const Command *command) {
    return StrVec_size(&command->tokens) == 0 &&
           command->redirections.stdout_target.path == NULL &&
           command->redirections.stderr_target.path == NULL;
}

void CommandVec_clear_owned(CommandVec *commands) {
    for (size_t i = 0; i < CommandVec_size(commands); i++) {
        command_free(CommandVec_get(commands, i));
    }

    CommandVec_clear(commands);
}

void CommandVec_free_owned(CommandVec *commands) {
    CommandVec_clear_owned(commands);
    CommandVec_free(commands);
}

int tokenize(const PChar *input, CommandVec *commands, TokenizerVariableLookup lookup, void *lookup_context) {
    const PChar *home = get_home_dir();
    const size_t home_length = home != NULL ? p_strlen(home) : 0;
    const Variables variables = {lookup, lookup_context};

    CLEANUP(word_buffer_free) WordBuffer word = {0};
    if (word_reserve(&word, p_strlen(input) + home_length) != 0) {
        return -1;
    }

    // The command being read. It is released on failure, and handed to `commands` when it ends
    CLEANUP(command_free) Command current = {0};
    if (command_init(&current) != 0) {
        return -1;
    }

    int pending_redirect_fd = 0; // The descriptor whose redirection target is expected next, 0 for none
    int pending_append = 0;
    int background = 0; // The line ended with `&`

    const PChar *p = input;
    while (1) {
        while (is_blank(*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        const PChar *word_start = p;
        word.length = 0;

        if (p[0] == '~' && (p[1] == '\0' || p[1] == '/' || p[1] == '>' || p[1] == '|' || p[1] == '&' || is_blank(p[1])) &&
            home != NULL) {
            if (word_append(&word, home, home_length) != 0) {
                return -1;
            }
            p++;
        }

        int quoted = 0;
        if (parse_word(&p, &word, &variables, &quoted) != 0) {
            return -1;
        }
        word.data[word.length] = '\0';

        const int at_redirect_operator = *p == '>';
        const int at_pipe = *p == '|';
        const int at_ampersand = *p == '&';
        int has_word = p != word_start;

        // Variables that are unset and not quoted leave nothing behind: `echo $none x` has one argument
        if (has_word && word.length == 0 && !quoted) {
            has_word = 0;
        }

        // A lone, unquoted `1` or `2` directly before `>` names the descriptor and is not an argument
        int operator_fd = 1;
        if (at_redirect_operator && p - word_start == 1 && (*word_start == '1' || *word_start == '2')) {
            operator_fd = (int)(*word_start - '0');
            has_word = 0;
        }

        if (has_word) {
            AUTO_FREE PChar *copy = p_strdup(word.data);
            if (copy == NULL) {
                return -1;
            }

            if (pending_redirect_fd != 0) {
                RedirectTarget *target = pending_redirect_fd == 2 ? &current.redirections.stderr_target
                                                                  : &current.redirections.stdout_target;
                free(target->path);
                target->path = TAKE(copy);
                target->append = pending_append;
                pending_redirect_fd = 0;
            } else {
                if (StrVec_push(&current.tokens, copy) != 0) {
                    return -1;
                }
                DISOWN(copy); // now owned by the command
            }
        }

        if (at_redirect_operator) {
            if (pending_redirect_fd != 0) {
                errno = TOKENIZE_MISSING_REDIRECT_TARGET; // `> >`
                return -1;
            }
            pending_redirect_fd = operator_fd;
            pending_append = p[1] == '>';
            p += pending_append ? 2 : 1;
        }

        if (at_pipe) {
            if (pending_redirect_fd != 0) {
                errno = TOKENIZE_MISSING_REDIRECT_TARGET; // `> |`
                return -1;
            }
            if (command_is_empty(&current)) {
                errno = TOKENIZE_EMPTY_COMMAND; // `| x` or `x | | y`
                return -1;
            }

            if (CommandVec_push(commands, current) != 0) {
                return -1;
            }
            // The commands own it now: start over with a fresh one
            if (command_init(&current) != 0) {
                return -1;
            }
            p++;
        }

        if (at_ampersand) {
            if (pending_redirect_fd != 0) {
                errno = TOKENIZE_MISSING_REDIRECT_TARGET; // `> &`
                return -1;
            }
            if (command_is_empty(&current)) {
                errno = TOKENIZE_EMPTY_COMMAND; // `&` alone, or `x | &`
                return -1;
            }

            p++;
            while (is_blank(*p)) {
                p++;
            }
            if (*p != '\0') {
                errno = TOKENIZE_MISPLACED_AMPERSAND; // `a & b`, `a && b`
                return -1;
            }
            background = 1;
        }
    }

    if (pending_redirect_fd != 0) {
        errno = TOKENIZE_MISSING_REDIRECT_TARGET;
        return -1;
    }

    current.background = background;

    if (command_is_empty(&current)) {
        if (CommandVec_size(commands) > 0) {
            errno = TOKENIZE_EMPTY_COMMAND; // The line ends with `|`
            return -1;
        }
        return 0; // A blank line
    }

    if (CommandVec_push(commands, current) != 0) {
        return -1;
    }
    memset(&current, 0, sizeof current); // Owned by the commands now

    return 0;
}
