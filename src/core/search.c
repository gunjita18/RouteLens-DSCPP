#include "search.h"

#include <stdlib.h>

#include "sort.h"

/* Orders pairs by key. merge_sort is stable, so equal keys keep their input order. */
static int compare_by_key(const void *a, const void *b) {
    long long x = ((const KeyValue *)a)->key, y = ((const KeyValue *)b)->key;
    return (x > y) - (x < y);
}

int sk_build(SortedKeys *s, const long long *keys, const int *values, int n) {
    s->items = NULL;
    s->count = 0;
    if (n < 0 || (n > 0 && keys == NULL)) return -1;
    if (n == 0) return 0;
    KeyValue *items = (KeyValue *)malloc((size_t)n * sizeof(KeyValue));
    if (items == NULL) return -1;
    for (int i = 0; i < n; i++) {
        items[i].key = keys[i];
        items[i].value = values ? values[i] : i;
    }
    if (merge_sort(items, (size_t)n, sizeof(KeyValue), compare_by_key) != 0) {
        free(items);
        return -1;
    }
    s->items = items;
    s->count = n;
    return 0;
}

void sk_free(SortedKeys *s) {
    free(s->items);
    s->items = NULL;
    s->count = 0;
}

int sk_find(const SortedKeys *s, long long key, int *value) {
    /* lower bound on the key field */
    int lo = 0, hi = s->count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (s->items[mid].key < key) lo = mid + 1; else hi = mid;
    }
    if (lo == s->count || s->items[lo].key != key) return 0;
    if (value != NULL) *value = s->items[lo].value;
    return 1;
}

int lower_bound_ll(const long long *a, int n, long long key) {
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;      /* not (lo+hi)/2: cannot overflow */
        if (a[mid] < key) lo = mid + 1; else hi = mid;
    }
    return lo;
}

int linear_find_ll(const long long *a, int n, long long key) {
    for (int i = 0; i < n; i++)
        if (a[i] == key) return i;
    return -1;
}
