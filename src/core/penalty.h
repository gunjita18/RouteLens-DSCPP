/*
 * penalty.h - alternative routes by the penalty method.
 *
 *   1. Find the cheapest route with Dijkstra.
 *   2. Multiply the cost of every edge on it by `factor` (default 1.4).
 *   3. Run Dijkstra again: the new route tends to use other roads.
 *   4. Repeat; keep a new route only if it is not a near-duplicate.
 *
 * Near-duplicate rule (overlap check): the edges of each accepted route are put in a
 * hash set (hashtable.c); a new route is dropped if more than `max_overlap` (default 70 %)
 * of ITS edges are in the set of an accepted route. A route costing more than
 * `max_stretch` times the cheapest route is dropped too (0 = no limit).
 *
 * The cost reported for every route is the REAL cost (the original, unpenalised
 * costs). Cost: at most max_iter Dijkstra runs, plus O(route length) per overlap check.
 */
#ifndef ROUTELENS_PENALTY_H
#define ROUTELENS_PENALTY_H

#include "dijkstra.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int k;               /* routes wanted (the first is the cheapest), 1 .. PENALTY_MAX_K */
    double factor;       /* cost multiplier for the edges of each found route, finite and > 1 */
    double max_overlap;  /* drop a route if more than this share of its edges is shared, in (0, 1] */
    double max_stretch;  /* drop a route costing more than this times the cheapest; 0 = off, else >= 1 */
    int max_iter;        /* Dijkstra runs allowed; 0 = 5 * k */
} PenaltyOptions;

#define PENALTY_MAX_K 64

typedef struct {
    int count;           /* routes found, 0 if the destination is unreachable */
    PathResult *routes;  /* routes[0] is the cheapest; cost is the real (unpenalised) cost; NULL if count == 0 */
    int iterations;      /* Dijkstra runs actually made */
} PenaltyResult;

PenaltyOptions penalty_default_options(void);   /* k = 3, factor 1.4, overlap 0.7, no stretch limit */
void penalty_free(PenaltyResult *r);

/* Returns 0 on success, -1 on invalid input (as for dijkstra_path, or invalid options) or out
 * of memory. cost[] is not modified. out is overwritten. */
int penalty_routes(const Graph *g, const double *cost, int src, int dst, const char *blocked,
                   const PenaltyOptions *opt, PenaltyResult *out);

#ifdef __cplusplus
}
#endif
#endif
