/*
 * lookup_benchmark.c - hash table vs binary search for looking up traffic by edge id.
 *
 * The same data is stored twice: in the hash table (O(1) average) and in an
 * array sorted by key and searched with binary search (O(log n)). Both answer the
 * same lookups; the program first checks that every answer is identical, then
 * times them.
 *
 * Method
 *   - datasets: the real snapshot (if it can be read) and synthetic key sets of
 *     growing size, because the real snapshot is small;
 *   - two query sets of L lookups each (default 1,000,000): "hits" (existing
 *     keys, random order) and "misses" (random keys inside the key range that
 *     are not present);
 *   - each timing is repeated R times (default 5) and the MEDIAN is reported;
 *   - both structures answer the same queries, and the result is accumulated so
 *     the compiler cannot remove the loop.
 *
 * usage: routelens_lookup_benchmark [--snapshot PATH] [--lookups L] [--runs R]
 * Build with optimisation (CMake adds -O2 for this target); -O0 timings mean little.
 */
#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../core/hashtable.h"
#include "../core/search.h"
#include "../core/traffic_reader.h"

static volatile long long g_sink;      /* keeps the lookup loops alive */

static unsigned long long rng_state = 0x9E3779B97F4A7C15ULL;
static unsigned long long rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static long long run_hash(const HashTable *t, const long long *q, int n) {
    long long acc = 0;
    int v;
    for (int i = 0; i < n; i++)
        if (ht_get(t, q[i], &v)) acc += v + 1;
    return acc;
}

static long long run_binary(const SortedKeys *s, const long long *q, int n) {
    long long acc = 0;
    int v;
    for (int i = 0; i < n; i++)
        if (sk_find(s, q[i], &v)) acc += v + 1;
    return acc;
}

static double median(double *a, int n) {
    for (int i = 1; i < n; i++) {                   /* insertion sort: n is tiny */
        double x = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > x) { a[j + 1] = a[j]; j--; }
        a[j + 1] = x;
    }
    return a[n / 2];
}

typedef struct {
    double hash_hit_ns, bin_hit_ns, hash_miss_ns, bin_miss_ns;
    double hash_build_ms, bin_build_ms;
    int max_chain;
} Result;

/* Returns 0 on success, -1 if the two structures ever disagree or memory runs out. */
static int bench(const char *label, const long long *keys, int n, int lookups, int runs, Result *out) {
    HashTable ht;
    SortedKeys sk;
    int status = -1;
    long long *hits = (long long *)malloc((size_t)lookups * sizeof(long long));
    long long *miss = (long long *)malloc((size_t)lookups * sizeof(long long));
    double *samples = (double *)malloc((size_t)runs * sizeof(double));
    int *vals = (int *)malloc((size_t)n * sizeof(int));
    ht.buckets = NULL;
    sk.items = NULL;
    sk.count = 0;
    if (!hits || !miss || !samples || !vals) goto done;

    long long max_key = 0;
    for (int i = 0; i < n; i++) { vals[i] = i; if (keys[i] > max_key) max_key = keys[i]; }

    double t0 = now_ms();
    if (ht_init(&ht, 1024) != 0) goto done;
    for (int i = 0; i < n; i++)
        if (ht_put(&ht, keys[i], i) != 0) goto done;
    out->hash_build_ms = now_ms() - t0;
    out->max_chain = ht_max_chain(&ht);

    t0 = now_ms();
    if (sk_build(&sk, keys, vals, n) != 0) goto done;
    out->bin_build_ms = now_ms() - t0;

    /* Misses are random absent keys INSIDE the key range, so binary search takes a
     * different path every time (keys beyond the maximum would all end at the
     * same array end and flatter binary search). If every value in the range is
     * a key, there is no gap, so use keys just beyond the maximum instead. */
    int gap_exists = max_key + 1 > (long long)n;
    for (int i = 0; i < lookups; i++) {
        hits[i] = keys[rng() % (unsigned long long)n];
        long long k;
        if (gap_exists) {
            do k = (long long)(rng() % (unsigned long long)(max_key + 1));
            while (ht_contains(&ht, k));
        } else {
            k = max_key + 1 + (long long)(rng() % 1000ULL);
        }
        miss[i] = k;
    }

    /* correctness first: every single answer must be identical */
    for (int pass = 0; pass < 2; pass++) {
        const long long *q = pass == 0 ? hits : miss;
        for (int i = 0; i < lookups; i++) {
            int hv = -1, bv = -1;
            int fh = ht_get(&ht, q[i], &hv), fb = sk_find(&sk, q[i], &bv);
            if (fh != fb || (fh && hv != bv) || fh != (pass == 0)) {
                fprintf(stderr, "MISMATCH in %s: key %lld hash=%d/%d binary=%d/%d\n",
                        label, q[i], fh, hv, fb, bv);
                goto done;
            }
        }
    }

    /* timing: median of `runs` repetitions; warm-up run first */
    g_sink += run_hash(&ht, hits, lookups) + run_binary(&sk, hits, lookups);
    for (int pass = 0; pass < 4; pass++) {
        const long long *q = pass < 2 ? hits : miss;
        int use_hash = (pass % 2) == 0;
        for (int r = 0; r < runs; r++) {
            double s = now_ms();
            g_sink += use_hash ? run_hash(&ht, q, lookups) : run_binary(&sk, q, lookups);
            samples[r] = now_ms() - s;
        }
        double ns = median(samples, runs) * 1e6 / (double)lookups;
        if (pass == 0) out->hash_hit_ns = ns;
        else if (pass == 1) out->bin_hit_ns = ns;
        else if (pass == 2) out->hash_miss_ns = ns;
        else out->bin_miss_ns = ns;
    }
    status = 0;

done:
    ht_free(&ht);
    sk_free(&sk);
    free(hits); free(miss); free(samples); free(vals);
    return status;
}

