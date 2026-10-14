#include "core/datastructs/hashmap.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static unsigned long hash_string(const PChar *str) {
  unsigned long hash = 5381;

  while (*str != '\0') {
    hash = ((hash << 5) + hash) + p_unit(*str++);
  }

  return hash;
}

static void destroy_entry(const HashMap *map, HashMapEntry *entry) {
  if (map->destroy_value != NULL) {
    map->destroy_value(entry->value);
  }

  free(entry->key);
  free(entry);
}

static HashMapEntry *find_entry(const HashMap *map, const PChar *key, size_t bucket_index) {
  HashMapEntry *entry = map->buckets[bucket_index];

  while (entry != NULL) {
    if (p_strcmp(entry->key, key) == 0) {
      return entry;
    }

    entry = entry->next;
  }

  return NULL;
}

int hashmap_init(HashMap *map, size_t bucket_count, size_t value_size, HashMapValueDestructor destroy_value) {
  if (map == NULL || bucket_count == 0) {
    errno = EINVAL;
    return -1;
  }

  /* Start as a valid empty map, so it stays usable (and freeable) if the allocation fails */
  map->buckets = NULL;
  map->bucket_count = 0;
  map->size = 0;
  map->value_size = value_size;
  map->destroy_value = destroy_value;

  map->buckets = calloc(bucket_count, sizeof(HashMapEntry *));

  if (map->buckets == NULL) {
    return -1;
  }

  map->bucket_count = bucket_count;

  return 0;
}

void hashmap_free(HashMap *map) {
  if (map == NULL) {
    return;
  }

  for (size_t i = 0; i < map->bucket_count; i++) {
    HashMapEntry *entry = map->buckets[i];

    while (entry != NULL) {
      HashMapEntry *next = entry->next;

      destroy_entry(map, entry);

      entry = next;
    }
  }

  free(map->buckets);

  map->buckets = NULL;
  map->bucket_count = 0;
  map->size = 0;
}

int hashmap_put(HashMap *map, const PChar *key, const void *value) {
  if (map == NULL || key == NULL || value == NULL || map->bucket_count == 0) {
    errno = EINVAL;
    return -1;
  }

  const size_t bucket_index = hash_string(key) % map->bucket_count;

  /* Replace the value if the key already exists */
  HashMapEntry *existing = find_entry(map, key, bucket_index);
  if (existing != NULL) {
    if (map->destroy_value != NULL) {
      map->destroy_value(existing->value);
    }
    memcpy(existing->value, value, map->value_size);

    return 0;
  }

  /* No key match scenario */
  HashMapEntry *new_entry = malloc(sizeof(HashMapEntry) + map->value_size);

  if (new_entry == NULL) {
    return -1;
  }

  new_entry->key = p_strdup(key);

  if (new_entry->key == NULL) {
    free(new_entry);
    return -1;
  }

  memcpy(new_entry->value, value, map->value_size);

  new_entry->next = map->buckets[bucket_index];
  map->buckets[bucket_index] = new_entry;
  map->size++;

  return 0;
}

void *hashmap_get(HashMap *map, const PChar *key) {
  if (map == NULL || key == NULL || map->bucket_count == 0) {
    return NULL;
  }

  const size_t bucket_index = hash_string(key) % map->bucket_count;
  HashMapEntry *entry = find_entry(map, key, bucket_index);

  return entry != NULL ? entry->value : NULL;
}

const void *hashmap_get_const(const HashMap *map, const PChar *key) {
  if (map == NULL || key == NULL || map->bucket_count == 0) {
    return NULL;
  }

  const size_t bucket_index = hash_string(key) % map->bucket_count;
  const HashMapEntry *entry = find_entry(map, key, bucket_index);

  return entry != NULL ? entry->value : NULL;
}

int hashmap_remove(HashMap *map, const PChar *key) {
  if (map == NULL || key == NULL || map->bucket_count == 0) {
    errno = EINVAL;
    return -1;
  }

  const size_t index = hash_string(key) % map->bucket_count;

  HashMapEntry *entry = map->buckets[index];
  HashMapEntry *prev = NULL;

  while (entry != NULL) {
    if (p_strcmp(entry->key, key) == 0) {
      if (prev == NULL) {
        map->buckets[index] = entry->next;
      } else {
        prev->next = entry->next;
      }

      destroy_entry(map, entry);
      map->size--;

      return 0;
    }

    prev = entry;
    entry = entry->next;
  }

  errno = ENOENT;
  return -1;
}

void hashmap_iter_init(HashMapIter *iter, const HashMap *map) {
  iter->map = map;
  iter->bucket = 0;
  iter->entry = NULL;
}

int hashmap_iter_next(HashMapIter *iter, const PChar **key, const void **value) {
  if (iter->map == NULL) {
    return 0;
  }

  while (iter->entry == NULL) {
    if (iter->bucket >= iter->map->bucket_count) {
      return 0;
    }

    iter->entry = iter->map->buckets[iter->bucket++];
  }

  *key = iter->entry->key;
  *value = iter->entry->value;
  iter->entry = iter->entry->next;

  return 1;
}
