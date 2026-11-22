/*
 * search.h - searching by key: binary search on a sorted array, and linear search.
 *
 * Used for the lookup experiment: the traffic snapshot is stored twice, in the
 * hash table (O(1) average) and in an array sorted by edge id and searched with
 * binary search (O(log n)); src/app/lookup_benchmark.c times both.
 */
#ifndef ROUTELENS_SEARCH_H
#define ROUTELENS_SEARCH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    long long key;
    int value;
} KeyValue;

/* An array of (key, value) pairs sorted by key (ascending, stable). */
typedef struct {
    KeyValue *items;
    int count;
} SortedKeys;

/* Copies the n pairs and sorts them with a merge sort (stable, O(n log n)).
 * values may be NULL (then value = position in the input). Duplicate keys are
 * kept, in input order. Returns 0 on success, -1 on invalid input or out of
 * memory (s is then empty and safe to sk_free). */
int  sk_build(SortedKeys *s, const long long *keys, const int *values, int n);
void sk_free(SortedKeys *s);

/* Binary search. Returns 1 and sets *value if the key is present (value may be
 * NULL). With duplicate keys the first one in input order is returned. O(log n). */
int  sk_find(const SortedKeys *s, long long key, int *value);

/* Index of the first element >= key in the sorted array a[0..n-1] (n if none).
 * The building block of sk_find; O(log n). */
int  lower_bound_ll(const long long *a, int n, long long key);

/* Linear search in an unsorted array: index of the first match, or -1. O(n). */
int  linear_find_ll(const long long *a, int n, long long key);

#ifdef __cplusplus
}
#endif
#endif
