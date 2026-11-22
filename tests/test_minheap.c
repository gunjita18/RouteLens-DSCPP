#include "../src/core/minheap.h"
#include "test_util.h"

#include <math.h>
#include <stdlib.h>

static unsigned long long rng_state = 0xA24BAED4963EE407ULL;
static unsigned long long rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void test_basic(void) {
    MinHeap h;
    CHECK(heap_init(&h, 0) == 0);                      /* default capacity */
    CHECK(heap_empty(&h) && heap_size(&h) == 0);

    CHECK(heap_push(&h, 30.0, 3) == 0);
    CHECK(heap_push(&h, 10.0, 1) == 0);
    CHECK(heap_push(&h, 20.0, 2) == 0);
    CHECK(heap_size(&h) == 3);
    CHECK(heap_peek(&h).node == 1 && heap_peek(&h).key == 10.0);
    CHECK(heap_size(&h) == 3);                         /* peek does not remove */

    HeapItem a = heap_pop(&h), b = heap_pop(&h), c = heap_pop(&h);
    CHECK(a.node == 1 && b.node == 2 && c.node == 3);
    CHECK(heap_empty(&h));

    CHECK(heap_push(&h, NAN, 9) == -1);                /* NaN rejected, heap unchanged */
    CHECK(heap_empty(&h));
    CHECK(heap_push(&h, -5.0, 4) == 0 && heap_push(&h, 0.0, 5) == 0);   /* negative keys are fine for a heap */
    CHECK(heap_pop(&h).node == 4);
    heap_clear(&h);
    CHECK(heap_empty(&h));
    heap_free(&h);
    CHECK(h.a == NULL && h.size == 0);
    heap_free(&h);                                     /* double free is harmless */
}

/* Pops come out sorted; same order as sorting the inputs (the plan's reference). */
static void test_sorted_pops(int n) {
    MinHeap h;
    double *keys = (double *)malloc((size_t)n * sizeof(double));
    CHECK(heap_init(&h, 2) == 0);                      /* tiny: forces many growths */
    for (int i = 0; i < n; i++) {
        keys[i] = (double)(rng() % 100000) / 7.0;      /* many duplicates and ties */
        CHECK(heap_push(&h, keys[i], i) == 0);
    }
    CHECK(heap_size(&h) == n);
    qsort(keys, (size_t)n, sizeof(double), cmp_double);
    int ok = 1;
    for (int i = 0; i < n; i++)
        if (heap_pop(&h).key != keys[i]) { ok = 0; break; }
    CHECK(ok && heap_empty(&h));
    heap_free(&h);
    free(keys);
}

/* Interleaved pushes and pops against a trivially correct model: a plain array. */
static void test_against_model(void) {
    enum { MAXN = 400 };
    MinHeap h;
    double model[MAXN];
    int nodes[MAXN], count = 0, ok = 1;
    CHECK(heap_init(&h, 4) == 0);
    for (int step = 0; step < 20000; step++) {
        if (count == 0 || (count < MAXN && rng() % 3 != 0)) {
            double k = (double)(rng() % 500);
            int node = (int)(rng() % 1000);
            heap_push(&h, k, node);
            model[count] = k;
            nodes[count++] = node;
        } else {
            int best = 0;
            for (int i = 1; i < count; i++) if (model[i] < model[best]) best = i;
            HeapItem it = heap_pop(&h);
            if (it.key != model[best]) ok = 0;
            int found = 0;                              /* the popped node must belong to an item with that key */
            for (int i = 0; i < count; i++)
                if (model[i] == it.key && nodes[i] == it.node) { model[i] = model[count - 1]; nodes[i] = nodes[count - 1]; count--; found = 1; break; }
            if (!found) ok = 0;
        }
        if (heap_size(&h) != count) ok = 0;
    }
    CHECK(ok);
    heap_free(&h);
}

/* The heap property itself: every parent <= its children. */
static void test_heap_property(void) {
    MinHeap h;
    CHECK(heap_init(&h, 8) == 0);
    for (int i = 0; i < 5000; i++) heap_push(&h, (double)(rng() % 1000), i);
    int ok = 1;
    for (int i = 1; i < h.size; i++)
        if (h.a[(i - 1) / 2].key > h.a[i].key) ok = 0;
    CHECK(ok);
    for (int i = 0; i < 2500; i++) heap_pop(&h);
    for (int i = 1; i < h.size; i++)
        if (h.a[(i - 1) / 2].key > h.a[i].key) ok = 0;
    CHECK(ok);
    heap_free(&h);
}

int main(void) {
    test_basic();
    test_sorted_pops(1);
    test_sorted_pops(2);
    test_sorted_pops(1000);
    test_sorted_pops(1000000);
    test_against_model();
    test_heap_property();
    TEST_REPORT("test_minheap");
}
