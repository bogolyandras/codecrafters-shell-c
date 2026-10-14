#ifndef BOGOLYANDRAS_VECTOR_H
#define BOGOLYANDRAS_VECTOR_H

#include <stddef.h>

typedef struct {
    void *data; /** Pointer to the array */
    size_t size; /** The logical size of the vector */
    size_t capacity; /** The physical capacity of the vector */
    size_t element_size; /** The size of each element in bytes */
} Vector;


/**
 * Functions returning int return 0 on success and -1 on failure with errno set.
 * A vector whose init failed is still a valid empty vector and safe to free.
 */
int vector_init(Vector *v, size_t element_size); /** Allocated memory for the vector */
void vector_free(Vector *v); /** Dispose of the vector and its allocated memory */
void vector_clear(Vector *v); /** Clear all elements from the vector without disposing capacity */

int vector_push(Vector *v, const void *elem); /** Adds a new element to the back */
void *vector_get(Vector *v, size_t index); /** Gets the pointer to a specific element, NULL if out of range */
const void *vector_get_const(const Vector *v, size_t index);

/**
 * Defines a type-safe vector type `Name` holding elements of type `T`, together with
 * typed wrappers over the generic Vector API:
 *   Name_init, Name_free, Name_clear, Name_push, Name_get, Name_get_const, Name_size
 * Each instantiation is a distinct struct type, so passing the wrong vector or pushing
 * the wrong element type is a compile-time diagnostic.
 */
#define VECTOR_DEFINE(T, Name)                                                        \
    typedef struct {                                                                  \
        Vector base;                                                                  \
    } Name;                                                                           \
    static inline int Name##_init(Name *v) {                                          \
        return vector_init(&v->base, sizeof(T));                                      \
    }                                                                                 \
    static inline void Name##_free(Name *v) {                                         \
        vector_free(&v->base);                                                        \
    }                                                                                 \
    static inline void Name##_clear(Name *v) {                                        \
        vector_clear(&v->base);                                                       \
    }                                                                                 \
    static inline int Name##_push(Name *v, T elem) {                                  \
        return vector_push(&v->base, &elem);                                          \
    }                                                                                 \
    static inline T *Name##_get(Name *v, size_t index) {                              \
        return (T *)vector_get(&v->base, index);                                      \
    }                                                                                 \
    static inline const T *Name##_get_const(const Name *v, size_t index) {            \
        return (const T *)vector_get_const(&v->base, index);                          \
    }                                                                                 \
    static inline size_t Name##_size(const Name *v) {                                 \
        return v->base.size;                                                          \
    }


#endif //BOGOLYANDRAS_VECTOR_H
