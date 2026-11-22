#include "trie.h"

#include <stdlib.h>

/* Lower-case for ASCII letters; anything else is unchanged. */
static int to_slot(char c) {
    unsigned char u = (unsigned char)c;
    if (u >= 'A' && u <= 'Z') {
        u = (unsigned char)(u - 'A' + 'a');
    }
    return (int)u;
}

static TrieNode *new_node(void) {
    /* calloc sets every child pointer to NULL. */
    TrieNode *n = (TrieNode *)calloc(1, sizeof(TrieNode));
    if (n != NULL) {
        n->value = -1;
    }
    return n;
}

int trie_init(Trie *t) {
    t->count = 0;
    t->root = new_node();
    return t->root != NULL;
}

int trie_insert(Trie *t, const char *name, int value) {
    TrieNode *cur = t->root;
    const char *p;
    if (cur == NULL || name == NULL || name[0] == '\0' || value < 0) {
        return 0;
    }
    /* Check the whole name first, so a bad name leaves the trie unchanged. */
    for (p = name; *p != '\0'; ++p) {
        if (to_slot(*p) >= TRIE_ALPHABET) {
            return 0;
        }
    }
    for (p = name; *p != '\0'; ++p) {
        int s = to_slot(*p);
        if (cur->child[s] == NULL) {
            cur->child[s] = new_node();
            if (cur->child[s] == NULL) {
                return 0;
            }
        }
        cur = cur->child[s];
    }
    if (cur->value < 0) {
        t->count++;
    }
    cur->value = value;
    return 1;
}

/* Follows `text` from the root; NULL if some character has no child. */
static const TrieNode *walk(const Trie *t, const char *text) {
    const TrieNode *cur = t->root;
    for (; cur != NULL && *text != '\0'; ++text) {
        int s = to_slot(*text);
        if (s >= TRIE_ALPHABET) {
            return NULL;
        }
        cur = cur->child[s];
    }
    return cur;
}

int trie_find(const Trie *t, const char *name, int *value) {
    const TrieNode *n = walk(t, name);
    if (n == NULL || n->value < 0) {
        return 0;
    }
    *value = n->value;
    return 1;
}

/*
 * Depth-first walk below `n`, children in slot order (so alphabetical).
 * A node's own name comes before longer names that extend it.
 */
static void collect(const TrieNode *n, int *out, int max, int *written) {
    int s;
    if (*written >= max) {
        return;
    }
    if (n->value >= 0) {
        out[(*written)++] = n->value;
    }
    for (s = 0; s < TRIE_ALPHABET && *written < max; ++s) {
        if (n->child[s] != NULL) {
            collect(n->child[s], out, max, written);
        }
    }
}

int trie_prefix(const Trie *t, const char *prefix, int *out, int max) {
    int written = 0;
    const TrieNode *start;
    if (prefix == NULL || out == NULL || max <= 0) {
        return 0;
    }
    start = walk(t, prefix);
    if (start != NULL) {
        collect(start, out, max, &written);
    }
    return written;
}

static void free_node(TrieNode *n) {
    int s;
    if (n == NULL) {
        return;
    }
    for (s = 0; s < TRIE_ALPHABET; ++s) {
        free_node(n->child[s]);
    }
    free(n);
}

void trie_free(Trie *t) {
    free_node(t->root);
    t->root = NULL;
    t->count = 0;
}
