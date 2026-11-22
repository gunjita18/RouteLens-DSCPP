#include "../src/core/dijkstra.h"
#include "test_util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long rng_state = 0x14057B7EF767814FULL;
static unsigned rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (unsigned)(rng_state >> 11);
}

/* Adds an edge with distance 1 m (the C search uses the separate cost array). Returns its index. */
static int edge(Graph *g, int a, int b) { return graph_add_edge(g, a, b, 1.0, 1.0, 0.0, NULL, 0); }

static int route_is_valid(const Graph *g, const PathResult *r, int src, int dst, const double *cost) {
    int at = src;
    double sum = 0.0;
    for (int i = 0; i < r->edges.size; i++) {
        int e = ia_get(&r->edges, i);
        if (e < 0 || e >= g->edge_count || g->edges[e].from != at) return 0;
        at = g->edges[e].to;
        sum += cost[e];
    }
    return at == dst && fabs(sum - r->cost) < 1e-9;
}

/* The classic example: 0->1->2->3 (1+1+1) beats 0->2 (4) and 1->3 (5). */
static void test_small(void) {
    Graph *g = graph_create(4);
    double cost[8];
    int e01 = edge(g, 0, 1), e02 = edge(g, 0, 2), e12 = edge(g, 1, 2), e13 = edge(g, 1, 3), e23 = edge(g, 2, 3);
    cost[e01] = 1; cost[e02] = 4; cost[e12] = 1; cost[e13] = 5; cost[e23] = 1;

    PathResult r;
    CHECK(dijkstra_path(g, cost, 0, 3, NULL, &r) == 0 && r.found);
    CHECK(r.cost == 3.0 && r.edges.size == 3);
    CHECK(ia_get(&r.edges, 0) == e01 && ia_get(&r.edges, 1) == e12 && ia_get(&r.edges, 2) == e23);
    CHECK(r.settled >= 1 && r.settled <= 4);
    path_free(&r);

    CHECK(dijkstra_path(g, cost, 2, 2, NULL, &r) == 0 && r.found && r.cost == 0.0 && r.edges.size == 0);   /* same node */
    path_free(&r);
    CHECK(dijkstra_path(g, cost, 3, 0, NULL, &r) == 0 && !r.found);                                       /* directed: no way back */
    path_free(&r);

    char blocked[8] = { 0 };
    blocked[e12] = 1;                                                    /* closing 1->2 forces 0->2->3 (4+1) */
    CHECK(dijkstra_path(g, cost, 0, 3, blocked, &r) == 0 && r.found && r.cost == 5.0);
    CHECK(r.edges.size == 2 && ia_get(&r.edges, 0) == e02);
    path_free(&r);
    blocked[e23] = 1;                                                    /* closing 2->3 as well: only 1->3 left (1+5) */
    CHECK(dijkstra_path(g, cost, 0, 3, blocked, &r) == 0 && r.found && r.cost == 6.0);
    path_free(&r);
    blocked[e13] = 1;                                                    /* now 3 is unreachable */
    CHECK(dijkstra_path(g, cost, 0, 3, blocked, &r) == 0 && !r.found);
    path_free(&r);
    graph_free(g);
}

static void test_parallel_and_zero_cost(void) {
    Graph *g = graph_create(3);
    double cost[8];
    int cheap = edge(g, 0, 1), dear = edge(g, 0, 1), z1 = edge(g, 1, 2), z2 = edge(g, 2, 1);
    cost[cheap] = 2; cost[dear] = 9; cost[z1] = 0; cost[z2] = 0;         /* a zero-cost cycle must not loop forever */
    PathResult r;
    CHECK(dijkstra_path(g, cost, 0, 2, NULL, &r) == 0 && r.found && r.cost == 2.0);
    CHECK(ia_get(&r.edges, 0) == cheap);                                 /* parallel edges: the cheaper one */
    path_free(&r);
    graph_free(g);
}

