/* Tiny test helper shared by the C structure tests (works even with -DNDEBUG). */
#ifndef ROUTELENS_TEST_UTIL_H
#define ROUTELENS_TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failed++;                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

#define TEST_REPORT(name)                                                    \
    do {                                                                     \
        printf("%s: %d checks, %d failed\n", name, g_checks, g_failed);      \
        return g_failed == 0 ? 0 : 1;                                        \
    } while (0)

#endif
