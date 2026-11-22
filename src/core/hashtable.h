/*
 * hashtable.h - hash table from long long keys to int values.
 *
 * Hash:       multiplicative (Fibonacci) hashing, so the 64-bit OSM ids spread
 *             over the buckets:  h = (key * 11400714819323198485) >> (64 - bits)
 * Collisions: separate chaining (each bucket is a singly linked list).
 * Resizing:   when count / nbuckets would exceed 0.75 the bucket count doubles
 *             and every entry is re-inserted (rehash).
 * nbuckets is always a power of two (2^bits) so the shift above is valid.
 *
 * Used for: OSM id -> node index (loader), edge id -> traffic (snapshot reader),
 * edge sets for the route-overlap check (value ignored).
 */
#ifndef ROUTELENS_HASHTABLE_H
#define ROUTELENS_HASHTABLE_H

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Node **buckets;
    int nbuckets;     /* 2^bits */
    int count;        /* number of stored keys */
    int bits;
} HashTable;

/* nbuckets is rounded up to a power of two (minimum 2). 0 ok, -1 out of memory. */
int  ht_init(HashTable *t, int nbuckets);
/* Inserts, or overwrites the value if the key exists. 0 ok, -1 out of memory. */
int  ht_put(HashTable *t, long long key, int value);
/* Returns 1 and sets *value if found (value may be NULL), else 0. */
int  ht_get(const HashTable *t, long long key, int *value);
int  ht_contains(const HashTable *t, long long key);
void ht_free(HashTable *t);

/* For tests and the report: length of the longest chain. */
int  ht_max_chain(const HashTable *t);

#ifdef __cplusplus
}
#endif
#endif
