#include "core/shell_internal/line_editor.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "core/platform.h"
#include "core/pstring.h"
#include "core/raii.h"
#include "core/terminal.h"

#define KEY_CTRL_C 0x03
#define KEY_CTRL_D 0x04
#define KEY_TAB '\t'
#define KEY_ENTER_LF '\n'
#define KEY_ENTER_CR '\r'
#define KEY_CTRL_U 0x15
#define KEY_ESCAPE 0x1b
#define KEY_BACKSPACE_DEL 0x7f
#define KEY_BACKSPACE_BS 0x08

static void put(const PChar *text, size_t length) {
    terminal_write(text, length);
}

static void put_string(const PChar *text) {
    put(text, p_strlen(text));
}

static void ring_bell(void) {
    put(P("\a"), 1);
}

static int is_blank(PChar c) {
    return c == ' ' || c == '\t';
}

/** Reads a line without terminal handling, for pipes, files and scripts */
static int read_plain(const PChar *prompt, PChar *buffer, size_t size) {
    p_fputs(prompt, stdout);

    if (p_fgets(buffer, (int)size, stdin) == NULL) {
        return feof(stdin) ? LINE_EOF : LINE_ERROR;
    }

    PChar *newline = p_strchr(buffer, '\n');
    if (newline != NULL) {
        *newline = '\0';
        return LINE_OK;
    }

    if (feof(stdin)) {
        return LINE_OK; // The last line had no trailing newline
    }

    // The line did not fit: drop the rest of it
    PInt c;
    while ((c = p_getchar()) != '\n' && c != P_EOF) {
    }
    return LINE_TOO_LONG;
}

static void restore_terminal(const int *unused) {
    (void)unused;
    terminal_restore();
}

/** The variable only exists to trigger terminal_restore when it goes out of scope */
#define AUTO_RESTORE_TERMINAL CLEANUP(restore_terminal)

static int compare_strings(const void *a, const void *b) {
    return p_strcmp(*(const PChar *const *)a, *(const PChar *const *)b);
}

/** The part of a candidate worth showing in a list: the last path component, directories keep their `/` */
static const PChar *display_name(const PChar *candidate) {
    const size_t length = p_strlen(candidate);

    for (size_t i = length > 1 ? length - 1 : 0; i > 0; i--) {
        if (platform_is_path_separator(candidate[i - 1])) {
            return candidate + i;
        }
    }

    return candidate;
}

/** Erases the last character on screen and in the buffer, aware of multi unit characters */
static void erase_last_character(PChar *buffer, size_t *length) {
    while (*length > 0 && p_is_continuation(buffer[*length - 1])) {
        (*length)--;
    }
    if (*length > 0) {
        (*length)--;
    }

    put(P("\b \b"), 3);
}

static void redraw(const PChar *prompt, const PChar *buffer, size_t length) {
    put_string(prompt);
    put(buffer, length);
}

/**
 * Replaces everything after `keep_end` in the buffer with `text` followed by `space_length`
 * spaces, updating the screen. Returns 0 without changing anything if it does not fit.
 */
static int replace_tail(PChar *buffer, size_t *length, size_t size, size_t keep_end,
                        const PChar *text, size_t text_length, size_t space_length) {
    if (keep_end + text_length + space_length >= size) {
        return 0;
    }

    while (*length > keep_end) {
        erase_last_character(buffer, length);
    }

    p_memcpy(buffer + *length, text, text_length);
    p_memset(buffer + *length + text_length, ' ', space_length);
    put(buffer + *length, text_length + space_length);
    *length += text_length + space_length;
    return 1;
}

/**
 * Handles one TAB press. Returns non-zero if the word was ambiguous, so that an immediately
 * following TAB lists the candidates.
 */