static void test_invalid_input(void) {
    Graph *g = graph_create(3);
    double cost[4];
    int a = edge(g, 0, 1), b = edge(g, 1, 2);
    cost[a] = 1; cost[b] = 1;
    PathResult r;
    CHECK(dijkstra_path(NULL, cost, 0, 1, NULL, &r) == -1);
    CHECK(dijkstra_path(g, NULL, 0, 1, NULL, &r) == -1);
    CHECK(dijkstra_path(g, cost, 0, 1, NULL, NULL) == -1);
    CHECK(dijkstra_path(g, cost, -1, 1, NULL, &r) == -1 && !r.found);
    CHECK(dijkstra_path(g, cost, 0, 3, NULL, &r) == -1);                 /* node out of range */
    cost[b] = -1.0;                                                      /* negative cost breaks Dijkstra's assumption */
    CHECK(dijkstra_path(g, cost, 0, 2, NULL, &r) == -1);
    cost[b] = NAN;
    CHECK(dijkstra_path(g, cost, 0, 2, NULL, &r) == -1);
    cost[b] = INFINITY;
    CHECK(dijkstra_path(g, cost, 0, 2, NULL, &r) == -1);
    cost[b] = 1.0;
    CHECK(dijkstra_path(g, cost, 0, 2, NULL, &r) == 0 && r.found);
    path_free(&r);
    graph_free(g);
}

/* Reference: Bellman-Ford (relax every edge n-1 times). Shares no code with Dijkstra. */
static double bellman_ford(const Graph *g, const double *cost, const char *blocked, int src, int dst) {
    double *d = (double *)malloc((size_t)g->node_count * sizeof(double));
    for (int i = 0; i < g->node_count; i++) d[i] = INFINITY;
    d[src] = 0.0;
    for (int round = 0; round < g->node_count; round++)
        for (int e = 0; e < g->edge_count; e++) {
            if (blocked && blocked[e]) continue;
            if (d[g->edges[e].from] + cost[e] < d[g->edges[e].to]) d[g->edges[e].to] = d[g->edges[e].from] + cost[e];
        }
    double result = d[dst];
    free(d);
    return result;
}

static void test_against_bellman_ford(void) {
    for (int trial = 0; trial < 400; trial++) {
        int n = 2 + (int)(rng() % 14), m = (int)(rng() % 60);
        Graph *g = graph_create(n);
        double cost[64];
        char blocked[64];
        memset(blocked, 0, sizeof blocked);
        for (int i = 0; i < m; i++) {
            int e = edge(g, (int)(rng() % (unsigned)n), (int)(rng() % (unsigned)n));
            if (e < 0) continue;                                         /* self-loops are rejected by the graph */
            cost[e] = (double)(rng() % 20) + (rng() % 4 == 0 ? 0.0 : 0.5);   /* includes zero costs */
            blocked[e] = (rng() % 6 == 0);
        }
        int use_blocked = (int)(rng() % 2);
        int src = (int)(rng() % (unsigned)n), dst = (int)(rng() % (unsigned)n);
        PathResult r;
        CHECK(dijkstra_path(g, cost, src, dst, use_blocked ? blocked : NULL, &r) == 0);
        double ref = bellman_ford(g, cost, use_blocked ? blocked : NULL, src, dst);
        if (isfinite(ref)) {
            CHECK(r.found && fabs(r.cost - ref) < 1e-9);
            CHECK(route_is_valid(g, &r, src, dst, cost));
            for (int i = 0; use_blocked && i < r.edges.size; i++) CHECK(!blocked[ia_get(&r.edges, i)]);
        } else {
            CHECK(!r.found);
        }
        CHECK(r.settled <= n);
        path_free(&r);
        graph_free(g);
    }
}

/* A long chain: no recursion anywhere, so depth is not a problem. */
static void test_long_chain(void) {
    enum { N = 200000 };
    Graph *g = graph_create(N);
    double *cost = (double *)malloc(N * sizeof(double));
    for (int i = 0; i + 1 < N; i++) { int e = edge(g, i, i + 1); cost[e] = 1.0; }
    PathResult r;
    CHECK(dijkstra_path(g, cost, 0, N - 1, NULL, &r) == 0 && r.found);
    CHECK(r.edges.size == N - 1 && r.cost == (double)(N - 1));
    path_free(&r);
    free(cost);
    graph_free(g);
}

int main(void) {
    test_small();
    test_parallel_and_zero_cost();
    test_invalid_input();
    test_against_bellman_ford();
    test_long_chain();
    TEST_REPORT("test_c_dijkstra");
}
