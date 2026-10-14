#include "shell_builtin/history.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raii.h"

int history_init(History *history) {
    return HistoryEntryVec_init(&history->entries);
}

void history_free(History *history) {
    for (size_t i = 0; i < HistoryEntryVec_size(&history->entries); i++) {
        free(HistoryEntryVec_get(&history->entries, i)->text);
    }

    HistoryEntryVec_free(&history->entries);
}

static int add_entry(History *history, const PChar *line, int is_new) {
    AUTO_FREE PChar *copy = p_strdup(line);
    if (copy == NULL) {
        return -1;
    }

    const HistoryEntry entry = {copy, is_new};
    if (HistoryEntryVec_push(&history->entries, entry) != 0) {
        return -1;
    }
    DISOWN(copy); // now owned by the history
    return 0;
}

int history_add(History *history, const PChar *line) {
    return add_entry(history, line, 1);
}

size_t history_size(const History *history) {
    return HistoryEntryVec_size(&history->entries);
}

const PChar *history_get(const History *history, size_t index) {
    const HistoryEntry *entry = HistoryEntryVec_get_const(&history->entries, index);
    return entry != NULL ? entry->text : NULL;
}

/** Reads one line of any length without its line ending. NULL at the end of the file or on failure */
static char *read_line(FILE *file) {
    size_t capacity = 128;
    size_t length = 0;

    AUTO_FREE char *line = malloc(capacity);
    if (line == NULL) {
        return NULL;
    }

    while (fgets(line + length, (int)(capacity - length), file) != NULL) {
        length += strlen(line + length);

        if (length > 0 && line[length - 1] == '\n') {
            line[--length] = '\0';
            if (length > 0 && line[length - 1] == '\r') {
                line[--length] = '\0';
            }
            return TAKE(line);
        }

        if (length + 1 < capacity) {
            continue; // fgets stopped early: the end of the file
        }

        capacity *= 2;
        char *larger = realloc(line, capacity);
        if (larger == NULL) {
            return NULL;
        }
        line = larger;
    }

    // The end of the file: a last line without a line ending still counts
    if (length > 0 && !ferror(file)) {
        if (line[length - 1] == '\r') {
            line[--length] = '\0';
        }
        return TAKE(line);
    }

    return NULL;
}

int history_load(History *history, const PChar *path) {
    AUTO_FCLOSE FILE *file = p_fopen(path, P("rb"));
    if (file == NULL) {
        return -1;
    }

    while (1) {
        AUTO_FREE char *line = read_line(file);
        if (line == NULL) {
            break;
        }

        // Blank lines are not commands
        const char *content = line;
        while (*content == ' ' || *content == '\t') {
            content++;
        }
        if (*content == '\0') {
            continue;
        }

        AUTO_FREE PChar *entry = p_from_utf8(line);
        if (entry == NULL) {
            return -1;
        }
        if (add_entry(history, entry, 0) != 0) {
            return -1;
        }
    }

    if (ferror(file)) {
        errno = EIO;
        return -1;
    }

    return 0;
}

int history_save(History *history, const PChar *path, int append) {
    AUTO_FCLOSE FILE *file = p_fopen(path, append ? P("ab") : P("wb"));
    if (file == NULL) {
        return -1;
    }

    const size_t count = HistoryEntryVec_size(&history->entries);
    for (size_t i = 0; i < count; i++) {
        const HistoryEntry *entry = HistoryEntryVec_get_const(&history->entries, i);
        if (append && !entry->is_new) {
            continue;
        }

        AUTO_FREE char *utf8 = p_to_utf8(entry->text);
        if (utf8 == NULL) {
            return -1;
        }

        if (fputs(utf8, file) == EOF || fputc('\n', file) == EOF) {
            return -1;
        }
    }

    if (fflush(file) != 0 || ferror(file)) {
        return -1;
    }

    // Only now that everything is in the file are the entries no longer new
    for (size_t i = 0; i < count; i++) {
        HistoryEntryVec_get(&history->entries, i)->is_new = 0;
    }

    return 0;
}
