#include "../src/core/queue.h"
#include "test_util.h"

int main(void) {
    Queue q;
    CHECK(q_init(&q, 0) == -1);         /* invalid capacity */
    CHECK(q_init(&q, 4) == 0);
    CHECK(q_empty(&q));
    CHECK(q_pop(&q) == -1);             /* pop on empty */

    /* FIFO order */
    for (int i = 1; i <= 4; i++) CHECK(q_push(&q, i * 10) == 0);
    CHECK(q_size(&q) == 4);
    CHECK(q_push(&q, 99) == -1);        /* full */
    CHECK(q_pop(&q) == 10);
    CHECK(q_pop(&q) == 20);

    /* wrap-around: tail passes the end of the array and reuses freed slots */
    CHECK(q_push(&q, 50) == 0);
    CHECK(q_push(&q, 60) == 0);
    CHECK(q.tail < q.head || q.size == q.cap);   /* it did wrap */
    CHECK(q_pop(&q) == 30);
    CHECK(q_pop(&q) == 40);
    CHECK(q_pop(&q) == 50);
    CHECK(q_pop(&q) == 60);
    CHECK(q_empty(&q));

    /* many wraps with a tiny buffer */
    int next_in = 0, next_out = 0, ok = 1;
    for (int round = 0; round < 10000; round++) {
        int k = 1 + round % 4;
        for (int i = 0; i < k; i++) q_push(&q, next_in++);
        for (int i = 0; i < k; i++) if (q_pop(&q) != next_out++) ok = 0;
    }
    CHECK(ok && q_empty(&q));

    q_free(&q);
    CHECK(q.data == NULL);

    TEST_REPORT("test_queue");
}