static int complete_word(const PChar *prompt, PChar *buffer, size_t *length, size_t size,
                         LineCompleter completer, void *context, int second_tab) {
    if (completer == NULL) {
        ring_bell();
        return 0;
    }

    buffer[*length] = '\0';

    size_t word_start = *length;
    while (word_start > 0 && !is_blank(buffer[word_start - 1])) {
        word_start--;
    }
    const size_t prefix_length = *length - word_start;

    CLEANUP(StrVec_free_owned) StrVec matches = {0};
    if (StrVec_init(&matches) != 0) {
        ring_bell();
        return 0;
    }
    int whole_names = 0;
    if (completer(buffer, word_start, &matches, &whole_names, context) != 0 || StrVec_size(&matches) == 0) {
        ring_bell();
        return 0;
    }

    // Sorted, duplicate free view over the matches
    const size_t match_count = StrVec_size(&matches);
    PChar **sorted = StrVec_get(&matches, 0);
    qsort(sorted, match_count, sizeof(PChar *), compare_strings);

    AUTO_FREE const PChar **unique = malloc(match_count * sizeof(PChar *));
    if (unique == NULL) {
        ring_bell();
        return 0;
    }
    size_t unique_count = 0;
    for (size_t i = 0; i < match_count; i++) {
        if (unique_count == 0 || p_strcmp(unique[unique_count - 1], sorted[i]) != 0) {
            unique[unique_count++] = sorted[i];
        }
    }

    // Normally the typed word is a prefix of every candidate and only the rest is appended. If it
    // is spelled differently (such as `\m` for `m`) the whole word is replaced instead.
    const int word_is_prefix = p_strncmp(unique[0], buffer + word_start, prefix_length) == 0;
    const size_t kept_length = word_is_prefix ? prefix_length : 0;
    const size_t keep_end = word_start + kept_length;

    if (unique_count == 1) {
        const PChar *candidate = unique[0];
        const size_t candidate_length = p_strlen(candidate);

        // A directory is completed without a space, so the next TAB can continue inside it
        const size_t space_length = candidate_length > 0 && candidate[candidate_length - 1] == '/' ? 0 : 1;

        if (!replace_tail(buffer, length, size, keep_end, candidate + kept_length,
                          candidate_length - kept_length, space_length)) {
            ring_bell();
        }
        return 0;
    }

    size_t common_length = p_strlen(unique[0]);
    for (size_t i = 1; i < unique_count; i++) {
        size_t j = 0;
        while (j < common_length && unique[0][j] == unique[i][j]) {
            j++;
        }
        common_length = j;
    }

    if (common_length > kept_length) {
        if (!replace_tail(buffer, length, size, keep_end, unique[0] + kept_length,
                          common_length - kept_length, 0)) {
            ring_bell();
        }
        return 1;
    }

    if (!second_tab) {
        ring_bell();
        return 1;
    }

    put(P("\n"), 1);
    for (size_t i = 0; i < unique_count; i++) {
        put_string(whole_names ? unique[i] : display_name(unique[i]));
        put(P("  "), 2);
    }
    put(P("\n"), 1);
    redraw(prompt, buffer, *length);
    return 1;
}

typedef enum {
    ESCAPE_OTHER,
    ESCAPE_UP,
    ESCAPE_DOWN,
} EscapeKey;

/** Reads the rest of an escape sequence (arrow keys and similar) and names the key if it is one we use */
static EscapeKey read_escape_sequence(void) {
    PChar c;
    if (terminal_read_char(&c) != 1) {
        return ESCAPE_OTHER;
    }
    if (c != '[' && c != 'O') {
        return ESCAPE_OTHER; // A two byte sequence such as Alt+key
    }

    // Parameters and intermediates, then a final byte in 0x40..0x7e
    while (terminal_read_char(&c) == 1) {
        if (p_unit(c) >= 0x40 && p_unit(c) <= 0x7e) {
            if (c == 'A') {
                return ESCAPE_UP;
            }
            if (c == 'B') {
                return ESCAPE_DOWN;
            }
            return ESCAPE_OTHER;
        }
    }

    return ESCAPE_OTHER;
}

/**
 * Puts the history entry `*position` on the line, or the line that was being typed when the
 * position is the end of the history (`draft`). Returns 0 without changing anything if the text
 * does not fit.
 */
