/*
 * minheap.h - binary min-heap of (key, node) pairs.
 *
 * Stored in an array: the children of index i are 2i+1 and 2i+2, its parent is
 * (i-1)/2, and every parent's key is <= its children's keys, so the smallest key
 * is always at index 0. push sifts the new item up, pop moves the last item to the
 * root and sifts it down: both O(log n); peek is O(1).
 *
 * Used as Dijkstra's priority queue, with lazy deletion: instead of lowering the
 * key of a node already in the heap, the node is pushed again with the smaller key
 * and the outdated entry is skipped when it is popped.
 */
#ifndef ROUTELENS_MINHEAP_H
#define ROUTELENS_MINHEAP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double key;
    int node;
} HeapItem;

typedef struct {
    HeapItem *a;
    int size;
    int cap;
} MinHeap;

int      heap_init(MinHeap *h, int cap);                 /* cap < 1 uses a default. 0 ok, -1 out of memory */
int      heap_push(MinHeap *h, double key, int node);    /* 0 ok, -1 on a NaN key or out of memory */
HeapItem heap_pop(MinHeap *h);                           /* smallest key; the heap must not be empty */
HeapItem heap_peek(const MinHeap *h);                    /* smallest key without removing it; not empty */
int      heap_empty(const MinHeap *h);
int      heap_size(const MinHeap *h);
void     heap_clear(MinHeap *h);                         /* size = 0, keeps the buffer */
void     heap_free(MinHeap *h);

#ifdef __cplusplus
}
#endif
#endif
