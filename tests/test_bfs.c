#include "../src/core/bfs.h"
#include "test_util.h"

#include <string.h>

static unsigned long long rng_state = 0x9E3779B97F4A7C15ULL;
static unsigned rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (unsigned)(rng_state >> 11);
}

static void test_small(void) {
    /* 0->1->2->0 is a cycle; 2->3 leaves it; 3<->4; 5 is isolated; 6->0 enters it */
    int from[] = { 0, 1, 2, 2, 3, 4, 6 };
    int to[]   = { 1, 2, 0, 3, 4, 3, 0 };
    char vis[7];

    CHECK(bfs_reach(7, 7, from, to, 0, 0, vis) == 5);       /* 0,1,2,3,4 */
    CHECK(vis[0] && vis[1] && vis[2] && vis[3] && vis[4] && !vis[5] && !vis[6]);

    CHECK(bfs_reach(7, 7, from, to, 0, 1, vis) == 4);       /* who can reach 0: 0,1,2,6 */
    CHECK(vis[0] && vis[1] && vis[2] && vis[6] && !vis[3] && !vis[4] && !vis[5]);

    CHECK(bfs_reach(7, 7, from, to, 5, 0, vis) == 1 && vis[5]);   /* isolated node */

    char keep[7];
    CHECK(bfs_strong_core(7, 7, from, to, 0, keep) == 3);   /* the cycle only */
    CHECK(keep[0] && keep[1] && keep[2] && !keep[3] && !keep[4] && !keep[5] && !keep[6]);
    CHECK(bfs_strong_core(7, 7, from, to, 3, keep) == 2);   /* 3<->4 */
    CHECK(keep[3] && keep[4] && !keep[0]);
}

static void test_edge_cases(void) {
    char vis[4];
    int from[] = { 0 }, to[] = { 1 };
    CHECK(bfs_reach(1, 0, NULL, NULL, 0, 0, vis) == 1);     /* one node, no edges */
    CHECK(bfs_reach(0, 0, NULL, NULL, 0, 0, vis) == -1);    /* no nodes           */
    CHECK(bfs_reach(2, 1, from, to, 2, 0, vis) == -1);      /* src out of range   */
    CHECK(bfs_reach(2, 1, from, to, -1, 0, vis) == -1);
    int bad_to[] = { 5 };
    CHECK(bfs_reach(2, 1, from, bad_to, 0, 0, vis) == -1);  /* edge endpoint out of range */
    CHECK(bfs_reach(2, 1, NULL, to, 0, 0, vis) == -1);      /* missing arrays     */
    CHECK(bfs_reach(2, 1, from, to, 0, 0, NULL) == -1);

    int self[] = { 0 };                                     /* self-loop: still terminates */
    CHECK(bfs_reach(1, 1, self, self, 0, 0, vis) == 1);
    int dup_f[] = { 0, 0, 0 }, dup_t[] = { 1, 1, 1 };       /* parallel edges: node queued once */
    CHECK(bfs_reach(2, 3, dup_f, dup_t, 0, 0, vis) == 2);
}

/* Reference: Warshall transitive closure, O(n^3), shares no code with BFS. */
static void test_against_closure(void) {
    enum { MAXN = 12, MAXM = 40 };
    for (int trial = 0; trial < 300; trial++) {
        int n = 2 + (int)(rng() % (MAXN - 1));
        int m = (int)(rng() % (MAXM + 1));
        int from[MAXM + 1], to[MAXM + 1];
        char reach[MAXN][MAXN];
        memset(reach, 0, sizeof reach);
        for (int i = 0; i < n; i++) reach[i][i] = 1;
        for (int i = 0; i < m; i++) {
            from[i] = (int)(rng() % (unsigned)n);
            to[i] = (int)(rng() % (unsigned)n);
            reach[from[i]][to[i]] = 1;
        }
        for (int k = 0; k < n; k++)
            for (int i = 0; i < n; i++)
                for (int j = 0; j < n; j++)
                    if (reach[i][k] && reach[k][j]) reach[i][j] = 1;

        int src = (int)(rng() % (unsigned)n);
        char fwd[MAXN], rev[MAXN], keep[MAXN];
        int cf = bfs_reach(n, m, from, to, src, 0, fwd);
        int cr = bfs_reach(n, m, from, to, src, 1, rev);
        int ck = bfs_strong_core(n, m, from, to, src, keep);

        int ef = 0, er = 0, ek = 0, ok = 1;
        for (int v = 0; v < n; v++) {
            if (fwd[v] != reach[src][v]) ok = 0;
            if (rev[v] != reach[v][src]) ok = 0;
            if (keep[v] != (reach[src][v] && reach[v][src])) ok = 0;
            ef += reach[src][v];
            er += reach[v][src];
            ek += reach[src][v] && reach[v][src];
        }
        CHECK(ok);
        CHECK(cf == ef && cr == er && ck == ek);
    }
}

/* A 300,000-node cycle: BFS is iterative, so there is no recursion to overflow. */
static void test_large_cycle(void) {
    enum { N = 300000 };
    int *from = (int *)malloc(N * sizeof(int));
    int *to = (int *)malloc(N * sizeof(int));
    char *keep = (char *)malloc(N);
    for (int i = 0; i < N; i++) { from[i] = i; to[i] = (i + 1) % N; }
    CHECK(bfs_strong_core(N, N, from, to, 0, keep) == N);
    to[N - 1] = N - 1;                      /* break the cycle: now it is a chain */
    CHECK(bfs_strong_core(N, N, from, to, 0, keep) == 1);
    free(from); free(to); free(keep);
}

int main(void) {
    test_small();
    test_edge_cases();
    test_against_closure();
    test_large_cycle();
    TEST_REPORT("test_bfs");
}
