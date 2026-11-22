/*
 * list.h - singly linked list of (key, value) pairs.
 *
 * insert at head O(1), search O(n).
 * Used as the bucket chain of the hash table (separate chaining).
 *
 * The list is represented by its head pointer (NULL = empty); functions that
 * change the head return the new head.
 */
#ifndef ROUTELENS_LIST_H
#define ROUTELENS_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Node {
    long long key;
    int value;
    struct Node *next;
} Node;

/* Returns the new head, or NULL on out-of-memory (the old list is untouched,
 * so check for NULL before overwriting your head pointer). */
Node *list_push_front(Node *head, long long key, int value);
Node *list_find(Node *head, long long key);     /* NULL if absent */
int   list_length(const Node *head);
void  list_free(Node *head);

#ifdef __cplusplus
}
#endif
#endif
