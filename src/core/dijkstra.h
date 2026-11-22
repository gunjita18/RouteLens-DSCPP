/*
 * dijkstra.h - cheapest route on the road graph (Dijkstra's algorithm).
 *
 * Uses the binary min-heap (minheap.c) as the priority queue with lazy deletion,
 * a distance array and a parent-edge array; the route is rebuilt by walking the
 * parent edges from the destination and pushing them on a stack (stack.c), so
 * popping gives the edges in travelling order.
 *
 * Cost: O((V + E) log V). Costs must be finite and NOT NEGATIVE: Dijkstra assumes a
 * node that has been settled can never get cheaper, which a negative cost would break.
 * Costs are given per edge (cost[e] for graph->edges[e]), so the same graph can be
 * searched under different priorities, scenarios or penalised costs.
 */
#ifndef ROUTELENS_DIJKSTRA_H
#define ROUTELENS_DIJKSTRA_H

#include "dynarray.h"
#include "graph.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int found;          /* 1 if the destination is reachable */
    double cost;        /* total cost of the route (0 for source == destination) */
    IntArray edges;     /* edge indices from source to destination, in order */
    int settled;        /* nodes settled before the search stopped (for evaluation) */
} PathResult;

void path_init(PathResult *r);
void path_free(PathResult *r);

/*
 * Finds the cheapest route from src to dst.
 *   cost     one finite, non-negative cost per edge (g->edge_count entries)
 *   blocked  optional: blocked[e] != 0 means edge e may not be used (NULL = none)
 * Returns 0 on success (check out->found), -1 on invalid input (NULL arguments, node
 * out of range, a negative / NaN / infinite cost) or out of memory. out is overwritten.
 */
int dijkstra_path(const Graph *g, const double *cost, int src, int dst,
                  const char *blocked, PathResult *out);

#ifdef __cplusplus
}
#endif
#endif
