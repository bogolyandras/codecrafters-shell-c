#include "shell_builtin/completers.h"

#include <errno.h>
#include <stdlib.h>

#include "raii.h"

int completers_init(CompleterTable *table) {
    return CompleterEntryVec_init(&table->entries);
}

static void free_entry(CompleterEntry *entry) {
    free(entry->command);
    free(entry->script);
    entry->command = NULL;
    entry->script = NULL;
}

void completers_clear(CompleterTable *table) {
    for (size_t i = 0; i < CompleterEntryVec_size(&table->entries); i++) {
        free_entry(CompleterEntryVec_get(&table->entries, i));
    }
    CompleterEntryVec_clear(&table->entries);
}

void completers_free(CompleterTable *table) {
    if (table == NULL) {
        return;
    }
    completers_clear(table);
    CompleterEntryVec_free(&table->entries);
}

static CompleterEntry *find_entry(const CompleterTable *table, const PChar *command) {
    for (size_t i = 0; i < CompleterEntryVec_size(&table->entries); i++) {
        CompleterEntry *entry = (CompleterEntry *)CompleterEntryVec_get_const(&table->entries, i);
        if (p_strcmp(entry->command, command) == 0) {
            return entry;
        }
    }
    return NULL;
}

int completers_set(CompleterTable *table, const PChar *command, const PChar *script) {
    AUTO_FREE PChar *script_copy = p_strdup(script);
    if (script_copy == NULL) {
        return -1;
    }

    CompleterEntry *existing = find_entry(table, command);
    if (existing != NULL) {
        free(existing->script);
        existing->script = TAKE(script_copy);
        return 0;
    }

    AUTO_FREE PChar *command_copy = p_strdup(command);
    if (command_copy == NULL) {
        return -1;
    }

    CompleterEntry entry = {command_copy, script_copy};
    if (CompleterEntryVec_push(&table->entries, entry) != 0) {
        return -1;
    }
    DISOWN(command_copy); // now owned by the table
    DISOWN(script_copy);
    return 0;
}

const PChar *completers_find(const CompleterTable *table, const PChar *command) {
    const CompleterEntry *entry = find_entry(table, command);
    return entry != NULL ? entry->script : NULL;
}

int completers_remove(CompleterTable *table, const PChar *command) {
    const size_t size = CompleterEntryVec_size(&table->entries);

    for (size_t i = 0; i < size; i++) {
        CompleterEntry *entry = CompleterEntryVec_get(&table->entries, i);
        if (p_strcmp(entry->command, command) != 0) {
            continue;
        }

        free_entry(entry);
        for (size_t j = i + 1; j < size; j++) {
            *CompleterEntryVec_get(&table->entries, j - 1) = *CompleterEntryVec_get(&table->entries, j);
        }
        table->entries.base.size--; // The Vector has no erase: the last slot is now a stale copy
        return 0;
    }

    errno = ENOENT;
    return -1;
}

size_t completers_size(const CompleterTable *table) {
    return CompleterEntryVec_size(&table->entries);
}

const CompleterEntry *completers_get(const CompleterTable *table, size_t index) {
    return CompleterEntryVec_get_const(&table->entries, index);
}
