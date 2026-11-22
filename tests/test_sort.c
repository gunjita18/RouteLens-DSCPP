#include "../src/core/sort.h"
#include "test_util.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long rng_state = 0x5851F42D4C957F2DULL;
static unsigned long long rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static int cmp_int(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}
static int cmp_int_desc(const void *a, const void *b) { return cmp_int(b, a); }

typedef struct { double score; int seq; char pad[12]; } Route;      /* 24 bytes: a non-int element size */
static int cmp_route(const void *a, const void *b) {
    double x = ((const Route *)a)->score, y = ((const Route *)b)->score;
    return (x > y) - (x < y);
}

/* Same result as qsort (the plan's reference) for many sizes, including 0, 1 and powers of two +- 1. */
static void test_against_qsort(void) {
    const int sizes[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 100, 1023, 1024, 1025, 100000 };
    for (size_t t = 0; t < sizeof sizes / sizeof sizes[0]; t++) {
        int n = sizes[t];
        int *a = (int *)malloc((size_t)(n + 1) * sizeof(int));
        int *ref = (int *)malloc((size_t)(n + 1) * sizeof(int));
        for (int i = 0; i < n; i++) a[i] = ref[i] = (int)(rng() % 2000) - 1000;
        qsort(ref, (size_t)n, sizeof(int), cmp_int);
        CHECK(merge_sort(a, (size_t)n, sizeof(int), cmp_int) == 0);
        CHECK(n == 0 || memcmp(a, ref, (size_t)n * sizeof(int)) == 0);
        free(a);
        free(ref);
    }
}

static void test_orders(void) {
    int up[] = { 1, 2, 3, 4, 5 }, down[] = { 5, 4, 3, 2, 1 }, same[] = { 7, 7, 7, 7 };
    merge_sort(down, 5, sizeof(int), cmp_int);
    CHECK(memcmp(down, up, sizeof up) == 0);               /* reversed input */
    merge_sort(up, 5, sizeof(int), cmp_int);
    CHECK(up[0] == 1 && up[4] == 5);                       /* already sorted */
    merge_sort(same, 4, sizeof(int), cmp_int);
    CHECK(same[0] == 7 && same[3] == 7);
    int d[] = { 3, 1, 2 };
    merge_sort(d, 3, sizeof(int), cmp_int_desc);           /* descending comparator */
    CHECK(d[0] == 3 && d[1] == 2 && d[2] == 1);
}

/* Stable: routes with equal scores keep their original order (seq increases within a score). */
static void test_stability(void) {
    enum { N = 5000 };
    Route *r = (Route *)malloc(N * sizeof(Route));
    for (int i = 0; i < N; i++) { r[i].score = (double)(rng() % 7); r[i].seq = i; memset(r[i].pad, 0, sizeof r[i].pad); }
    CHECK(merge_sort(r, N, sizeof(Route), cmp_route) == 0);
    int ok = 1;
    for (int i = 1; i < N; i++) {
        if (r[i - 1].score > r[i].score) ok = 0;
        if (r[i - 1].score == r[i].score && r[i - 1].seq > r[i].seq) ok = 0;
    }
    CHECK(ok);
    free(r);
}

static void test_invalid_arguments(void) {
    int a[] = { 2, 1 };
    CHECK(merge_sort(NULL, 2, sizeof(int), cmp_int) == -1);
    CHECK(merge_sort(a, 2, sizeof(int), NULL) == -1);
    CHECK(merge_sort(a, 2, 0, cmp_int) == -1);
    CHECK(merge_sort(a, SIZE_MAX / 2, 4, cmp_int) == -1);   /* n * size would overflow: refused, array untouched */
    CHECK(a[0] == 2 && a[1] == 1);
    CHECK(merge_sort(a, 0, sizeof(int), cmp_int) == 0);
    CHECK(merge_sort(a, 1, sizeof(int), cmp_int) == 0);
}

int main(void) {
    test_against_qsort();
    test_orders();
    test_stability();
    test_invalid_arguments();
    TEST_REPORT("test_sort");
}
