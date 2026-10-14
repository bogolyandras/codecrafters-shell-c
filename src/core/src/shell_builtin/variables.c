#include "core/shell_builtin/variables.h"

#include <stdlib.h>

#include "core/raii.h"

int variable_name_is_valid(const PChar *name) {
    if (!variable_is_name_start(name[0])) {
        return 0;
    }
    for (const PChar *c = name + 1; *c != '\0'; c++) {
        if (!variable_is_name_char(*c)) {
            return 0;
        }
    }
    return 1;
}

int variables_init(VariableTable *table) {
    return VariableVec_init(&table->entries);
}

void variables_free(VariableTable *table) {
    if (table == NULL) {
        return;
    }
    for (size_t i = 0; i < VariableVec_size(&table->entries); i++) {
        Variable *variable = VariableVec_get(&table->entries, i);
        free(variable->name);
        free(variable->value);
    }
    VariableVec_free(&table->entries);
}

const Variable *variables_find(const VariableTable *table, const PChar *name) {
    for (size_t i = 0; i < VariableVec_size(&table->entries); i++) {
        const Variable *variable = VariableVec_get_const(&table->entries, i);
        if (p_strcmp(variable->name, name) == 0) {
            return variable;
        }
    }
    return NULL;
}

int variables_set(VariableTable *table, const PChar *name, const PChar *value) {
    AUTO_FREE PChar *value_copy = NULL;
    if (value != NULL) {
        value_copy = p_strdup(value);
        if (value_copy == NULL) {
            return -1;
        }
    }

    Variable *existing = (Variable *)variables_find(table, name);
    if (existing != NULL) {
        if (value_copy != NULL) {
            free(existing->value);
            existing->value = TAKE(value_copy);
        }
        return 0;
    }

    AUTO_FREE PChar *name_copy = p_strdup(name);
    if (name_copy == NULL) {
        return -1;
    }

    Variable variable = {name_copy, value_copy};
    if (VariableVec_push(&table->entries, variable) != 0) {
        return -1;
    }
    DISOWN(name_copy); // now owned by the table
    DISOWN(value_copy);
    return 0;
}

size_t variables_size(const VariableTable *table) {
    return VariableVec_size(&table->entries);
}

const Variable *variables_get(const VariableTable *table, size_t index) {
    return VariableVec_get_const(&table->entries, index);
}
