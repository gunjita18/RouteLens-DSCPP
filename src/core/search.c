#include "search.h"

#include <stdlib.h>
#include <string.h>

/* ---- merge sort of KeyValue by key -----------------------------------------
 * Bottom-up: merge runs of width 1, 2, 4, ... between two buffers. Always
 * O(n log n). Stable because on equal keys the element of the LEFT run is taken
 * first. No recursion, so no stack depth concern. */
static void merge_sort(KeyValue *a, KeyValue *tmp, int n) {
    KeyValue *src = a, *dst = tmp;
    for (int width = 1; width < n; width *= 2) {
        for (int lo = 0; lo < n; lo += 2 * width) {
            int mid = lo + width < n ? lo + width : n;
            int hi = lo + 2 * width < n ? lo + 2 * width : n;
            int i = lo, j = mid, k = lo;
            while (i < mid && j < hi)
                dst[k++] = (src[j].key < src[i].key) ? src[j++] : src[i++];   /* left wins ties */
            while (i < mid) dst[k++] = src[i++];
            while (j < hi) dst[k++] = src[j++];
        }
        KeyValue *swap = src; src = dst; dst = swap;       /* result is now in src */
    }
    if (src != a) memcpy(a, src, (size_t)n * sizeof(KeyValue));
}

int sk_build(SortedKeys *s, const long long *keys, const int *values, int n) {
    s->items = NULL;
    s->count = 0;
    if (n < 0 || (n > 0 && keys == NULL)) return -1;
    if (n == 0) return 0;
    KeyValue *items = (KeyValue *)malloc((size_t)n * sizeof(KeyValue));
    KeyValue *tmp = (KeyValue *)malloc((size_t)n * sizeof(KeyValue));
    if (items == NULL || tmp == NULL) { free(items); free(tmp); return -1; }
    for (int i = 0; i < n; i++) {
        items[i].key = keys[i];
        items[i].value = values ? values[i] : i;
    }
    merge_sort(items, tmp, n);
    free(tmp);
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
