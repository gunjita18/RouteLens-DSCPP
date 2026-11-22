#include "hashtable.h"

#include <stdint.h>
#include <stdlib.h>

#define HT_MULTIPLIER 11400714819323198485ULL   /* 2^64 / golden ratio */
#define HT_MAX_BITS 30

static int ht_index(long long key, int bits) {
    uint64_t h = (uint64_t)key * HT_MULTIPLIER;   /* unsigned: wraps, no UB */
    return (int)(h >> (64 - bits));
}

int ht_init(HashTable *t, int nbuckets) {
    int bits = 1;
    while (bits < HT_MAX_BITS && (1 << bits) < nbuckets) bits++;
    t->buckets = (Node **)calloc((size_t)1 << bits, sizeof(Node *));
    if (t->buckets == NULL) {
        t->nbuckets = t->count = t->bits = 0;
        return -1;
    }
    t->bits = bits;
    t->nbuckets = 1 << bits;
    t->count = 0;
    return 0;
}

/* Doubles the bucket array and relinks every node (no node is reallocated). */
static int ht_rehash(HashTable *t) {
    if (t->bits >= HT_MAX_BITS) return 0;           /* stop growing; still correct */
    int new_bits = t->bits + 1;
    Node **nb = (Node **)calloc((size_t)1 << new_bits, sizeof(Node *));
    if (nb == NULL) return -1;
    for (int i = 0; i < t->nbuckets; i++) {
        Node *p = t->buckets[i];
        while (p != NULL) {
            Node *next = p->next;
            int j = ht_index(p->key, new_bits);
            p->next = nb[j];
            nb[j] = p;
            p = next;
        }
    }
    free(t->buckets);
    t->buckets = nb;
    t->bits = new_bits;
    t->nbuckets = 1 << new_bits;
    return 0;
}

int ht_put(HashTable *t, long long key, int value) {
    int i = ht_index(key, t->bits);
    Node *hit = list_find(t->buckets[i], key);
    if (hit != NULL) {
        hit->value = value;
        return 0;
    }
    /* load factor after insertion would be (count + 1) / nbuckets > 0.75 */
    if (4 * (t->count + 1) > 3 * t->nbuckets) {
        if (ht_rehash(t) != 0) return -1;
        i = ht_index(key, t->bits);
    }
    Node *head = list_push_front(t->buckets[i], key, value);
    if (head == NULL) return -1;
    t->buckets[i] = head;
    t->count++;
    return 0;
}

int ht_get(const HashTable *t, long long key, int *value) {
    Node *n = list_find(t->buckets[ht_index(key, t->bits)], key);
    if (n == NULL) return 0;
    if (value != NULL) *value = n->value;
    return 1;
}

int ht_contains(const HashTable *t, long long key) {
    return ht_get(t, key, NULL);
}

void ht_free(HashTable *t) {
    if (t->buckets != NULL)
        for (int i = 0; i < t->nbuckets; i++) list_free(t->buckets[i]);
    free(t->buckets);
    t->buckets = NULL;
    t->nbuckets = t->count = t->bits = 0;
}

int ht_max_chain(const HashTable *t) {
    int best = 0;
    for (int i = 0; i < t->nbuckets; i++) {
        int len = list_length(t->buckets[i]);
        if (len > best) best = len;
    }
    return best;
}
