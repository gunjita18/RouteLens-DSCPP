/*
 * bfs.h - breadth-first search on a directed graph, and graph cleaning.
 *
 * The graph is given as plain edge arrays (from[i] -> to[i], i < m) so BFS does
 * not depend on the Graph struct and can run before the routing graph exists.
 * Internally the edges are packed into compressed-adjacency arrays (CSR):
 * O(V + E) time and memory, and every search is O(V + E) using the circular
 * Queue from queue.h.
 */
#ifndef ROUTELENS_BFS_H
#define ROUTELENS_BFS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Marks every node reachable from src.
 *   reverse = 0: follow edges forwards  (nodes src can reach)
 *   reverse = 1: follow edges backwards (nodes that can reach src)
 * visited[0..n-1] is overwritten (1 = reached, 0 = not).
 * Returns the number of marked nodes (>= 1), or -1 on invalid input
 * (n < 1, src or an edge endpoint out of range) or out of memory.
 */
int bfs_reach(int n, int m, const int *from, const int *to,
              int src, int reverse, char *visited);

/*
 * Graph cleaning: keep[i] = 1 for nodes that src can reach AND that can reach
 * src. Those nodes are exactly src's strongly connected component, so inside
 * the kept set a route exists between every pair of nodes.
 * Returns the number of kept nodes, or -1 as for bfs_reach.
 */
int bfs_strong_core(int n, int m, const int *from, const int *to,
                    int center, char *keep);

#ifdef __cplusplus
}
#endif
#endif
