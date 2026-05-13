#include "vector.h"

#include <stdlib.h>
#include <string.h>



// static int vector_grow(Vector *v, size_t min_capacity) {
//     size_t new_capacity = v->capacity == 0 ? 4 : v->capacity;
//
//     while (new_capacity < min_capacity) {
//         new_capacity *= 2;
//     }
//
//     void *new_data = realloc(v->data, new_capacity * v->elem_size);
//     if (new_data == NULL) {
//         return 0;
//     }
//
//     v->data = new_data;
//     v->capacity = new_capacity;
//     return 1;
// }
//
// int vector_init(Vector *v, size_t elem_size) {
//     if (v == NULL || elem_size == 0) {
//         return 0;
//     }
//
//     v->data = NULL;
//     v->size = 0;
//     v->capacity = 0;
//     v->elem_size = elem_size;
//     return 1;
// }
//
// void vector_free(Vector *v) {
//     if (v == NULL) {
//         return;
//     }
//
//     free(v->data);
//     v->data = NULL;
//     v->size = 0;
//     v->capacity = 0;
//     v->elem_size = 0;
// }
//
// int vector_push(Vector *v, const void *elem) {
//     if (v == NULL || elem == NULL) {
//         return 0;
//     }
//
//     if (v->size == v->capacity) {
//         if (!vector_grow(v, v->size + 1)) {
//             return 0;
//         }
//     }
//
//     memcpy((char *)v->data + v->size * v->elem_size, elem, v->elem_size);
//     v->size++;
//     return 1;
// }
//
// void *vector_get(Vector *v, size_t index) {
//     if (v == NULL || index >= v->size) {
//         return NULL;
//     }
//
//     return (char *)v->data + index * v->elem_size;
// }
//
// const void *vector_get_const(const Vector *v, size_t index) {
//     if (v == NULL || index >= v->size) {
//         return NULL;
//     }
//
//     return (const char *)v->data + index * v->elem_size;
// }
//
// int vector_set(Vector *v, size_t index, const void *elem) {
//     if (v == NULL || elem == NULL || index >= v->size) {
//         return 0;
//     }
//
//     memcpy((char *)v->data + index * v->elem_size, elem, v->elem_size);
//     return 1;
// }
//
// size_t vector_size(const Vector *v) {
//     return v == NULL ? 0 : v->size;
// }