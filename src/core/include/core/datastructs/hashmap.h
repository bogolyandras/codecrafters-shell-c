#ifndef BOGOLYANDRAS_HASHMAP_H
#define BOGOLYANDRAS_HASHMAP_H

#include <stddef.h>

#include "core/pstring.h"

typedef struct HashMapEntry {
    PChar *key; /** Owned copy of the key */
    struct HashMapEntry *next;
    _Alignas(max_align_t) unsigned char value[]; /** Value bytes, `value_size` long */
} HashMapEntry;

/** Called with a pointer to the stored value slot when a value is removed, replaced or freed */
typedef void (*HashMapValueDestructor)(void *value);

typedef struct {
    HashMapEntry **buckets;
    size_t bucket_count;
    size_t size;
    size_t value_size; /** The size of each value in bytes */
    HashMapValueDestructor destroy_value; /** Optional, may be NULL */
} HashMap;


/**
 * Functions returning int return 0 on success and -1 on failure with errno set.
 * `destroy_value` may be NULL if values own no resources.
 */
int hashmap_init(HashMap *map, size_t bucket_count, size_t value_size, HashMapValueDestructor destroy_value);
void hashmap_free(HashMap *map);

/** Copies the key and the value bytes into the map. On failure the caller keeps ownership of the value */
int hashmap_put(HashMap *map, const PChar *key, const void *value);
/** Gets the pointer to the stored value, or NULL if the key is not present */
void *hashmap_get(HashMap *map, const PChar *key);
const void *hashmap_get_const(const HashMap *map, const PChar *key);
/** Fails with ENOENT if the key is not present */
int hashmap_remove(HashMap *map, const PChar *key);

/** Iteration in unspecified order. The map must not be modified while iterating */
typedef struct {
    const HashMap *map;
    size_t bucket; /** The next bucket to look at */
    const HashMapEntry *entry; /** The next entry in the current bucket, or NULL */
} HashMapIter;

void hashmap_iter_init(HashMapIter *iter, const HashMap *map);
/** Returns 1 and sets `key` and `value` if there is another entry, 0 at the end */
int hashmap_iter_next(HashMapIter *iter, const PChar **key, const void **value);

/**
 * Defines a type-safe hashmap type `Name` mapping PChar string keys to values of type `T`, together
 * with typed wrappers over the generic HashMap API:
 *   Name_init, Name_free, Name_put, Name_get, Name_get_const, Name_remove, Name_size,
 *   Name_iter_init, Name_iter_next
 * Each instantiation is a distinct struct type, so passing the wrong map or storing the wrong
 * value type is a compile-time diagnostic.
 */
#define HASHMAP_DEFINE(T, Name)                                                                \
    typedef struct {                                                                           \
        HashMap base;                                                                          \
    } Name;                                                                                    \
    static inline int Name##_init(Name *m, size_t bucket_count,                                \
                                  HashMapValueDestructor destroy_value) {                      \
        return hashmap_init(&m->base, bucket_count, sizeof(T), destroy_value);                 \
    }                                                                                          \
    static inline void Name##_free(Name *m) {                                                  \
        hashmap_free(&m->base);                                                                \
    }                                                                                          \
    static inline int Name##_put(Name *m, const PChar *key, T value) {                          \
        return hashmap_put(&m->base, key, &value);                                             \
    }                                                                                          \
    static inline T *Name##_get(Name *m, const PChar *key) {                                    \
        return (T *)hashmap_get(&m->base, key);                                                \
    }                                                                                          \
    static inline const T *Name##_get_const(const Name *m, const PChar *key) {                  \
        return (const T *)hashmap_get_const(&m->base, key);                                    \
    }                                                                                          \
    static inline int Name##_remove(Name *m, const PChar *key) {                                \
        return hashmap_remove(&m->base, key);                                                  \
    }                                                                                          \
    static inline size_t Name##_size(const Name *m) {                                          \
        return m->base.size;                                                                   \
    }                                                                                          \
    typedef struct {                                                                           \
        HashMapIter base;                                                                      \
    } Name##Iter;                                                                              \
    static inline void Name##_iter_init(Name##Iter *it, const Name *m) {                       \
        hashmap_iter_init(&it->base, &m->base);                                                \
    }                                                                                          \
    static inline int Name##_iter_next(Name##Iter *it, const PChar **key, const T **value) {    \
        const void *untyped_value;                                                             \
        if (!hashmap_iter_next(&it->base, key, &untyped_value)) {                              \
            return 0;                                                                          \
        }                                                                                      \
        *value = (const T *)untyped_value;                                                     \
        return 1;                                                                              \
    }

#endif //BOGOLYANDRAS_HASHMAP_H
