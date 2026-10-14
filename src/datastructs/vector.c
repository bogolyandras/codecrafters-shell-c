#include "vector.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define START_CAPACITY 20
#define GROWTH_FACTOR 2

static int vector_grow(Vector *v) {
    size_t new_capacity;
    if (v->capacity == 0) {
        new_capacity = START_CAPACITY;
    } else if (v->capacity > SIZE_MAX / GROWTH_FACTOR) {
        errno = ENOMEM; // capacity would overflow
        return -1;
    } else {
        new_capacity = v->capacity * GROWTH_FACTOR;
    }

    if (new_capacity > SIZE_MAX / v->element_size) {
        errno = ENOMEM; // allocation size would overflow
        return -1;
    }

    void *new_data = realloc(v->data, new_capacity * v->element_size);
    if (new_data == NULL) {
        return -1; // errno set by realloc
    }

    v->data = new_data;
    v->capacity = new_capacity;
    return 0;
}

int vector_init(Vector *v, size_t element_size) {
    if (v == NULL || element_size == 0) {
        errno = EINVAL;
        return -1;
    }

    // Start as a valid empty vector, so it stays usable (and freeable) if the allocation fails
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
    v->element_size = element_size;

    return vector_grow(v);
}

void vector_free(Vector *v) {
    if (v == NULL) {
        return;
    }

    free(v->data);
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
    v->element_size = 0;
}

void vector_clear(Vector *v) {
    if (v == NULL) {
        return;
    }

    v->size = 0;
}

int vector_push(Vector *v, const void *elem) {
    if (v == NULL || elem == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (v->size == v->capacity) {
        if (vector_grow(v) != 0) {
            return -1;
        }
    }

    memcpy((char *)v->data + v->size * v->element_size, elem, v->element_size);
    v->size++;
    return 0;
}

void *vector_get(Vector *v, size_t index) {
    if (v == NULL || index >= v->size) {
        return NULL;
    }

    return (char *)v->data + index * v->element_size;
}

const void *vector_get_const(const Vector *v, size_t index) {
    if (v == NULL || index >= v->size) {
        return NULL;
    }

    return (const char *)v->data + index * v->element_size;
}
