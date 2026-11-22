#include "dynarray.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>

#define IA_INITIAL_CAP 8

void ia_init(IntArray *a) {
    a->data = NULL;
    a->size = 0;
    a->cap = 0;
}

int ia_push(IntArray *a, int x) {
    if (a->size == a->cap) {
        if (a->cap > INT_MAX / 2) return -1;            /* doubling would overflow */
        int new_cap = a->cap == 0 ? IA_INITIAL_CAP : a->cap * 2;
        int *p = (int *)realloc(a->data, (size_t)new_cap * sizeof(int));
        if (p == NULL) return -1;                       /* old buffer still valid  */
        a->data = p;
        a->cap = new_cap;
    }
    a->data[a->size++] = x;
    return 0;
}

int ia_get(const IntArray *a, int i) {
    assert(i >= 0 && i < a->size);
    return a->data[i];
}

void ia_set(IntArray *a, int i, int x) {
    assert(i >= 0 && i < a->size);
    a->data[i] = x;
}

void ia_clear(IntArray *a) {
    a->size = 0;
}

void ia_free(IntArray *a) {
    free(a->data);
    ia_init(a);
}
