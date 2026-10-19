/*
 * graph.h — Weighted directed graph (forward-star adjacency list).
 *
 * Nodes are waypoint indices 0..node_count-1. Each Edge stores the REAL
 * metrics fetched from OpenRouteService / Overpass; the scalar cost used by
 * the path algorithms is computed separately from a Weights vector, so the
 * same graph can be searched under different user priorities.
 */
#ifndef ROUTELENS_GRAPH_H
#define ROUTELENS_GRAPH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int from;
    int to;
    double distance_m;      /* road distance, metres (ORS)                 */
    double duration_s;      /* free-flow travel time, seconds (ORS)        */
    double traffic_index;   /* 0 = low .. 1 = high congestion LIKELIHOOD   */
    long long *signal_ids;  /* OSM node ids of traffic signals on the edge */
    int signal_count;
} Edge;

typedef struct {
    int node_count;
    int edge_count;
    int edge_capacity;
    Edge *edges;
    int *first_out;   /* node -> index of its first outgoing edge, or -1 */
    int *next_out;    /* edge -> next outgoing edge of the same node, or -1 */
} Graph;

/* User priorities. Every edge cost is a non-negative linear combination:
 *   cost = per_km * km + per_minute * min
 *        + per_congested_minute * (traffic_index * min)
 *        + per_signal * signal_count
 * Non-negativity is what makes Dijkstra (and therefore Yen) correct. */
typedef struct {
    double per_km;
    double per_minute;
    double per_congested_minute;
    double per_signal;
} Weights;

Graph *graph_create(int node_count);
void   graph_free(Graph *g);

/* Returns the new edge index, or -1 on invalid input / allocation failure.
 * signal_ids is copied; it may be NULL when signal_count == 0. */
int graph_add_edge(Graph *g, int from, int to,
                   double distance_m, double duration_s, double traffic_index,
                   const long long *signal_ids, int signal_count);

int  weights_valid(const Weights *w);
/* Fills out_costs[0..edge_count-1]. Returns 0 if weights are invalid. */
int  graph_compute_costs(const Graph *g, const Weights *w, double *out_costs);

#ifdef __cplusplus
}
#endif
#endif
