#include "../src/core/hashtable.h"
#include "test_util.h"

#include <string.h>

static unsigned long long rng_state = 88172645463325252ULL;
static unsigned long long rng(void) {          /* xorshift64, deterministic */
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static void test_basic(void) {
    HashTable t;
    CHECK(ht_init(&t, 8) == 0);
    int v = -1;
    CHECK(ht_get(&t, 5, &v) == 0);
    CHECK(ht_put(&t, 5, 50) == 0);
    CHECK(ht_get(&t, 5, &v) == 1 && v == 50);
    CHECK(ht_put(&t, 5, 51) == 0);              /* overwrite, not duplicate */
    CHECK(ht_get(&t, 5, &v) == 1 && v == 51);
    CHECK(t.count == 1);
    CHECK(ht_contains(&t, 5) && !ht_contains(&t, 6));
    CHECK(ht_put(&t, -3, 7) == 0 && ht_get(&t, -3, &v) == 1 && v == 7);
    CHECK(ht_get(&t, 5, NULL) == 1);            /* NULL value pointer allowed */
    ht_free(&t);
    CHECK(t.buckets == NULL && t.count == 0);
}

static void test_rehash_and_collisions(void) {
    HashTable t;
    CHECK(ht_init(&t, 2) == 0);                 /* tiny: forces many rehashes */
    CHECK(t.nbuckets == 2);
    const int N = 100000;
    for (int i = 0; i < N; i++) CHECK(ht_put(&t, (long long)i * 7919, i) == 0);
    CHECK(t.count == N);
    CHECK(t.nbuckets > 2);
    CHECK(4 * t.count <= 3 * t.nbuckets);       /* load factor never above 0.75 */
    int ok = 1, v;
    for (int i = 0; i < N; i++)
        if (!ht_get(&t, (long long)i * 7919, &v) || v != i) { ok = 0; break; }
    CHECK(ok);
    CHECK(!ht_contains(&t, 1));
    /* chains stay short: far below N, so lookups are not degenerating to O(n) */
    CHECK(ht_max_chain(&t) < 20);
    ht_free(&t);
}

/* Compare against a trivially correct reference: a sorted-free linear store. */
static void test_against_reference(void) {
    enum { M = 2000 };
    long long keys[M]; int vals[M]; int n = 0;
    HashTable t;
    CHECK(ht_init(&t, 4) == 0);
    for (int step = 0; step < 20000; step++) {
        long long k = (long long)(rng() % 1500) - 700;     /* many repeats, negatives */
        int v = (int)(rng() % 100000);
        int idx = -1;
        for (int i = 0; i < n; i++) if (keys[i] == k) { idx = i; break; }
        if (idx >= 0) vals[idx] = v; else { keys[n] = k; vals[n] = v; n++; }
        ht_put(&t, k, v);
    }
    CHECK(t.count == n);
    int ok = 1, got;
    for (int i = 0; i < n; i++)
        if (!ht_get(&t, keys[i], &got) || got != vals[i]) ok = 0;
    CHECK(ok);
    ht_free(&t);
}

/* Real OSM ids from data/osm/nodes.csv (not in git; skipped when absent). */
static void test_real_osm_ids(void) {
    FILE *f = fopen("data/osm/nodes.csv", "r");
    if (f == NULL) { printf("note: data/osm/nodes.csv not found, real-ID test skipped\n"); return; }
    char line[512];
    HashTable t;
    CHECK(ht_init(&t, 16) == 0);
    int rows = 0, ok = 1;
    if (fgets(line, sizeof line, f) == NULL) { fclose(f); return; }   /* header */
    long long *ids = (long long *)malloc(40000 * sizeof(long long));
    while (fgets(line, sizeof line, f) != NULL && rows < 40000) {
        long long id = strtoll(line, NULL, 10);
        ids[rows] = id;
        if (ht_put(&t, id, rows) != 0) ok = 0;
        rows++;
    }
    fclose(f);
    CHECK(ok);
    CHECK(rows == 32110);                       /* meta.txt: nodes = 32110 */
    CHECK(t.count == rows);                     /* ids are unique */
    int v, all = 1;
    for (int i = 0; i < rows; i++)
        if (!ht_get(&t, ids[i], &v) || v != i) { all = 0; break; }
    CHECK(all);
    CHECK(!ht_contains(&t, 1LL));               /* an id that is not in the file */
    printf("real OSM ids: %d keys, %d buckets, longest chain %d\n",
           t.count, t.nbuckets, ht_max_chain(&t));
    free(ids);
    ht_free(&t);
}

int main(void) {
    test_basic();
    test_rehash_and_collisions();
    test_against_reference();
    test_real_osm_ids();
    TEST_REPORT("test_hashtable");
}
