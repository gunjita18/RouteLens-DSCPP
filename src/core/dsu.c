#include "dsu.h"

#include <assert.h>
#include <stdlib.h>

int dsu_init(Dsu *d, int n) {
    d->parent = NULL;
    d->rank = NULL;
    d->n = 0;
    d->sets = 0;
    if (n < 0) return -1;
    if (n == 0) return 0;
    d->parent = (int *)malloc((size_t)n * sizeof(int));
    d->rank = (int *)calloc((size_t)n, sizeof(int));
    if (d->parent == NULL || d->rank == NULL) {
        dsu_free(d);
        return -1;
    }
    for (int i = 0; i < n; i++) d->parent[i] = i;      /* every element is its own root */
    d->n = n;
    d->sets = n;
    return 0;
}

int dsu_find(Dsu *d, int x) {
    assert(x >= 0 && x < d->n);
    int root = x;
    while (d->parent[root] != root) root = d->parent[root];
    while (d->parent[x] != root) {                      /* path compression: point everything on the path at the root */
        int next = d->parent[x];
        d->parent[x] = root;
        x = next;
    }
    return root;
}

int dsu_union(Dsu *d, int a, int b) {
    int ra = dsu_find(d, a), rb = dsu_find(d, b);
    if (ra == rb) return 0;
    if (d->rank[ra] < d->rank[rb]) { int t = ra; ra = rb; rb = t; }
    d->parent[rb] = ra;                                 /* the shallower tree goes under the deeper one */
    if (d->rank[ra] == d->rank[rb]) d->rank[ra]++;
    d->sets--;
    return 1;
}

int dsu_connected(Dsu *d, int a, int b) {
    return dsu_find(d, a) == dsu_find(d, b);
}

int dsu_sets(const Dsu *d) {
    return d->sets;
}

void dsu_free(Dsu *d) {
    free(d->parent);
    free(d->rank);
    d->parent = NULL;
    d->rank = NULL;
    d->n = 0;
    d->sets = 0;
}
