/* test_bfs_hops.c - bfs_hops: reachability and fewest edges on the routing graph,
 * with closures. Reference on random graphs: Dijkstra with every edge cost 1
 * (dijkstra.c, a different algorithm and data structure). */
#include "../src/core/bfs.h"
#include "../src/core/dijkstra.h"
#include "../src/core/graph.h"
#include "test_util.h"

#include <stdlib.h>

static unsigned long long rng_state = 0x2545F4914F6CDD1DULL;
static unsigned rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (unsigned)(rng_state >> 11);
}

static void add(Graph *g, int u, int v) { graph_add_edge(g, u, v, 1.0, 1.0, 0.0, NULL, 0); }

static void test_small(void) {
    /* A(0) -> B(1) -> C(2), C -> B -> A back, and D(3) that nothing reaches.
     * Edges: 0: 0->1   1: 1->2   2: 2->1   3: 1->0 */
    Graph *g = graph_create(4);
    add(g, 0, 1); add(g, 1, 2); add(g, 2, 1); add(g, 1, 0);
    int visited = -5;

    CHECK(bfs_hops(g, 0, 2, NULL, &visited) == 2);
    CHECK(visited == 3);                           /* A, B, C reached */
    CHECK(bfs_hops(g, 1, 1, NULL, &visited) == 0); /* source is the destination */
    CHECK(visited == 1);
    CHECK(bfs_hops(g, 0, 3, NULL, &visited) == BFS_UNREACHABLE);
    CHECK(visited == 3);                           /* searched everything it could reach */
    CHECK(bfs_hops(g, 0, 2, NULL, NULL) == 2);     /* visited is optional */

    /* close B -> C: C is cut off from A, but C can still reach A (directed graph) */
    char blocked[4] = { 0, 1, 0, 0 };
    CHECK(bfs_hops(g, 0, 2, blocked, &visited) == BFS_UNREACHABLE);
    CHECK(visited == 2);                           /* A and B only */
    CHECK(bfs_hops(g, 2, 0, blocked, &visited) == 2);

    /* invalid input */
    CHECK(bfs_hops(NULL, 0, 1, NULL, &visited) == BFS_ERROR && visited == 0);
    CHECK(bfs_hops(g, -1, 1, NULL, &visited) == BFS_ERROR && visited == 0);
    CHECK(bfs_hops(g, 0, 4, NULL, &visited) == BFS_ERROR && visited == 0);
    graph_free(g);
}

static void test_against_dijkstra(void) {
    enum { MAXN = 30, MAXM = 90 };
    double cost[MAXM];
    char blocked[MAXM];
    for (int i = 0; i < MAXM; i++) cost[i] = 1.0;
    int agree = 0, trials = 400;
    for (int t = 0; t < trials; t++) {
        int n = 2 + (int)(rng() % (MAXN - 1));
        int m = (int)(rng() % (MAXM + 1));
        Graph *g = graph_create(n);
        for (int i = 0; i < m; i++) {
            add(g, (int)(rng() % (unsigned)n), (int)(rng() % (unsigned)n));
            blocked[i] = (char)(rng() % 5 == 0);   /* about 20 % of roads closed */
        }
        int src = (int)(rng() % (unsigned)n), dst = (int)(rng() % (unsigned)n);
        const char *closures = (t % 2) ? blocked : NULL;

        int hops = bfs_hops(g, src, dst, closures, NULL);
        PathResult p;
        CHECK(dijkstra_path(g, cost, src, dst, closures, &p) == 0);
        if (p.found ? (hops == (int)p.cost) : (hops == BFS_UNREACHABLE)) agree++;
        path_free(&p);
        graph_free(g);
    }
    CHECK(agree == trials);
}

/* A 300,000-node chain: iterative, so no recursion depth; the queue never overflows. */
static void test_long_chain(void) {
    enum { N = 300000 };
    Graph *g = graph_create(N);
    for (int i = 0; i + 1 < N; i++) add(g, i, i + 1);
    int visited = 0;
    CHECK(bfs_hops(g, 0, N - 1, NULL, &visited) == N - 1);
    CHECK(visited == N);
    CHECK(bfs_hops(g, N - 1, 0, NULL, &visited) == BFS_UNREACHABLE && visited == 1);
    graph_free(g);
}

int main(void) {
    test_small();
    test_against_dijkstra();
    test_long_chain();
    TEST_REPORT("test_bfs_hops");
}