static int show_history_position(PChar *buffer, size_t *length, size_t size, const History *history,
                                 size_t position, const PChar *draft) {
    const PChar *text = position < history_size(history) ? history_get(history, position) : draft;
    return replace_tail(buffer, length, size, 0, text, p_strlen(text), 0);
}

static int read_interactive(const PChar *prompt, PChar *buffer, size_t size, const History *history,
                            LineCompleter completer, void *context) {
    if (terminal_enter_raw() != 0) {
        return read_plain(prompt, buffer, size);
    }
    // Raw mode must end on every way out of this function
    AUTO_RESTORE_TERMINAL int raw_mode_active = 1;
    (void)raw_mode_active;

    put_string(prompt);

    size_t length = 0;
    int last_tab_was_ambiguous = 0;

    // Walking through the history: `position` is an index into it, equal to its size for the line
    // being typed, which is kept in `draft` while an older entry is shown
    const size_t history_count = history != NULL ? history_size(history) : 0;
    size_t position = history_count;
    AUTO_FREE PChar *draft = NULL;

    while (1) {
        PChar c;
        const int status = terminal_read_char(&c);
        if (status < 0) {
            return LINE_ERROR;
        }
        if (status == 0) {
            if (length == 0) {
                return LINE_EOF;
            }
            break; // Input ended in the middle of a line: use what was typed
        }

        const int was_ambiguous = last_tab_was_ambiguous;
        last_tab_was_ambiguous = 0;

        const unsigned int key = p_unit(c);

        if (key == KEY_ENTER_LF || key == KEY_ENTER_CR) {
            put(P("\n"), 1);
            break;
        }

        switch (key) {
            case KEY_TAB:
                last_tab_was_ambiguous =
                    complete_word(prompt, buffer, &length, size, completer, context, was_ambiguous);
                break;

            case KEY_BACKSPACE_DEL:
            case KEY_BACKSPACE_BS:
                if (length > 0) {
                    erase_last_character(buffer, &length);
                }
                break;

            case KEY_CTRL_U:
                while (length > 0) {
                    erase_last_character(buffer, &length);
                }
                break;

            case KEY_CTRL_C:
                put(P("^C\n"), 3);
                buffer[0] = '\0';
                return LINE_OK; // An empty line: the caller shows a fresh prompt

            case KEY_CTRL_D:
                if (length == 0) {
                    return LINE_EOF;
                }
                ring_bell();
                break;

            case KEY_ESCAPE: {
                const EscapeKey escape_key = read_escape_sequence();
                if (history == NULL || escape_key == ESCAPE_OTHER) {
                    break;
                }

                if (escape_key == ESCAPE_UP && position > 0) {
                    if (position == history_count) {
                        // Leaving the line being typed: keep it for the way back
                        buffer[length] = '\0';
                        free(draft);
                        draft = p_strdup(buffer);
                        if (draft == NULL) {
                            ring_bell();
                            break;
                        }
                    }
                    if (show_history_position(buffer, &length, size, history, position - 1, draft)) {
                        position--;
                    } else {
                        ring_bell();
                    }
                } else if (escape_key == ESCAPE_DOWN && position < history_count) {
                    if (show_history_position(buffer, &length, size, history, position + 1, draft)) {
                        position++;
                    } else {
                        ring_bell();
                    }
                } else {
                    ring_bell(); // Already at the oldest or at the newest line
                }
                break;
            }

            default:
                if (key < 0x20) {
                    break; // Other control characters are ignored
                }
                if (length + 1 >= size) {
                    ring_bell();
                    break;
                }
                buffer[length++] = c;
                put(&c, 1);
                break;
        }
    }

    buffer[length] = '\0';
    return LINE_OK;
}

int line_read(const PChar *prompt, PChar *buffer, size_t size, const History *history,
              LineCompleter completer, void *context) {
    if (!terminal_is_interactive()) {
        return read_plain(prompt, buffer, size);
    }

    return read_interactive(prompt, buffer, size, history, completer, context);
}