static void print_row(const char *label, int n, const Result *r) {
    printf("%-22s %9d | %7.1f %7.1f %5.1fx | %7.1f %7.1f %5.1fx | %7.2f %7.2f | %3d\n",
           label, n, r->hash_hit_ns, r->bin_hit_ns, r->bin_hit_ns / r->hash_hit_ns,
           r->hash_miss_ns, r->bin_miss_ns, r->bin_miss_ns / r->hash_miss_ns,
           r->hash_build_ms, r->bin_build_ms, r->max_chain);
}

int main(int argc, char **argv) {
    const char *snapshot = "data/traffic/traffic.csv";
    int lookups = 1000000, runs = 5;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc) snapshot = argv[++i];
        else if (strcmp(argv[i], "--lookups") == 0 && i + 1 < argc) lookups = atoi(argv[++i]);
        else if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc) runs = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: %s [--snapshot PATH] [--lookups L] [--runs R]\n", argv[0]);
            return 2;
        }
    }
    if (lookups < 1 || runs < 1) { fprintf(stderr, "lookups and runs must be at least 1\n"); return 2; }

    printf("Lookup benchmark: %d lookups per measurement, median of %d runs.\n", lookups, runs);
    printf("ns = nanoseconds per lookup (lower is better); ratio = binary / hash.\n\n");
    printf("%-22s %9s | %-23s | %-23s | %-15s | %s\n", "dataset", "n",
           "hits: hash bin ratio", "misses: hash bin ratio", "build ms h/b", "chain");

    int failed = 0;
    Result r;

    TrafficSnapshot snap;
    if (ts_load(&snap, snapshot) == 0 && snap.count > 0) {
        long long *keys = (long long *)malloc((size_t)snap.count * sizeof(long long));
        if (keys) {
            for (int i = 0; i < snap.count; i++) keys[i] = snap.records[i].edge_id;
            memset(&r, 0, sizeof r);
            if (bench("real snapshot", keys, snap.count, lookups, runs, &r) == 0) print_row("real snapshot", snap.count, &r);
            else failed = 1;
            free(keys);
        }
    } else {
        printf("(snapshot '%s' not found or empty: synthetic datasets only)\n", snapshot);
    }
    ts_free(&snap);

    /* synthetic: distinct, scattered keys. key = (i * odd) mod 2^40 is a bijection, so no repeats. */
    const int sizes[] = { 2000, 32000, 1000000 };
    for (int s = 0; s < 3; s++) {
        int n = sizes[s];
        long long *keys = (long long *)malloc((size_t)n * sizeof(long long));
        if (!keys) { failed = 1; break; }
        for (int i = 0; i < n; i++) keys[i] = (long long)(((unsigned long long)i * 2654435761ULL) & ((1ULL << 40) - 1));
        memset(&r, 0, sizeof r);
        char label[32];
        snprintf(label, sizeof label, "synthetic");
        if (bench(label, keys, n, lookups, runs, &r) == 0) print_row(label, n, &r);
        else failed = 1;
        free(keys);
    }

    printf("\n%s\n", failed ? "FAILED: the structures disagreed or memory ran out."
                           : "All lookups gave identical answers in both structures.");
    return failed ? 1 : 0;
}
