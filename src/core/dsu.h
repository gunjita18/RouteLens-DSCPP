/*
 * dsu.h - union-find (disjoint-set union) with union by rank and path compression.
 *
 * Each element belongs to one set; find() returns the set's representative and
 * union() merges two sets. With both optimisations a sequence of m operations on
 * n elements costs O(m * alpha(n)), where alpha is the inverse Ackermann function
 * (below 5 for any realistic n), i.e. almost O(1) per operation.
 *
 * Used to count the weakly connected parts of the road network (edge direction
 * ignored), an independent check on the BFS graph cleaning.
 */
#ifndef ROUTELENS_DSU_H
#define ROUTELENS_DSU_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int *parent;
    int *rank;      /* upper bound on the height of the tree below the node */
    int n;
    int sets;       /* current number of sets */
} Dsu;

int  dsu_init(Dsu *d, int n);            /* n singleton sets; 0 ok, -1 on invalid n or out of memory */
int  dsu_find(Dsu *d, int x);            /* representative of x's set; x must be in [0, n) */
int  dsu_union(Dsu *d, int a, int b);    /* 1 if two different sets were merged, 0 if already together */
int  dsu_connected(Dsu *d, int a, int b);
int  dsu_sets(const Dsu *d);
void dsu_free(Dsu *d);

#ifdef __cplusplus
}
#endif
#endif
