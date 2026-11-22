#include "../src/core/hashtable.h"
#include "../src/core/search.h"
#include "test_util.h"

#include <string.h>

static unsigned long long rng_state = 0x2545F4914F6CDD1DULL;
static unsigned long long rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static int cmp_ll(const void *a, const void *b) {
    long long x = *(const long long *)a, y = *(const long long *)b;
    return (x > y) - (x < y);
}

static void test_lower_bound_and_linear(void) {
    long long a[] = { 2, 4, 4, 4, 9 };
    CHECK(lower_bound_ll(a, 5, 1) == 0);
    CHECK(lower_bound_ll(a, 5, 2) == 0);
    CHECK(lower_bound_ll(a, 5, 3) == 1);
    CHECK(lower_bound_ll(a, 5, 4) == 1);          /* first of the duplicates */
    CHECK(lower_bound_ll(a, 5, 5) == 4);
    CHECK(lower_bound_ll(a, 5, 9) == 4);
    CHECK(lower_bound_ll(a, 5, 10) == 5);         /* larger than everything */
    CHECK(lower_bound_ll(a, 0, 7) == 0);          /* empty array */

    CHECK(linear_find_ll(a, 5, 9) == 4);
    CHECK(linear_find_ll(a, 5, 4) == 1);          /* first match */
    CHECK(linear_find_ll(a, 5, 3) == -1);
    CHECK(linear_find_ll(a, 0, 3) == -1);
}

static void test_sorted_keys_small(void) {
    SortedKeys s;
    CHECK(sk_build(&s, NULL, NULL, 0) == 0 && s.count == 0);
    CHECK(sk_find(&s, 5, NULL) == 0);             /* empty: nothing found, no crash */
    sk_free(&s);

    CHECK(sk_build(&s, NULL, NULL, -1) == -1);
    CHECK(sk_build(&s, NULL, NULL, 3) == -1);     /* missing keys */

    long long one[] = { 42 };
    int v = -1;
    CHECK(sk_build(&s, one, NULL, 1) == 0);
    CHECK(sk_find(&s, 42, &v) == 1 && v == 0);
    CHECK(sk_find(&s, 41, &v) == 0 && sk_find(&s, 43, &v) == 0);
    sk_free(&s);

    /* unsorted input, explicit values, negative and huge keys */
    long long keys[] = { 50, -7, 3000000000LL, 12, 0 };
    int vals[] = { 5, 70, 99, 12, 1 };
    CHECK(sk_build(&s, keys, vals, 5) == 0);
    for (int i = 1; i < s.count; i++) CHECK(s.items[i - 1].key <= s.items[i].key);
    for (int i = 0; i < 5; i++) CHECK(sk_find(&s, keys[i], &v) == 1 && v == vals[i]);
    CHECK(sk_find(&s, 13, &v) == 0);
    sk_free(&s);
    sk_free(&s);                                  /* double free is harmless */
}

/* Stability: equal keys keep their input order (value = input position). */
static void test_stability(void) {
    enum { N = 1000 };
    long long keys[N];
    for (int i = 0; i < N; i++) keys[i] = (long long)(rng() % 10);   /* only 10 distinct keys */
    SortedKeys s;
    CHECK(sk_build(&s, keys, NULL, N) == 0);
    int ok = 1;
    for (int i = 1; i < N; i++) {
        if (s.items[i - 1].key > s.items[i].key) ok = 0;
        if (s.items[i - 1].key == s.items[i].key && s.items[i - 1].value > s.items[i].value) ok = 0;
    }
    CHECK(ok);
    int v;
    for (long long k = 0; k < 10; k++) {                             /* first duplicate wins */
        CHECK(sk_find(&s, k, &v) == 1 && keys[v] == k && linear_find_ll(keys, N, k) == v);
    }
    sk_free(&s);
}

/* Reference: qsort. Same key sequence for many sizes, including 0, 1, 2 and powers of two +- 1. */
static void test_against_qsort(void) {
    const int sizes[] = { 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 100, 1023, 1024, 1025, 100000 };
    for (size_t t = 0; t < sizeof sizes / sizeof sizes[0]; t++) {
        int n = sizes[t];
        long long *keys = (long long *)malloc((size_t)n * sizeof(long long));
        long long *ref = (long long *)malloc((size_t)n * sizeof(long long));
        long long *orig = (long long *)malloc((size_t)n * sizeof(long long));
        for (int i = 0; i < n; i++) keys[i] = ref[i] = orig[i] = (long long)(rng() % 5000) - 2500;
        qsort(ref, (size_t)n, sizeof(long long), cmp_ll);
        SortedKeys s;
        CHECK(sk_build(&s, keys, NULL, n) == 0 && s.count == n);
        int same = 1;
        for (int i = 0; i < n; i++) if (s.items[i].key != ref[i]) { same = 0; break; }
        CHECK(same);
        CHECK(memcmp(keys, orig, (size_t)n * sizeof(long long)) == 0);   /* input not modified */
        sk_free(&s);
        free(keys);
        free(ref);
        free(orig);
    }
}

/* Binary search and the hash table must give the same answer for every key. */
static void test_agrees_with_hash_table(void) {
    enum { N = 50000 };
    long long *keys = (long long *)malloc(N * sizeof(long long));
    int *vals = (int *)malloc(N * sizeof(int));
    HashTable t;
    CHECK(ht_init(&t, 16) == 0);
    for (int i = 0; i < N; i++) {
        keys[i] = ((long long)i * 7919 + 13) * 31;      /* distinct, scattered, not sorted by position */
        vals[i] = i * 2;
        ht_put(&t, keys[i], vals[i]);
    }
    /* shuffle so the input is not already ordered */
    for (int i = N - 1; i > 0; i--) {
        int j = (int)(rng() % (unsigned long long)(i + 1));
        long long tk = keys[i]; keys[i] = keys[j]; keys[j] = tk;
        int tv = vals[i]; vals[i] = vals[j]; vals[j] = tv;
    }
    SortedKeys s;
    CHECK(sk_build(&s, keys, vals, N) == 0);

    int agree = 1, hv, sv;
    for (int i = 0; i < N; i++) {                        /* all present keys */
        int fh = ht_get(&t, keys[i], &hv), fs = sk_find(&s, keys[i], &sv);
        if (!fh || !fs || hv != sv || hv != vals[i]) { agree = 0; break; }
    }
    CHECK(agree);
    agree = 1;
    for (int i = 0; i < 100000; i++) {                   /* mostly absent keys */
        long long k = (long long)(rng() % 20000000ULL);
        int fh = ht_get(&t, k, &hv), fs = sk_find(&s, k, &sv);
        if (fh != fs || (fh && hv != sv)) { agree = 0; break; }
    }
    CHECK(agree);
    sk_free(&s);
    ht_free(&t);
    free(keys);
    free(vals);
}

int main(void) {
    test_lower_bound_and_linear();
    test_sorted_keys_small();
    test_stability();
    test_against_qsort();
    test_agrees_with_hash_table();
    TEST_REPORT("test_search");
}
