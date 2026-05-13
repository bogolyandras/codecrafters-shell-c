#ifndef BOGOLYANDRAS_VECTOR_H
#define BOGOLYANDRAS_VECTOR_H

#include <stddef.h>

typedef struct {
    void *data;
    size_t size;
    size_t capacity;
    size_t elem_size;
} Vector;

//
// int vector_init(Vector *v, size_t elem_size);
// void vector_free(Vector *v);
//
// int vector_push(Vector *v, const void *elem);
// void *vector_get(Vector *v, size_t index);
// const void *vector_get_const(const Vector *v, size_t index);
//
// int vector_set(Vector *v, size_t index, const void *elem);
//
// size_t vector_size(const Vector *v);

#endif //BOGOLYANDRAS_VECTOR_H
