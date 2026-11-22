#ifndef ROUTELENS_TRIE_H
#define ROUTELENS_TRIE_H

/*
 * Trie (prefix tree) for place-name autocomplete, hand-written in C.
 *
 * Each node has one child slot per ASCII character (128). A name is stored by
 * walking from the root one character at a time, creating missing nodes; the
 * node reached by the last character remembers the name's value (for
 * RouteLens: the index of the junction). Letters are stored in lower case, so
 * searching "cl", "Cl" or "CL" gives the same answers.
 *
 * Costs (L = length of the text):
 *   trie_insert        O(L)
 *   trie_prefix        O(L + size of the part of the tree below the prefix)
 *
 * Example: after inserting "Clock Tower" (0) and "Clement Town" (1),
 * trie_prefix(t, "cl", out, 10) writes {1, 0} (alphabetical: "cle" < "clo").
 */

#ifdef __cplusplus
extern "C" {
#endif

#define TRIE_ALPHABET 128

typedef struct TrieNode {
    struct TrieNode *child[TRIE_ALPHABET];
    int value; /* value of the name ending here, or -1 if no name ends here */
} TrieNode;

typedef struct {
    TrieNode *root;
    int count; /* number of names stored */
} Trie;

/* Returns 1 on success, 0 if out of memory. */
int trie_init(Trie *t);

/*
 * Stores `name` with `value` (value must be >= 0). Inserting the same name
 * again replaces its value. Returns 1 on success, 0 if the name is empty,
 * contains a non-ASCII character, or memory runs out.
 */
int trie_insert(Trie *t, const char *name, int value);

/* Returns 1 and sets *value if exactly `name` is stored, otherwise 0. */
int trie_find(const Trie *t, const char *name, int *value);

/*
 * Writes the values of up to `max` names that start with `prefix` into
 * out[0..], in alphabetical order of the (lower-case) names. Returns how many
 * were written. An empty prefix matches every name.
 */
int trie_prefix(const Trie *t, const char *prefix, int *out, int max);

/* Frees every node. The trie can be initialised again afterwards. */
void trie_free(Trie *t);

#ifdef __cplusplus
}
#endif

#endif
