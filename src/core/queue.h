/*
 * queue.h - FIFO queue of int on a circular array (fixed capacity).
 *
 * tail = (tail + 1) % cap, so slots freed by q_pop are reused without
 * shifting elements: enqueue and dequeue are both O(1).
 *
 * Used by BFS. Each node is enqueued at most once, so capacity = node count
 * can never overflow.
 */
#ifndef ROUTELENS_QUEUE_H
#define ROUTELENS_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int *data;
    int head;    /* index of the oldest element */
    int tail;    /* index where the next element goes */
    int size;
    int cap;
} Queue;

int  q_init(Queue *q, int cap);        /* cap >= 1. 0 ok, -1 failure */
int  q_push(Queue *q, int x);          /* 0 ok, -1 if full           */
int  q_pop(Queue *q);                  /* oldest element; -1 if empty (so only store x >= 0) */
int  q_empty(const Queue *q);
int  q_size(const Queue *q);
void q_free(Queue *q);

#ifdef __cplusplus
}
#endif
#endif
