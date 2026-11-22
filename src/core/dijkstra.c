#include "dijkstra.h"

#include <math.h>
#include <stdlib.h>

#include "minheap.h"
#include "stack.h"

void path_init(PathResult *r) {
    r->found = 0;
    r->cost = 0.0;
    ia_init(&r->edges);
    r->settled = 0;
}

void path_free(PathResult *r) {
    ia_free(&r->edges);
    r->found = 0;
    r->cost = 0.0;
    r->settled = 0;
}

static int costs_valid(const Graph *g, const double *cost) {
    for (int e = 0; e < g->edge_count; e++)
        if (!isfinite(cost[e]) || cost[e] < 0.0) return 0;
    return 1;
}

int dijkstra_path(const Graph *g, const double *cost, int src, int dst,
                  const char *blocked, PathResult *out) {
    if (out == NULL) return -1;
    path_init(out);
    if (g == NULL || cost == NULL || src < 0 || dst < 0 || src >= g->node_count || dst >= g->node_count)
        return -1;
    if (!costs_valid(g, cost)) return -1;

    const int n = g->node_count;
    double *dist = (double *)malloc((size_t)n * sizeof(double));
    int *parent_edge = (int *)malloc((size_t)n * sizeof(int));
    MinHeap heap;
    if (dist == NULL || parent_edge == NULL || heap_init(&heap, 1024) != 0) {
        free(dist);
        free(parent_edge);
        return -1;
    }
    for (int i = 0; i < n; i++) { dist[i] = INFINITY; parent_edge[i] = -1; }

    dist[src] = 0.0;
    int status = 0;
    if (heap_push(&heap, 0.0, src) != 0) status = -1;

    while (status == 0 && !heap_empty(&heap)) {
        HeapItem it = heap_pop(&heap);
        if (it.key > dist[it.node]) continue;             /* outdated entry: a cheaper one was pushed later */
        out->settled++;
        if (it.node == dst) break;                        /* the cheapest cost to dst is final */
        for (int e = g->first_out[it.node]; e != -1; e = g->next_out[e]) {
            if (blocked != NULL && blocked[e]) continue;
            int v = g->edges[e].to;
            double nd = dist[it.node] + cost[e];
            if (nd < dist[v]) {
                dist[v] = nd;
                parent_edge[v] = e;
                if (heap_push(&heap, nd, v) != 0) { status = -1; break; }   /* lazy deletion: push again */
            }
        }
    }

    if (status == 0 && isfinite(dist[dst])) {
        /* Rebuild the route: parent edges lead from dst back to src, so push them on a stack
         * and pop them to get travelling order. */
        Stack st;
        st_init(&st);
        for (int v = dst; v != src && status == 0; v = g->edges[parent_edge[v]].from)
            if (st_push(&st, parent_edge[v]) != 0) status = -1;
        while (status == 0 && !st_empty(&st))
            if (ia_push(&out->edges, st_pop(&st)) != 0) status = -1;
        st_free(&st);
        if (status == 0) { out->found = 1; out->cost = dist[dst]; }
    }

    heap_free(&heap);
    free(dist);
    free(parent_edge);
    if (status != 0) { path_free(out); return -1; }
    return 0;
}
