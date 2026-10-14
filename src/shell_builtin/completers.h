#ifndef BOGOLYANDRAS_COMPLETERS_H
#define BOGOLYANDRAS_COMPLETERS_H

#include <stddef.h>

#include "datastructs/vector.h"
#include "pstring.h"

typedef struct {
    PChar *command; /** The command whose arguments are completed, owned */
    PChar *script; /** The program that produces the candidates, owned */
} CompleterEntry;

VECTOR_DEFINE(CompleterEntry, CompleterEntryVec)

/**
 * The completers registered with `complete -C script command`, in registration order. A command
 * has at most one: registering it again replaces the script. The table lives as long as the
 * shell and is not saved anywhere.
 */
typedef struct {
    CompleterEntryVec entries;
} CompleterTable;

/** Returns 0 on success, -1 with errno set on failure. A failed table is safe to free */
int completers_init(CompleterTable *table);

/** Releases the entries. NULL-safe and cleanup-attribute friendly */
void completers_free(CompleterTable *table);

/** Registers (or replaces) the completer of `command`. Returns 0, or -1 with errno set */
int completers_set(CompleterTable *table, const PChar *command, const PChar *script);

/** The script registered for `command`, or NULL. The pointer is valid until the table changes */
const PChar *completers_find(const CompleterTable *table, const PChar *command);

/** Unregisters `command`. Returns 0, or -1 with errno set to ENOENT if it was not registered */
int completers_remove(CompleterTable *table, const PChar *command);

/** Unregisters everything */
void completers_clear(CompleterTable *table);

size_t completers_size(const CompleterTable *table);

/** Returns the entry at `index` (registration order), or NULL */
const CompleterEntry *completers_get(const CompleterTable *table, size_t index);

#endif //BOGOLYANDRAS_COMPLETERS_H
