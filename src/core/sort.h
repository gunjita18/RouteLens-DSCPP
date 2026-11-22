/*
 * sort.h - merge sort for arrays of any element type.
 *
 * Always O(n log n) (no bad input, unlike quicksort) and STABLE: elements that
 * compare equal keep their original order, so ranking routes by score and then
 * sorting by name keeps ties in a predictable order. Needs O(n) extra memory.
 * Iterative (bottom-up): runs of width 1, 2, 4, ... are merged, so there is no
 * recursion depth to worry about.
 *
 * Used to rank routes by score and to build the sorted array for binary search.
 */
#ifndef ROUTELENS_SORT_H
#define ROUTELENS_SORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sorts base[0..n-1], elements of `size` bytes, by cmp (negative / zero / positive like qsort).
 * Returns 0 on success, -1 on invalid arguments (base or cmp NULL, size 0, n * size overflows)
 * or out of memory; the array is then unchanged. n of 0 or 1 is a no-op. */
int merge_sort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));

#ifdef __cplusplus
}
#endif
#endif
