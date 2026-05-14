#ifndef BOGOLYANDRAS_VECTOR_H
#define BOGOLYANDRAS_VECTOR_H

#include <stddef.h>

typedef struct {
    void *data; /** Pointer to the array */
    size_t size; /** The logical size of the vector */
    size_t capacity; /** The physical capacity of the vector */
    size_t element_size; /** The size of each element in bytes */
} Vector;


int vector_init(Vector *v, size_t element_size); /** Allocated memory for the vector */
void vector_free(Vector *v); /** Dispose of the vector and its allocated memory */
void vector_clear(Vector *v); /** Clear all elements from the vector without disposing capacity */

int vector_push(Vector *v, const void *elem); /** Adds a new element to the back */
void *vector_get(Vector *v, size_t index); /** Gets the pointer to a specific element */
const void *vector_get_const(const Vector *v, size_t index);


#endif //BOGOLYANDRAS_VECTOR_H
