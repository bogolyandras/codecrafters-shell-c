#ifndef BOGOLYANDRAS_HISTORY_H
#define BOGOLYANDRAS_HISTORY_H

#include <stddef.h>

#include "core/datastructs/vector.h"
#include "core/pstring.h"

typedef struct {
    PChar *text; /** Owned */
    int is_new; /** Entered in this session and not saved to a file yet */
} HistoryEntry;

VECTOR_DEFINE(HistoryEntry, HistoryEntryVec)

/**
 * The commands that were entered, oldest first. Entries are numbered from 1 in the order they
 * were added, and the number of an entry never changes.
 *
 * Files are plain UTF-8 text, one command per line. The module knows which entries are not in a
 * file yet, so that appending to a file never repeats what it already holds.
 */
typedef struct {
    HistoryEntryVec entries;
} History;

/** Returns 0 on success, -1 with errno set on failure. A failed history is safe to free */
int history_init(History *history);

/** Releases the entries. NULL-safe and cleanup-attribute friendly */
void history_free(History *history);

/** Appends a copy of the line, as a new entry. Returns 0 on success, -1 with errno set on failure */
int history_add(History *history, const PChar *line);

/**
 * Appends the lines of a file to the history, one entry per line, skipping blank ones and
 * tolerating `\r\n` line endings. The entries are not new: they came from a file. Returns 0 on
 * success, or -1 with errno set if the file cannot be opened or read; the lines read before a
 * failure stay in the history.
 */
int history_load(History *history, const PChar *path);

/**
 * Writes the history to a file. With `append` zero the file is replaced by the whole history;
 * with `append` non-zero only the new entries are added to the end of the file (which is created
 * if it is missing). The written entries are no longer new afterwards. Returns 0 on success, or
 * -1 with errno set, in which case no entry is marked as saved.
 */
int history_save(History *history, const PChar *path, int append);

size_t history_size(const History *history);

/** Returns the entry at `index` (0 is the oldest, the entry number is index + 1), or NULL */
const PChar *history_get(const History *history, size_t index);

#endif //BOGOLYANDRAS_HISTORY_H
