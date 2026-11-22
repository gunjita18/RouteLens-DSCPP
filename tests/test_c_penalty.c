#include "../src/core/penalty.h"
#include "test_util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long rng_state = 0x2545F4914F6CDD1DULL;
static unsigned rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (unsigned)(rng_state >> 11);
}

static int edge(Graph *g, int a, int b) { return graph_add_edge(g, a, b, 1.0, 1.0, 0.0, NULL, 0); }

static int route_ok(const Graph *g, const PathResult *r, int src, int dst, const double *cost) {
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

static int same_route(const PathResult *a, const PathResult *b) {
    if (a->edges.size != b->edges.size) return 0;
    for (int i = 0; i < a->edges.size; i++)
        if (ia_get(&a->edges, i) != ia_get(&b->edges, i)) return 0;
    return 1;
}

/* Three disjoint routes 0 -> M -> 5 through M = 1, 2, 3 with costs 2+2, 3+3, 5+5. */
static Graph *three_roads(double *cost) {
    Graph *g = graph_create(5);
    const double c[3] = { 2, 3, 5 };
    for (int m = 1; m <= 3; m++) {
        int a = edge(g, 0, m), b = edge(g, m, 4);
        cost[a] = c[m - 1];
        cost[b] = c[m - 1];
    }
    return g;
}

static void test_disjoint_alternatives(void) {
    double cost[16];
    Graph *g = three_roads(cost);
    PenaltyOptions o = penalty_default_options();
    CHECK(o.k == 3 && o.factor == 1.4 && o.max_overlap == 0.7 && o.max_stretch == 0.0);
    PenaltyResult res;
    CHECK(penalty_routes(g, cost, 0, 4, NULL, &o, &res) == 0);
    CHECK(res.count == 3);
    CHECK(res.routes[0].cost == 4.0 && res.routes[1].cost == 6.0 && res.routes[2].cost == 10.0);   /* real, unpenalised costs */
    for (int i = 0; i < res.count; i++) CHECK(route_ok(g, &res.routes[i], 0, 4, cost));
    CHECK(!same_route(&res.routes[0], &res.routes[1]) && !same_route(&res.routes[1], &res.routes[2]));
    CHECK(res.iterations >= 3);
    CHECK(cost[0] == 2.0);                                              /* the caller's costs are untouched */
    penalty_free(&res);
    CHECK(res.routes == NULL && res.count == 0);

    o.k = 2;
    CHECK(penalty_routes(g, cost, 0, 4, NULL, &o, &res) == 0 && res.count == 2);   /* k limits the result */
    penalty_free(&res);

    o.k = 3;
    o.max_stretch = 2.0;                                                /* 10 > 2 * 4: the dearest route is dropped */
    CHECK(penalty_routes(g, cost, 0, 4, NULL, &o, &res) == 0 && res.count == 2);
    penalty_free(&res);
    graph_free(g);
}

/* Main route 0-1-2-3-4-7 (5 edges, cost 5). The only other simple route, 0-1-2-3-4-5-7, has 6 edges
 * and shares 4 of them with the main route: 4/6 = 66.7 % of the NEW route's edges.
 * Dijkstra returns the main route again until its edges have been penalised enough (x1.4 each
 * time) for the variant to win; duplicates (100 % shared) are dropped on the way. */
static void test_overlap_rule(void) {
    Graph *g = graph_create(8);
    double cost[32];
    int e;
    for (int i = 0; i < 4; i++) { e = edge(g, i, i + 1); cost[e] = 1.0; }   /* 0-1-2-3-4 */
    e = edge(g, 4, 7); cost[e] = 1.0;
    e = edge(g, 4, 5); cost[e] = 1.0;
    e = edge(g, 5, 7); cost[e] = 1.0;
    PenaltyOptions o = penalty_default_options();
    PenaltyResult res;
    CHECK(penalty_routes(g, cost, 0, 7, NULL, &o, &res) == 0);
    CHECK(res.count == 2);                                              /* 66.7 % <= 70 %: the variant is kept */
    CHECK(res.routes[0].cost == 5.0 && res.routes[1].cost == 6.0);      /* real costs, not penalised ones */
    CHECK(res.routes[0].edges.size == 5 && res.routes[1].edges.size == 6);
    CHECK(res.iterations > 2);                                          /* duplicates were found and dropped first */
    penalty_free(&res);

    o.max_overlap = 0.6;                                                /* 66.7 % > 60 %: now a near-duplicate */
    CHECK(penalty_routes(g, cost, 0, 7, NULL, &o, &res) == 0);
    CHECK(res.count == 1 && res.routes[0].cost == 5.0);
    penalty_free(&res);
    graph_free(g);
}

/* Two routes of equal cost 5: A = 0-1-2-3-4-9, B = 0-1-2-5-6-9. They share 2 of 5 edges (40 %),
 * whichever one Dijkstra finds first. Kept at the default 70 %, dropped at 30 %. */
static void test_overlap_threshold(void) {
    Graph *g = graph_create(10);
    double cost[32];
    int e;
    for (int i = 0; i < 4; i++) { e = edge(g, i, i + 1); cost[e] = 1.0; }   /* 0-1-2-3-4 */
    e = edge(g, 4, 9); cost[e] = 1.0;
    e = edge(g, 2, 5); cost[e] = 1.0;
    e = edge(g, 5, 6); cost[e] = 1.0;
    e = edge(g, 6, 9); cost[e] = 1.0;
    PenaltyOptions o = penalty_default_options();
    o.k = 2;
    PenaltyResult res;
    CHECK(penalty_routes(g, cost, 0, 9, NULL, &o, &res) == 0);
    CHECK(res.count == 2);                                              /* 2 of 5 edges shared = 40 %: kept */
    CHECK(res.routes[0].cost == 5.0 && res.routes[1].cost == 5.0);
    penalty_free(&res);
    o.max_overlap = 0.3;                                                /* 40 % > 30 %: now a near-duplicate */
    CHECK(penalty_routes(g, cost, 0, 9, NULL, &o, &res) == 0);
    CHECK(res.count == 1);
    penalty_free(&res);
    graph_free(g);
}

static void test_edge_cases(void) {
    double cost[16];
    Graph *g = three_roads(cost);
    PenaltyOptions o = penalty_default_options();
    PenaltyResult res;

    CHECK(penalty_routes(g, cost, 2, 2, NULL, &o, &res) == 0 && res.count == 1 && res.routes[0].edges.size == 0);   /* src == dst */
    penalty_free(&res);
    CHECK(penalty_routes(g, cost, 4, 0, NULL, &o, &res) == 0 && res.count == 0 && res.routes == NULL);              /* unreachable */
    penalty_free(&res);

    char blocked[16];
    memset(blocked, 0, sizeof blocked);
    blocked[0] = 1;                                                     /* edge 0 is 0->1: the cheapest road is closed */
    CHECK(penalty_routes(g, cost, 0, 4, blocked, &o, &res) == 0 && res.count == 2 && res.routes[0].cost == 6.0);
    penalty_free(&res);

    PenaltyOptions bad = o;
    bad.k = 0;                      CHECK(penalty_routes(g, cost, 0, 4, NULL, &bad, &res) == -1);
    bad = o; bad.k = PENALTY_MAX_K + 1; CHECK(penalty_routes(g, cost, 0, 4, NULL, &bad, &res) == -1);
    bad = o; bad.factor = 1.0;      CHECK(penalty_routes(g, cost, 0, 4, NULL, &bad, &res) == -1);
    bad = o; bad.factor = NAN;      CHECK(penalty_routes(g, cost, 0, 4, NULL, &bad, &res) == -1);
    bad = o; bad.max_overlap = 0.0; CHECK(penalty_routes(g, cost, 0, 4, NULL, &bad, &res) == -1);
    bad = o; bad.max_overlap = 1.5; CHECK(penalty_routes(g, cost, 0, 4, NULL, &bad, &res) == -1);
    bad = o; bad.max_stretch = 0.5; CHECK(penalty_routes(g, cost, 0, 4, NULL, &bad, &res) == -1);
    bad = o; bad.max_iter = -1;     CHECK(penalty_routes(g, cost, 0, 4, NULL, &bad, &res) == -1);
    CHECK(penalty_routes(NULL, cost, 0, 4, NULL, &o, &res) == -1);
    CHECK(penalty_routes(g, cost, 0, 9, NULL, &o, &res) == -1);          /* node out of range */
    cost[0] = -1.0;
    CHECK(penalty_routes(g, cost, 0, 4, NULL, &o, &res) == -1);          /* negative cost */
    graph_free(g);
}

/* Random graphs: every route is a valid path, routes are pairwise different, the first one is
 * the Dijkstra cheapest, and reported costs are real. */
static void test_random_graphs(void) {
    for (int trial = 0; trial < 300; trial++) {
        int n = 3 + (int)(rng() % 12), m = 4 + (int)(rng() % 50);
        Graph *g = graph_create(n);
        double cost[64];
        for (int i = 0; i < m; i++) {
            int e = edge(g, (int)(rng() % (unsigned)n), (int)(rng() % (unsigned)n));
            if (e >= 0) cost[e] = 1.0 + (double)(rng() % 10);
        }
        int src = (int)(rng() % (unsigned)n), dst = (int)(rng() % (unsigned)n);
        PathResult best;
        CHECK(dijkstra_path(g, cost, src, dst, NULL, &best) == 0);
        PenaltyOptions o = penalty_default_options();
        o.k = 1 + (int)(rng() % 5);
        PenaltyResult res;
        CHECK(penalty_routes(g, cost, src, dst, NULL, &o, &res) == 0);
        if (!best.found) {
            CHECK(res.count == 0);
        } else {
            CHECK(res.count >= 1 && res.count <= o.k);
            CHECK(fabs(res.routes[0].cost - best.cost) < 1e-9);
            for (int i = 0; i < res.count; i++) {
                CHECK(route_ok(g, &res.routes[i], src, dst, cost));
                CHECK(res.routes[i].cost >= best.cost - 1e-9);
                for (int j = 0; j < i; j++) CHECK(!same_route(&res.routes[i], &res.routes[j]) || res.routes[i].edges.size == 0);
            }
        }
        penalty_free(&res);
        path_free(&best);
        graph_free(g);
    }
}

int main(void) {
    test_disjoint_alternatives();
    test_overlap_rule();
    test_overlap_threshold();
    test_edge_cases();
    test_random_graphs();
    TEST_REPORT("test_c_penalty");
}
