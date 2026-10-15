#include "core/shell_builtin/history.h"

#include <stdlib.h>
#include <string.h>

#include "core/platform.h"
#include "core/raii.h"

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

int history_load(History *history, const PChar *path) {
    size_t size = 0;
    AUTO_FREE char *content = platform_read_file(path, &size);
    if (content == NULL) {
        return -1;
    }

    // `content` ends with a 0 that platform_read_file adds, so each line can be ended in place
    size_t position = 0;
    while (position < size) {
        char *line = content + position;
        char *newline = memchr(line, '\n', size - position);
        const size_t length = newline != NULL ? (size_t)(newline - line) : size - position;
        position += length + 1;

        line[length] = '\0';
        if (length > 0 && line[length - 1] == '\r') {
            line[length - 1] = '\0';
        }

        // Blank lines are not commands
        const char *text = line;
        while (*text == ' ' || *text == '\t') {
            text++;
        }
        if (*text == '\0') {
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

    return 0;
}

int history_save(History *history, const PChar *path, int append) {
    // The whole text is built first and written in one go, so a failure leaves no half written entry
    size_t capacity = 1024;
    size_t length = 0;
    AUTO_FREE char *text = malloc(capacity);
    if (text == NULL) {
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

        const size_t line_length = strlen(utf8);
        while (length + line_length + 1 > capacity) {
            capacity *= 2;
            char *larger = realloc(text, capacity);
            if (larger == NULL) {
                return -1;
            }
            text = larger;
        }
        memcpy(text + length, utf8, line_length);
        length += line_length;
        text[length++] = '\n';
    }

    if (platform_write_file(path, text, length, append) != 0) {
        return -1;
    }

    // Only now that everything is in the file are the entries no longer new
    for (size_t i = 0; i < count; i++) {
        HistoryEntryVec_get(&history->entries, i)->is_new = 0;
    }

    return 0;
}
