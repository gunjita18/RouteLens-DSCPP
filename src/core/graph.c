#include "graph.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

Graph *graph_create(int node_count) {
    if (node_count <= 0) return NULL;
    Graph *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->node_count = node_count;
    g->first_out = malloc((size_t)node_count * sizeof *g->first_out);
    if (!g->first_out) { free(g); return NULL; }
    for (int i = 0; i < node_count; i++) g->first_out[i] = -1;
    return g;
}

void graph_free(Graph *g) {
    if (!g) return;
    for (int i = 0; i < g->edge_count; i++) free(g->edges[i].signal_ids);
    free(g->edges);
    free(g->next_out);
    free(g->first_out);
    free(g);
}

static int finite_nonneg(double x) { return isfinite(x) && x >= 0.0; }

int graph_add_edge(Graph *g, int from, int to,
                   double distance_m, double duration_s, double traffic_index,
                   const long long *signal_ids, int signal_count) {
    if (!g || from < 0 || to < 0 || from >= g->node_count || to >= g->node_count) return -1;
    if (from == to) return -1;
    if (!finite_nonneg(distance_m) || !finite_nonneg(duration_s)) return -1;
    if (!isfinite(traffic_index) || traffic_index < 0.0 || traffic_index > 1.0) return -1;
    if (signal_count < 0 || (signal_count > 0 && !signal_ids)) return -1;

    if (g->edge_count == g->edge_capacity) {
        int cap = g->edge_capacity ? g->edge_capacity * 2 : 16;
        Edge *ne = realloc(g->edges, (size_t)cap * sizeof *ne);
        if (!ne) return -1;
        g->edges = ne;
        int *nn = realloc(g->next_out, (size_t)cap * sizeof *nn);
        if (!nn) return -1;
        g->next_out = nn;
        g->edge_capacity = cap;
    }

    Edge *e = &g->edges[g->edge_count];
    e->from = from;
    e->to = to;
    e->distance_m = distance_m;
    e->duration_s = duration_s;
    e->traffic_index = traffic_index;
    e->signal_count = 0;
    e->signal_ids = NULL;
    if (signal_count > 0) {
        e->signal_ids = malloc((size_t)signal_count * sizeof *e->signal_ids);
        if (!e->signal_ids) return -1;
        memcpy(e->signal_ids, signal_ids, (size_t)signal_count * sizeof *e->signal_ids);
        e->signal_count = signal_count;
    }

    int idx = g->edge_count++;
    g->next_out[idx] = g->first_out[from];   /* push-front into from's list */
    g->first_out[from] = idx;
    return idx;
}

int weights_valid(const Weights *w) {
    if (!w) return 0;
    double v[4] = { w->per_km, w->per_minute, w->per_congested_minute, w->per_signal };
    double sum = 0.0;
    for (int i = 0; i < 4; i++) {
        if (!finite_nonneg(v[i])) return 0;
        sum += v[i];
    }
    return sum > 0.0;   /* all-zero weights would make every path cost 0 */
}

int graph_compute_costs(const Graph *g, const Weights *w, double *out_costs) {
    if (!g || !out_costs || !weights_valid(w)) return 0;
    for (int i = 0; i < g->edge_count; i++) {
        const Edge *e = &g->edges[i];
        double km = e->distance_m / 1000.0;
        double min = e->duration_s / 60.0;
        out_costs[i] = w->per_km * km
                     + w->per_minute * min
                     + w->per_congested_minute * e->traffic_index * min
                     + w->per_signal * (double)e->signal_count;
    }
    return 1;
}
