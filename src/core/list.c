#include "list.h"

#include <stdlib.h>

Node *list_push_front(Node *head, long long key, int value) {
    Node *n = (Node *)malloc(sizeof(Node));
    if (n == NULL) return NULL;
    n->key = key;
    n->value = value;
    n->next = head;
    return n;
}

Node *list_find(Node *head, long long key) {
    for (Node *p = head; p != NULL; p = p->next)
        if (p->key == key) return p;
    return NULL;
}

int list_length(const Node *head) {
    int n = 0;
    for (const Node *p = head; p != NULL; p = p->next) n++;
    return n;
}

void list_free(Node *head) {
    while (head != NULL) {
        Node *next = head->next;     /* read before freeing */
        free(head);
        head = next;
    }
}
