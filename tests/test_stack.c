#include "../src/core/stack.h"
#include "test_util.h"

int main(void) {
    Stack s;
    st_init(&s);
    CHECK(st_empty(&s) && st_size(&s) == 0 && s.data == NULL);

    /* LIFO order */
    for (int i = 1; i <= 5; i++) CHECK(st_push(&s, i * 10) == 0);
    CHECK(st_size(&s) == 5 && st_peek(&s) == 50);
    CHECK(st_pop(&s) == 50 && st_pop(&s) == 40);
    CHECK(st_push(&s, 99) == 0);
    CHECK(st_pop(&s) == 99 && st_pop(&s) == 30 && st_pop(&s) == 20 && st_pop(&s) == 10);
    CHECK(st_empty(&s));

    /* growth past the initial capacity keeps every value; negative values are fine */
    const int N = 1000000;
    for (int i = 0; i < N; i++) CHECK(st_push(&s, i - 500000) == 0);
    CHECK(st_size(&s) == N && s.cap >= N);
    int ok = 1;
    for (int i = N - 1; i >= 0; i--)
        if (st_pop(&s) != i - 500000) { ok = 0; break; }
    CHECK(ok && st_empty(&s));

    /* the use in Dijkstra: reverse an order by pushing and popping */
    int in[] = { 7, 8, 9, 10 }, out[4];
    for (int i = 0; i < 4; i++) st_push(&s, in[i]);
    for (int i = 0; i < 4; i++) out[i] = st_pop(&s);
    CHECK(out[0] == 10 && out[1] == 9 && out[2] == 8 && out[3] == 7);

    st_free(&s);
    CHECK(s.data == NULL && s.top == 0 && s.cap == 0);
    st_free(&s);                                        /* double free is harmless */
    TEST_REPORT("test_stack");
}
