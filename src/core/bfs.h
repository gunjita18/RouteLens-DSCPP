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

#include "graph.h"

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

/*
 * Reachability for the what-if scenario mode, on the routing graph itself:
 * can dst still be reached from src when some roads are closed, and in how few
 * road segments? Edge costs are ignored (BFS, not Dijkstra).
 *   blocked   optional: blocked[e] != 0 means edge e may not be used (NULL = none)
 *   visited   optional: set to the number of nodes the search reached before it
 *             stopped (it stops as soon as dst is taken out of the queue)
 * Returns the fewest edges from src to dst (0 if src == dst), BFS_UNREACHABLE (-1)
 * if dst cannot be reached, or BFS_ERROR (-2) on invalid input (NULL graph, node
 * out of range) or out of memory; *visited is then 0.
 * Uses the circular Queue and an int distance array: O(V + E) time, O(V) memory.
 */
#define BFS_UNREACHABLE (-1)
#define BFS_ERROR (-2)
int bfs_hops(const Graph *g, int src, int dst, const char *blocked, int *visited);

#ifdef __cplusplus
}
#endif
#endif
