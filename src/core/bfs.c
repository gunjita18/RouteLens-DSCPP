#include "bfs.h"

#include <stdlib.h>
#include <string.h>

#include "queue.h"

/*
 * Packs the edges into CSR form. The neighbours of node v are
 * adj[start[v] .. start[v+1]-1]. With reverse = 1 every edge is flipped.
 * Counting sort by source node: O(V + E).
 */
static int build_csr(int n, int m, const int *from, const int *to, int reverse,
                     int **start_out, int **adj_out) {
    const int *src = reverse ? to : from;
    const int *dst = reverse ? from : to;

    int *start = (int *)calloc((size_t)n + 1, sizeof(int));
    int *adj = (int *)malloc((size_t)(m > 0 ? m : 1) * sizeof(int));
    int *fill = (int *)malloc((size_t)n * sizeof(int));
    if (start == NULL || adj == NULL || fill == NULL) {
        free(start); free(adj); free(fill);
        return -1;
    }
    for (int i = 0; i < m; i++) start[src[i] + 1]++;          /* out-degree counts   */
    for (int v = 0; v < n; v++) start[v + 1] += start[v];     /* prefix sums         */
    memcpy(fill, start, (size_t)n * sizeof(int));
    for (int i = 0; i < m; i++) adj[fill[src[i]]++] = dst[i]; /* place each neighbour */
    free(fill);
    *start_out = start;
    *adj_out = adj;
    return 0;
}

static int valid_input(int n, int m, const int *from, const int *to, int src) {
    if (n < 1 || m < 0 || src < 0 || src >= n) return 0;
    if (m > 0 && (from == NULL || to == NULL)) return 0;
    for (int i = 0; i < m; i++)
        if (from[i] < 0 || from[i] >= n || to[i] < 0 || to[i] >= n) return 0;
    return 1;
}

int bfs_reach(int n, int m, const int *from, const int *to,
              int src, int reverse, char *visited) {
    if (visited == NULL || !valid_input(n, m, from, to, src)) return -1;

    int *start = NULL, *adj = NULL;
    if (build_csr(n, m, from, to, reverse != 0, &start, &adj) != 0) return -1;

    Queue q;
    if (q_init(&q, n) != 0) { free(start); free(adj); return -1; }

    memset(visited, 0, (size_t)n);
    visited[src] = 1;
    q_push(&q, src);                    /* cannot fail: capacity n, each node enqueued once */
    int count = 1;

    while (!q_empty(&q)) {
        int v = q_pop(&q);
        for (int k = start[v]; k < start[v + 1]; k++) {
            int w = adj[k];
            if (!visited[w]) {
                visited[w] = 1;         /* mark on enqueue so w is never queued twice */
                q_push(&q, w);
                count++;
            }
        }
    }

    q_free(&q);
    free(start);
    free(adj);
    return count;
}

int bfs_strong_core(int n, int m, const int *from, const int *to,
                    int center, char *keep) {
    if (keep == NULL || !valid_input(n, m, from, to, center)) return -1;

    char *back = (char *)malloc((size_t)n);
    if (back == NULL) return -1;

    int ok = bfs_reach(n, m, from, to, center, 0, keep);       /* reached from center */
    if (ok < 0 || bfs_reach(n, m, from, to, center, 1, back) < 0) {
        free(back);
        return -1;
    }
    int kept = 0;
    for (int i = 0; i < n; i++) {
        keep[i] = (char)(keep[i] && back[i]);                  /* both directions */
        kept += keep[i];
    }
    free(back);
    return kept;
}

int bfs_hops(const Graph *g, int src, int dst, const char *blocked, int *visited) {
    if (visited != NULL) *visited = 0;
    if (g == NULL || g->node_count < 1 || src < 0 || src >= g->node_count ||
        dst < 0 || dst >= g->node_count)
        return BFS_ERROR;

    const int n = g->node_count;
    int *hops = (int *)malloc((size_t)n * sizeof(int));   /* -1 = not reached yet */
    Queue q;
    if (hops == NULL || q_init(&q, n) != 0) { free(hops); return BFS_ERROR; }
    for (int v = 0; v < n; v++) hops[v] = -1;

    hops[src] = 0;
    q_push(&q, src);                    /* cannot fail: capacity n, each node enqueued once */
    int reached = 1;
    int result = BFS_UNREACHABLE;

    while (!q_empty(&q)) {
        int u = q_pop(&q);
        if (u == dst) {                 /* BFS takes nodes out in order of hops: this is the fewest */
            result = hops[u];
            break;
        }
        for (int e = g->first_out[u]; e != -1; e = g->next_out[e]) {
            if (blocked != NULL && blocked[e]) continue;
            int v = g->edges[e].to;
            if (v < 0 || v >= n || hops[v] != -1) continue;
            hops[v] = hops[u] + 1;      /* mark on enqueue so v is never queued twice */
            q_push(&q, v);
            reached++;
        }
    }

    q_free(&q);
    free(hops);
    if (visited != NULL) *visited = reached;
    return result;
}
