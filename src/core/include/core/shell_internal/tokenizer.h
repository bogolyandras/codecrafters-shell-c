#ifndef BOGOLYANDRAS_TOKENIZER_H
#define BOGOLYANDRAS_TOKENIZER_H

#include <errno.h>

#include "core/redirection.h"
#include "core/str_vec.h"

/** errno values set by tokenize besides ENOMEM */
#define TOKENIZE_UNTERMINATED_QUOTE EINVAL
#define TOKENIZE_MISSING_REDIRECT_TARGET EBADMSG
#define TOKENIZE_EMPTY_COMMAND ENODATA /** A `|` with no command before or after it */
#define TOKENIZE_MISPLACED_AMPERSAND EPROTO /** A `&` that is not the last thing on the line */
#define TOKENIZE_BAD_SUBSTITUTION EILSEQ /** A `${` that is not `${name}` with a valid name */

/**
 * Returns the value of the variable `name`, or NULL if it is not set. The text only has to stay
 * valid until the next call: the tokenizer copies it at once.
 */
typedef const PChar *(*TokenizerVariableLookup)(const PChar *name, void *context);

/** One simple command: its words and its redirections */
typedef struct {
    StrVec tokens; /** Owns its strings */
    Redirections redirections; /** Owns its strings */
    int background; /** Non-zero for the last command of a line that ends with `&` */
} Command;

VECTOR_DEFINE(Command, CommandVec)

/** Release the commands of a CommandVec together with the strings they own */
void CommandVec_clear_owned(CommandVec *commands);
void CommandVec_free_owned(CommandVec *commands);

/**
 * Returns non-zero if an unquoted backslash before `c` is an escape. On POSIX that is any
 * character. On Windows a backslash is mostly a path separator (`C:\Users\me`), so it only
 * escapes blanks, quotes, `>` and `~`; before anything else it stays a literal backslash.
 */
int tokenizer_is_escapable(PChar c);

/**
 * Splits a command line into the commands of a pipeline and appends them to `commands`, which
 * owns everything (release with CommandVec_clear_owned / CommandVec_free_owned). A blank line
 * gives no commands, and a line without `|` gives exactly one. Redirections are not words, they
 * are stored in the command they belong to.
 *
 *  - words are separated by unquoted blanks (spaces and tabs)
 *  - inside single or double quotes every character is literal, including `~`, blanks, `>` and
 *    the other kind of quote; the quotes themselves are removed, and quoted and unquoted parts of
 *    one word are joined ('a'"b"c -> abc)
 *  - outside quotes a backslash removes the special meaning of the next character and is itself
 *    removed (`\ ` is a literal space, `\'` a literal quote, `\\` a backslash), see
 *    tokenizer_is_escapable for the Windows difference; a backslash at the very end of the
 *    input stays literal
 *  - inside double quotes a backslash only escapes `"`, `\`, `$` and a backtick (only `"` on
 *    Windows); before any other character it is literal. Inside single quotes a backslash is
 *    always literal
 *  - an unquoted `~` at the start of a word, followed by `/` or the end of the word, expands to
 *    the home directory
 *  - `$name` and `${name}` (a letter or underscore, then letters, digits and underscores) are
 *    replaced by the value `lookup` gives, outside quotes and inside double quotes, never inside
 *    single quotes or after a backslash. An unset variable is the empty string; a word that is
 *    nothing but unquoted unset variables disappears (`echo $none x` has one argument). A `$` not
 *    followed by a name or `{` (`$`, `$1`, `$?`) stays as it is, and `${` without a valid name
 *    and `}` is TOKENIZE_BAD_SUBSTITUTION. The value is used as it is: it is not split into
 *    words, and not scanned for quotes or operators again
 *  - an unquoted `|` ends a command: its output feeds the next one. It must have a command on
 *    both sides (TOKENIZE_EMPTY_COMMAND otherwise)
 *  - an unquoted `&` must be the last thing on the line: the line is to run in the background
 *    (`background` is set on its last command). Anywhere else, or with nothing before it, it is
 *    TOKENIZE_MISPLACED_AMPERSAND / TOKENIZE_EMPTY_COMMAND
 *  - an unquoted `>` or `1>` redirects standard output, and `2>` standard error, to the word
 *    that follows it, truncating the file; `>>`, `1>>` and `2>>` append instead (the 1 or 2 must
 *    be a word of its own, directly before the operator); the last redirection of each kind wins
 *
 * Returns 0 on success, -1 with errno set on failure: TOKENIZE_UNTERMINATED_QUOTE,
 * TOKENIZE_MISSING_REDIRECT_TARGET, TOKENIZE_EMPTY_COMMAND, TOKENIZE_MISPLACED_AMPERSAND,
 * TOKENIZE_BAD_SUBSTITUTION or ENOMEM. Commands stored before a failure stay in `commands`.
 *
 * `lookup` may be NULL: every variable is then unset.
 */
int tokenize(const PChar *input, CommandVec *commands, TokenizerVariableLookup lookup, void *lookup_context);

#endif //BOGOLYANDRAS_TOKENIZER_H
