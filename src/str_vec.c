#include "str_vec.h"

#include <stdlib.h>

void StrVec_clear_owned(StrVec *vec) {
    for (size_t i = 0; i < StrVec_size(vec); i++) {
        free(*StrVec_get(vec, i));
    }

    StrVec_clear(vec);
}

void StrVec_free_owned(StrVec *vec) {
    StrVec_clear_owned(vec);
    StrVec_free(vec);
}
