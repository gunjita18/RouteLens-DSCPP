#include "../src/core/dynarray.h"
#include "test_util.h"

int main(void) {
    IntArray a;
    ia_init(&a);
    CHECK(a.size == 0 && a.cap == 0 && a.data == NULL);

    /* growth past the initial capacity keeps every value (realloc copies) */
    const int N = 1000000;
    int cap_changes = 0, last_cap = a.cap;
    for (int i = 0; i < N; i++) {
        CHECK(ia_push(&a, i * 3) == 0);
        if (a.cap != last_cap) { cap_changes++; last_cap = a.cap; }
    }
    CHECK(a.size == N);
    CHECK(a.cap >= N);

    int all_ok = 1;
    for (int i = 0; i < N; i++)
        if (ia_get(&a, i) != i * 3) { all_ok = 0; break; }
    CHECK(all_ok);

    /* doubling => about log2(N) reallocations, not N (amortised O(1)) */
    CHECK(cap_changes <= 20);

    ia_set(&a, 5, -42);
    CHECK(ia_get(&a, 5) == -42);

    /* clear keeps the buffer for reuse */
    int *buf = a.data;
    ia_clear(&a);
    CHECK(a.size == 0 && a.data == buf);
    ia_push(&a, 7);
    CHECK(ia_get(&a, 0) == 7);

    ia_free(&a);
    CHECK(a.data == NULL && a.size == 0 && a.cap == 0);
    ia_free(&a);                        /* double free must be harmless */

    TEST_REPORT("test_dynarray");
}
