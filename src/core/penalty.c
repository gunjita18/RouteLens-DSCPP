#include "penalty.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "hashtable.h"

PenaltyOptions penalty_default_options(void) {
    PenaltyOptions o;
    o.k = 3;
    o.factor = 1.4;
    o.max_overlap = 0.7;
    o.max_stretch = 0.0;
    o.max_iter = 0;
    return o;
}

void penalty_free(PenaltyResult *r) {
    if (r->routes != NULL)
        for (int i = 0; i < r->count; i++) path_free(&r->routes[i]);
    free(r->routes);
    r->routes = NULL;
    r->count = 0;
    r->iterations = 0;
}

static int options_valid(const PenaltyOptions *o) {
    if (o->k < 1 || o->k > PENALTY_MAX_K) return 0;
    if (!isfinite(o->factor) || o->factor <= 1.0) return 0;
    if (!(o->max_overlap > 0.0 && o->max_overlap <= 1.0)) return 0;
    if (o->max_stretch != 0.0 && !(isfinite(o->max_stretch) && o->max_stretch >= 1.0)) return 0;
    if (o->max_iter < 0) return 0;
    return 1;
}

static double real_cost(const PathResult *r, const double *cost) {
    double sum = 0.0;
    for (int i = 0; i < r->edges.size; i++) sum += cost[ia_get(&r->edges, i)];
    return sum;
}

/* Share of r's edges that are in the set (0 for a route without edges). */
static double shared_share(const PathResult *r, const HashTable *set) {
    if (r->edges.size == 0) return 0.0;
    int shared = 0;
    for (int i = 0; i < r->edges.size; i++)
        if (ht_contains(set, ia_get(&r->edges, i))) shared++;
    return (double)shared / (double)r->edges.size;
}

int penalty_routes(const Graph *g, const double *cost, int src, int dst, const char *blocked,
                   const PenaltyOptions *opt, PenaltyResult *out) {
    if (out == NULL) return -1;
    out->count = 0;
    out->routes = NULL;
    out->iterations = 0;
    if (g == NULL || cost == NULL || opt == NULL || !options_valid(opt)) return -1;

    const int max_iter = opt->max_iter > 0 ? opt->max_iter : 5 * opt->k;
    double *work = (double *)malloc((size_t)(g->edge_count > 0 ? g->edge_count : 1) * sizeof(double));
    PathResult *routes = (PathResult *)calloc((size_t)opt->k, sizeof(PathResult));
    HashTable *sets = (HashTable *)calloc((size_t)opt->k, sizeof(HashTable));   /* one edge set per accepted route */
    int accepted = 0, status = 0, iterations = 0;
    double cheapest = 0.0;
    if (work == NULL || routes == NULL || sets == NULL) status = -1;
    if (status == 0) memcpy(work, cost, (size_t)g->edge_count * sizeof(double));

    while (status == 0 && accepted < opt->k && iterations < max_iter) {
        PathResult r;
        if (dijkstra_path(g, work, src, dst, blocked, &r) != 0) { status = -1; break; }
        iterations++;
        if (!r.found) { path_free(&r); break; }               /* nothing (more) to find */
        r.cost = real_cost(&r, cost);                         /* report the unpenalised cost */

        int keep = 1;
        if (accepted == 0) {
            cheapest = r.cost;
        } else {
            if (opt->max_stretch > 0.0 && r.cost > opt->max_stretch * cheapest) keep = 0;
            for (int i = 0; keep && i < accepted; i++)
                if (shared_share(&r, &sets[i]) > opt->max_overlap) keep = 0;
        }

        if (keep) {
            if (ht_init(&sets[accepted], 64) != 0) { path_free(&r); status = -1; break; }
            for (int i = 0; i < r.edges.size; i++)
                if (ht_put(&sets[accepted], ia_get(&r.edges, i), 1) != 0) status = -1;
            if (status != 0) { path_free(&r); ht_free(&sets[accepted]); break; }
            routes[accepted] = r;
            accepted++;
        }
        /* Penalise this route's edges whether or not it was kept, so the next search moves away from it.
         * A route with no edges (src == dst) cannot have alternatives. */
        for (int i = 0; i < r.edges.size; i++) work[ia_get(&r.edges, i)] *= opt->factor;
        if (!keep) path_free(&r);
        if (accepted == 1 && routes[0].edges.size == 0) break;
    }

    for (int i = 0; i < accepted; i++) ht_free(&sets[i]);
    free(sets);
    free(work);
    if (status != 0) {
        for (int i = 0; i < accepted; i++) path_free(&routes[i]);
        free(routes);
        return -1;
    }
    out->count = accepted;
    out->iterations = iterations;
    if (accepted == 0) {                                  /* nothing found: no array to free later */
        free(routes);
        routes = NULL;
    }
    out->routes = routes;
    return 0;
}
