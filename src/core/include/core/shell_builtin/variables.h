#ifndef BOGOLYANDRAS_VARIABLES_H
#define BOGOLYANDRAS_VARIABLES_H

#include <stddef.h>

#include "core/datastructs/vector.h"
#include "core/pstring.h"

/** Shell variables, made with `declare name=value`. Not exported to the programs that are run */
typedef struct {
    PChar *name; /** Owned */
    PChar *value; /** Owned. NULL for a variable that was declared without a value */
} Variable;

VECTOR_DEFINE(Variable, VariableVec)

/** The variables in the order they were first declared. They live as long as the shell */
typedef struct {
    VariableVec entries;
} VariableTable;

/** Returns non-zero for a character a variable name may start with: a letter or an underscore (ASCII) */
static inline int variable_is_name_start(PChar c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

/** Returns non-zero for a character a variable name may contain: a letter, a digit or an underscore */
static inline int variable_is_name_char(PChar c) {
    return variable_is_name_start(c) || (c >= '0' && c <= '9');
}

/** Returns non-zero if `name` is a valid identifier: not empty, no digit first */
int variable_name_is_valid(const PChar *name);

/** Returns 0 on success, -1 with errno set on failure. A failed table is safe to free */
int variables_init(VariableTable *table);

/** Releases the variables. NULL-safe and cleanup-attribute friendly */
void variables_free(VariableTable *table);

/**
 * Creates the variable or changes its value. A NULL `value` only declares it: a variable that
 * exists keeps its value. The name must be valid, which is the caller's check.
 * Returns 0, or -1 with errno set.
 */
int variables_set(VariableTable *table, const PChar *name, const PChar *value);

/** The variable called `name`, or NULL. The pointer is valid until the table changes */
const Variable *variables_find(const VariableTable *table, const PChar *name);

size_t variables_size(const VariableTable *table);

/** Returns the variable at `index` (declaration order), or NULL */
const Variable *variables_get(const VariableTable *table, size_t index);

#endif //BOGOLYANDRAS_VARIABLES_H
