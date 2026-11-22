# Lookup experiment: hash table vs binary search

**Question.** The traffic snapshot is looked up by edge id. Is a hash table (average O(1))
measurably faster than a sorted array searched with binary search (O(log n))?

**Answer.** Yes. On this machine the hash table was 4 to 9 times faster at every size
tested, for both present and absent keys. In RouteLens the difference does not change
what a user sees (see "What it means for RouteLens").

## Method

- Program: `src/app/lookup_benchmark.c` (target `routelens_lookup_benchmark`, always built with `-O2`).
- Both structures hold the same keys: `src/core/hashtable.c` and a `SortedKeys` array built with a
  stable merge sort and searched with `sk_find` (`src/core/search.c`).
- Datasets: the real snapshot (2,047 edges, `data/traffic/traffic.csv`) and synthetic sets of
  2,000, 32,000 (about the size of the road network's node count) and 1,000,000 distinct, scattered keys.
- Two query sets of 1,000,000 lookups each: **hits** (random existing keys) and **misses**
  (random absent keys inside the key range, so binary search takes a different path each time).
- Each timing is repeated 5 times and the **median** is reported; a warm-up run comes first.
- Before timing, every query is answered by both structures and the answers are compared. Any
  difference stops the program with an error. The lookup loops accumulate a result so the compiler cannot remove them.

Reproduce (repository root, after building):

```bash
./build/routelens_lookup_benchmark                 # --lookups 1000000 --runs 5 by default
```

## Results

Nanoseconds per lookup, one run (the other two runs of three agreed within about 5%).
Machine: AMD Ryzen 7 7445HS, WSL2 Ubuntu, GCC `-O2`.

| Dataset | n | Hash, hit | Binary, hit | Binary / hash | Hash, miss | Binary, miss | Binary / hash |
|---|---:|---:|---:|---:|---:|---:|---:|
| real snapshot | 2,047 | 10.1 | 85.5 | 8.5x | 13.7 | 64.2 | 4.7x |
| synthetic | 2,000 | 16.1 | 86.2 | 5.4x | 11.5 | 84.4 | 7.4x |
| synthetic | 32,000 | 21.0 | 123.9 | 5.9x | 17.0 | 121.3 | 7.1x |
| synthetic | 1,000,000 | 67.1 | 279.6 | 4.2x | 62.1 | 273.3 | 4.4x |

Building the structures (1,000,000 keys): hash table about 130 ms, sorted array about 75 ms.
Longest hash chain in every dataset: 2 to 4 entries.

## Reading the results

- **Binary search cost grows with log n.** n grows 500 times (2,000 to 1,000,000); the cost grows
  about 3.2 times, while log2 n grows from 11 to 20 (1.8 times). The extra growth comes from
  memory: a large array no longer fits in the CPU cache.
- **The hash table is not exactly flat.** It rises from about 16 to 67 ns for the same reason (cache
  misses on a large table, plus following chain pointers through separately allocated nodes), not
  because chains get longer: the longest chain stays at 2 to 4.
- **The sorted array builds faster.** Inserting into the hash table allocates one node per key and
  rehashes as the table grows. Sorting is a single pass of merging in two arrays.
- **Why not always use a hash table?** It is unordered. A sorted array also answers "nearest key",
  "all keys between a and b" and is smaller in memory. For exact-match lookups by id, as here,
  the hash table is the better fit.

## What it means for RouteLens

A route request looks up a few thousand edges at most. At 86 ns per binary-search lookup that is
about 0.3 ms; with the hash table it is about 0.03 ms. Both are far below the time of the routing
search itself, so this is a data-structure comparison for the report, not a performance fix.

## Limits of this experiment

- One machine, one compiler setting, a few runs. Absolute times will differ elsewhere; the ratio and the trend are the result.
- Queries are uniformly random. Real route lookups follow roads, so they have more locality and would be faster for both structures.
- The synthetic keys are scattered integers, not real edge ids. The real snapshot row confirms the same pattern at its own size.
- The first version of this benchmark drew absent keys from beyond the largest key. Binary search then followed the same path every time and looked much better (a tie at 1,000,000 keys). That was a flaw in the test, not a property of binary search; the version here uses absent keys inside the key range.
