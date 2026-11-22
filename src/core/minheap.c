#include "minheap.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>

#define HEAP_DEFAULT_CAP 64

int heap_init(MinHeap *h, int cap) {
    if (cap < 1) cap = HEAP_DEFAULT_CAP;
    h->size = 0;
    h->cap = 0;
    h->a = (HeapItem *)malloc((size_t)cap * sizeof(HeapItem));
    if (h->a == NULL) return -1;
    h->cap = cap;
    return 0;
}

static void swap_items(HeapItem *x, HeapItem *y) {
    HeapItem t = *x;
    *x = *y;
    *y = t;
}

int heap_push(MinHeap *h, double key, int node) {
    if (isnan(key)) return -1;                          /* NaN would break every comparison */
    if (h->size == h->cap) {
        if (h->cap > INT_MAX / 2) return -1;
        int new_cap = h->cap * 2;
        HeapItem *p = (HeapItem *)realloc(h->a, (size_t)new_cap * sizeof(HeapItem));
        if (p == NULL) return -1;                       /* old buffer still valid */
        h->a = p;
        h->cap = new_cap;
    }
    int i = h->size++;
    h->a[i].key = key;
    h->a[i].node = node;
    while (i > 0) {                                     /* sift up */
        int parent = (i - 1) / 2;
        if (h->a[parent].key <= h->a[i].key) break;
        swap_items(&h->a[parent], &h->a[i]);
        i = parent;
    }
    return 0;
}

HeapItem heap_pop(MinHeap *h) {
    assert(h->size > 0);
    HeapItem top = h->a[0];
    h->a[0] = h->a[--h->size];                          /* last item takes the root's place */
    int i = 0;
    for (;;) {                                          /* sift down */
        int left = 2 * i + 1, right = left + 1, smallest = i;
        if (left < h->size && h->a[left].key < h->a[smallest].key) smallest = left;
        if (right < h->size && h->a[right].key < h->a[smallest].key) smallest = right;
        if (smallest == i) break;
        swap_items(&h->a[i], &h->a[smallest]);
        i = smallest;
    }
    return top;
}

HeapItem heap_peek(const MinHeap *h) {
    assert(h->size > 0);
    return h->a[0];
}

int heap_empty(const MinHeap *h) { return h->size == 0; }
int heap_size(const MinHeap *h) { return h->size; }
void heap_clear(MinHeap *h) { h->size = 0; }

void heap_free(MinHeap *h) {
    free(h->a);
    h->a = NULL;
    h->size = h->cap = 0;
}
