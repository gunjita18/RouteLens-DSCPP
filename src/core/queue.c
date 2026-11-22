#include "queue.h"

#include <stdlib.h>

int q_init(Queue *q, int cap) {
    q->data = NULL;
    q->head = q->tail = q->size = q->cap = 0;
    if (cap < 1) return -1;
    q->data = (int *)malloc((size_t)cap * sizeof(int));
    if (q->data == NULL) return -1;
    q->cap = cap;
    return 0;
}

int q_push(Queue *q, int x) {
    if (q->size == q->cap) return -1;
    q->data[q->tail] = x;
    q->tail = (q->tail + 1) % q->cap;
    q->size++;
    return 0;
}

int q_pop(Queue *q) {
    if (q->size == 0) return -1;
    int x = q->data[q->head];
    q->head = (q->head + 1) % q->cap;
    q->size--;
    return x;
}

int q_empty(const Queue *q) {
    return q->size == 0;
}

int q_size(const Queue *q) {
    return q->size;
}

void q_free(Queue *q) {
    free(q->data);
    q->data = NULL;
    q->head = q->tail = q->size = q->cap = 0;
}
