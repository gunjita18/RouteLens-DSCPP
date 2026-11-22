/*
 * stack.h - LIFO stack of int on a growable array.
 *
 * push and pop are O(1) (push amortised, as for the dynamic array). Dijkstra stores
 * only each node's parent edge, so the route comes out backwards (destination to
 * start); pushing it on a stack and popping gives it in travelling order.
 */
#ifndef ROUTELENS_STACK_H
#define ROUTELENS_STACK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int *data;
    int top;      /* number of elements; the next free slot */
    int cap;
} Stack;

void st_init(Stack *s);                 /* empty, no allocation yet */
int  st_push(Stack *s, int x);          /* 0 ok, -1 out of memory */
int  st_pop(Stack *s);                  /* top element; the stack must not be empty */
int  st_peek(const Stack *s);           /* top element without removing it; not empty */
int  st_empty(const Stack *s);
int  st_size(const Stack *s);
void st_free(Stack *s);                 /* releases the buffer, re-inits */

#ifdef __cplusplus
}
#endif
#endif
