/* Tests for the C trie (src/core/trie.c). Run under Valgrind in CI to show
 * that trie_free releases every node. */
#include <stdio.h>
#include <string.h>

#include "trie.h"

static int checks = 0;
static int failures = 0;

static void check(int condition, const char *message) {
    ++checks;
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

int main(void) {
    Trie t;
    int out[16];
    int n, v;
    const char *names[] = {"Clock Tower", "Clement Town", "Prince Chowk", "ISBT", "Clock"};

    check(trie_init(&t), "init succeeds");
    check(trie_prefix(&t, "", out, 16) == 0, "empty trie has no names");

    for (v = 0; v < 5; ++v) {
        check(trie_insert(&t, names[v], v), "insert succeeds");
    }
    check(t.count == 5, "five names stored");

    /* Exact lookups, any letter case. */
    check(trie_find(&t, "Clock Tower", &v) && v == 0, "find exact name");
    check(trie_find(&t, "clock tower", &v) && v == 0, "find ignores case");
    check(!trie_find(&t, "Clock Towe", &v), "a prefix alone is not a stored name");
    check(!trie_find(&t, "Rajpur Road", &v), "unknown name is not found");

    /* Prefix search: "cl" gives Clement Town, Clock, Clock Tower (alphabetical,
     * a shorter name before the longer names that extend it). */
    n = trie_prefix(&t, "Cl", out, 16);
    check(n == 3, "three names start with Cl");
    check(n == 3 && out[0] == 1 && out[1] == 4 && out[2] == 0, "alphabetical order");
    check(trie_prefix(&t, "CLOCK", out, 16) == 2, "upper-case prefix works");
    check(trie_prefix(&t, "x", out, 16) == 0, "no name starts with x");
    check(trie_prefix(&t, "", out, 16) == 5, "empty prefix lists every name");

    /* The limit is respected. */
    check(trie_prefix(&t, "c", out, 2) == 2, "at most max results");
    check(trie_prefix(&t, "c", out, 0) == 0, "max 0 gives nothing");

    /* Re-inserting a name replaces its value and does not count it twice. */
    check(trie_insert(&t, "isbt", 7), "re-insert succeeds");
    check(trie_find(&t, "ISBT", &v) && v == 7, "value replaced");
    check(t.count == 5, "count unchanged by re-insert");

    /* Bad input is rejected and leaves the trie unchanged. */
    check(!trie_insert(&t, "", 1), "empty name rejected");
    check(!trie_insert(&t, "Caf\xc3\xa9", 1), "non-ASCII name rejected");
    check(!trie_insert(&t, "Paltan", -1), "negative value rejected");
    check(t.count == 5, "rejected inserts change nothing");
    check(trie_prefix(&t, "Caf", out, 16) == 0, "rejected name left no partial path visible");

    /* Many names: all are found again. */
    {
        char buf[32];
        int i, ok = 1;
        for (i = 0; i < 1000; ++i) {
            snprintf(buf, sizeof buf, "Place %d", i);
            ok = ok && trie_insert(&t, buf, 100 + i);
        }
        for (i = 0; i < 1000 && ok; ++i) {
            snprintf(buf, sizeof buf, "place %d", i);
            ok = trie_find(&t, buf, &v) && v == 100 + i;
        }
        check(ok, "1000 extra names stored and found");
        check(t.count == 1005, "count is 1005");
        check(trie_prefix(&t, "place 99", out, 16) == 11, "place 99 and place 990..999");
    }

    trie_free(&t);
    check(t.root == NULL && t.count == 0, "free resets the trie");

    printf("Trie tests: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
