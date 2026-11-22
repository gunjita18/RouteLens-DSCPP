#include "sort.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int merge_sort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
    if (base == NULL || cmp == NULL || size == 0) return -1;
    if (n > SIZE_MAX / size) return -1;                    /* n * size would overflow */
    if (n < 2) return 0;

    unsigned char *a = (unsigned char *)base;
    unsigned char *tmp = (unsigned char *)malloc(n * size);
    if (tmp == NULL) return -1;

    unsigned char *src = a, *dst = tmp;
    for (size_t width = 1; width < n; width *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * width) {
            size_t mid = lo + width < n ? lo + width : n;
            size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) {
                /* take from the right run only if it is strictly smaller: ties go left, so it is stable */
                if (cmp(src + j * size, src + i * size) < 0) memcpy(dst + k++ * size, src + j++ * size, size);
                else memcpy(dst + k++ * size, src + i++ * size, size);
            }
            while (i < mid) memcpy(dst + k++ * size, src + i++ * size, size);
            while (j < hi) memcpy(dst + k++ * size, src + j++ * size, size);
        }
        unsigned char *swap = src; src = dst; dst = swap;  /* the merged result is now in src */
    }
    if (src != a) memcpy(a, src, n * size);
    free(tmp);
    return 0;
}
