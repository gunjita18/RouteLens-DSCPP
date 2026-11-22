#include "../src/core/dsu.h"
#include "test_util.h"

#include <string.h>

static unsigned long long rng_state = 0xD1B54A32D192ED03ULL;
static unsigned rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (unsigned)(rng_state >> 11);
}

static void test_basic(void) {
    Dsu d;
    CHECK(dsu_init(&d, -1) == -1);
    CHECK(dsu_init(&d, 0) == 0 && dsu_sets(&d) == 0);
    dsu_free(&d);

    CHECK(dsu_init(&d, 6) == 0);
    CHECK(dsu_sets(&d) == 6);
    CHECK(!dsu_connected(&d, 0, 1));
    CHECK(dsu_connected(&d, 3, 3));
    CHECK(dsu_union(&d, 0, 1) == 1 && dsu_sets(&d) == 5);
    CHECK(dsu_union(&d, 1, 0) == 0 && dsu_sets(&d) == 5);       /* already together */
    CHECK(dsu_union(&d, 2, 3) == 1 && dsu_union(&d, 1, 3) == 1);
    CHECK(dsu_sets(&d) == 3);                                   /* {0,1,2,3} {4} {5} */
    CHECK(dsu_connected(&d, 0, 2) && dsu_connected(&d, 1, 3));
    CHECK(!dsu_connected(&d, 0, 4) && !dsu_connected(&d, 4, 5));
    CHECK(dsu_union(&d, 4, 4) == 0);                            /* self-union changes nothing */
    dsu_free(&d);
    CHECK(d.parent == NULL && d.n == 0);
    dsu_free(&d);                                               /* double free is harmless */
}

/* Reference: Warshall transitive closure on the undirected graph (O(n^3)). */
static void test_against_closure(void) {
    enum { MAXN = 14 };
    for (int trial = 0; trial < 300; trial++) {
        int n = 1 + (int)(rng() % MAXN);
        int m = (int)(rng() % 20);
        char reach[MAXN][MAXN];
        memset(reach, 0, sizeof reach);
        for (int i = 0; i < n; i++) reach[i][i] = 1;
        Dsu d;
        CHECK(dsu_init(&d, n) == 0);
        for (int i = 0; i < m; i++) {
            int a = (int)(rng() % (unsigned)n), b = (int)(rng() % (unsigned)n);
            reach[a][b] = reach[b][a] = 1;
            dsu_union(&d, a, b);
        }
        for (int k = 0; k < n; k++)
            for (int i = 0; i < n; i++)
                for (int j = 0; j < n; j++)
                    if (reach[i][k] && reach[k][j]) reach[i][j] = 1;

        int ok = 1, components = 0;
        for (int i = 0; i < n; i++) {
            int is_first = 1;
            for (int j = 0; j < i; j++) if (reach[i][j]) is_first = 0;
            components += is_first;
            for (int j = 0; j < n; j++)
                if (dsu_connected(&d, i, j) != reach[i][j]) ok = 0;
        }
        CHECK(ok);
        CHECK(dsu_sets(&d) == components);
        dsu_free(&d);
    }
}

/* Path compression: after find(x), x points straight at the root. */
static void test_compression_and_depth(void) {
    enum { N = 1000000 };
    Dsu d;
    CHECK(dsu_init(&d, N) == 0);
    /* worst case for a naive union: always attach the big set under a new singleton */
    for (int i = 1; i < N; i++) dsu_union(&d, i, i - 1);
    CHECK(dsu_sets(&d) == 1);
    int root = dsu_find(&d, 0);
    CHECK(dsu_find(&d, N - 1) == root);
    CHECK(d.parent[N - 1] == root);                 /* compressed */
    int max_rank = 0;
    for (int i = 0; i < N; i++) if (d.rank[i] > max_rank) max_rank = d.rank[i];
    CHECK(max_rank <= 20);                          /* union by rank: height <= log2(N) */
    dsu_free(&d);
}

int main(void) {
    test_basic();
    test_against_closure();
    test_compression_and_depth();
    TEST_REPORT("test_dsu");
}
