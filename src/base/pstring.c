#include "pstring.h"

#include <stdlib.h>

PChar *p_strdup(const PChar *text) {
    const size_t length = p_strlen(text);

    PChar *copy = malloc((length + 1) * sizeof(PChar));
    if (copy == NULL) {
        return NULL;
    }

    p_memcpy(copy, text, length + 1);
    return copy;
}
