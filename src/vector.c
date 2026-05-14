#include "vector.h"

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
        new_capacity = SIZE_MAX; //overflow would happen
    } else if (v->capacity == SIZE_MAX) {
        return 0; // Cannot grow further
    } else {
        new_capacity = v->capacity * GROWTH_FACTOR;
    }

    void *new_data = realloc(v->data, new_capacity * v->element_size);
    if (new_data == NULL) {
        return 0; //Failed to allocate memory
    }

    v->data = new_data;
    v->capacity = new_capacity;
    return 1;
}

int vector_init(Vector *v, size_t element_size) {
    if (v == NULL || element_size == 0) {
        return 0;
    }

    v->size = 0;
    v->capacity = START_CAPACITY;
    v->element_size = element_size;
    v->data = malloc(v->capacity * v->element_size);
    if (v->data == NULL) {
        return 0;
    }
    return 1;
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
        return 0;
    }

    if (v->size == v->capacity) {
        if (!vector_grow(v)) {
            return 0;
        }
    }

    memcpy((char *)v->data + v->size * v->element_size, elem, v->element_size);
    v->size++;
    return 1;
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
