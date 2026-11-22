#include "stack.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>

#define ST_INITIAL_CAP 16

void st_init(Stack *s) {
    s->data = NULL;
    s->top = 0;
    s->cap = 0;
}

int st_push(Stack *s, int x) {
    if (s->top == s->cap) {
        if (s->cap > INT_MAX / 2) return -1;
        int new_cap = s->cap == 0 ? ST_INITIAL_CAP : s->cap * 2;
        int *p = (int *)realloc(s->data, (size_t)new_cap * sizeof(int));
        if (p == NULL) return -1;
        s->data = p;
        s->cap = new_cap;
    }
    s->data[s->top++] = x;
    return 0;
}

int st_pop(Stack *s) {
    assert(s->top > 0);
    return s->data[--s->top];
}

int st_peek(const Stack *s) {
    assert(s->top > 0);
    return s->data[s->top - 1];
}

int st_empty(const Stack *s) { return s->top == 0; }
int st_size(const Stack *s) { return s->top; }

void st_free(Stack *s) {
    free(s->data);
    st_init(s);
}
