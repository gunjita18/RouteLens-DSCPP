/*
 * dynarray.h - growable array of int (hand-written, no STL).
 *
 * Capacity doubles with realloc when full, so a push costs O(1) amortised:
 * n pushes copy at most 1 + 2 + 4 + ... + n < 2n elements in total.
 *
 * Used for: node/edge index lists, the edge list of a route.
 */
#ifndef ROUTELENS_DYNARRAY_H
#define ROUTELENS_DYNARRAY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int *data;
    int size;
    int cap;
} IntArray;

void ia_init(IntArray *a);                 /* empty array, no allocation yet */
int  ia_push(IntArray *a, int x);          /* 0 ok, -1 out of memory         */
int  ia_get(const IntArray *a, int i);     /* caller guarantees 0 <= i < size */
void ia_set(IntArray *a, int i, int x);    /* same precondition               */
void ia_clear(IntArray *a);                /* size = 0, keeps the buffer      */
void ia_free(IntArray *a);                 /* releases the buffer, re-inits   */

#ifdef __cplusplus
}
#endif
#endif
