#include "../src/core/list.h"
#include "test_util.h"

int main(void) {
    Node *head = NULL;
    CHECK(list_length(head) == 0);
    CHECK(list_find(head, 1) == NULL);

    /* insert at head: newest element comes first */
    for (int i = 0; i < 100; i++) {
        Node *h = list_push_front(head, 1000000000000LL + i, i);
        CHECK(h != NULL);
        head = h;
    }
    CHECK(list_length(head) == 100);
    CHECK(head->value == 99);

    for (int i = 0; i < 100; i++) {
        Node *n = list_find(head, 1000000000000LL + i);
        CHECK(n != NULL && n->value == i);
    }
    CHECK(list_find(head, 5) == NULL);
    CHECK(list_find(head, -1) == NULL);

    /* a negative 64-bit key works too */
    head = list_push_front(head, -77LL, 7);
    CHECK(list_find(head, -77LL) != NULL && list_find(head, -77LL)->value == 7);

    list_free(head);
    list_free(NULL);                    /* freeing an empty list is harmless */

    TEST_REPORT("test_list");
}
