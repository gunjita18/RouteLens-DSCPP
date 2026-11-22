# Data structures, algorithms and traffic: questions and answers

Short answers for the map-data and traffic part, with the evidence in this repository.
File names are relative to the repository root.

## Hash table (`src/core/hashtable.c`)

**Why a hash table for OSM IDs?**
OSM IDs are large numbers (for example 2,861,234,567) and the graph needs indices 0..n-1.
A hash table maps one to the other in O(1) average time, so the rest of the code can use plain arrays.
It holds all 32,110 real IDs (`tests/test_hashtable.c`, `tests/test_osm.cpp`).

**How are collisions handled?**
Separate chaining: each bucket is a singly linked list (`src/core/list.c`); a colliding key is added at the head of its bucket's list.
On the real IDs the longest chain is 5, with 65,536 buckets.

**What is the hash function, and why that one?**
Multiplicative (Fibonacci) hashing: `(key * 11400714819323198485) >> (64 - bits)`. The constant is 2^64 divided by the golden ratio.
It needs no division, and it spreads clustered or sequential IDs evenly, because the top bits of the product depend on every bit of the key.

**When does it resize, and why is the bucket count a power of two?**
When `count / buckets` would exceed 0.75, the bucket array doubles and every node is relinked (rehash).
The count is a power of two, 2^bits, because the hash takes the top `bits` bits with a shift; a shift is only a valid bucket index for such sizes.

**Cost?** Insert and lookup O(1) average, O(n) worst case if every key collided. Rehash is O(n) but happens log n times, so inserts are O(1) amortised.

## Dynamic array, linked list, queue

**Why is dynamic-array push O(1) amortised?**
When full, capacity doubles with `realloc`. n pushes copy at most 1 + 2 + 4 + ... + n < 2n elements in total, so each push costs a constant on average.
Test: 1,000,000 pushes, about 17 reallocations.

**Why a circular queue for BFS?**
Enqueue and dequeue are both O(1) and freed slots are reused with `tail = (tail + 1) % cap`, with no shifting.
Each node enters the queue at most once, so a capacity equal to the node count can never overflow.

## BFS graph cleaning (`src/core/bfs.c`, `src/data/osm_loader.cpp`)

**How does the cleaning work?**
Run BFS from a central node along the roads (everything reachable from it), then BFS along reversed roads (everything that can reach it).
Keep the nodes found by both. They are exactly the centre's strongly connected component, so a route exists between any two kept nodes.
Cost: O(V + E) each, using a CSR (compressed adjacency) array built by counting sort.

**Which node is the centre, and what if it is a bad one?**
The node nearest Clock Tower (coordinates from `data/junctions/major_junctions.csv`).
BFS from one node only finds that node's component. If the centre sits on a dead-end stub, the loader tries the next-nearest nodes (at most 20) and keeps the largest result.
It stops once one component has more than half of all nodes, because two disjoint components cannot both be that large. A test covers this fallback.

**Why BFS and not Tarjan's algorithm?**
Tarjan is outside the syllabus, and for one main component BFS gives the same answer. The trade-off: Tarjan finds every component in one pass; BFS finds only the centre's.

**How do you know it is correct?**
- On the real network it keeps 31,802 nodes, the number NetworkX reports as the largest strongly connected component (`data/osm/meta.txt`).
- On 300 random graphs, BFS reachability matches a Warshall transitive closure, a method that shares no code with BFS (`tests/test_bfs.c`).
- Union-find over the raw edges counts 72 weakly connected parts, matching NetworkX; it does not take part in the cleaning, so it is an independent check.

## Union-find (`src/core/dsu.c`)

**How does it work, and what does it cost?**
Each set is a tree; `find` follows parent pointers to the root and, with path compression, points every node on the path straight at the root.
Union by rank attaches the shallower tree under the deeper one, so tree height stays at most log2 n.
Together, m operations cost O(m * alpha(n)), where alpha is the inverse Ackermann function (below 5 for any realistic n).

## Hash table vs binary search (`docs/LOOKUP_EXPERIMENT.md`)

**What did you find?**
On one machine (1,000,000 lookups, median of 5 runs) the hash table was 4 to 9 times faster at every size from 2,000 to 1,000,000 keys, for present and absent keys.
In RouteLens a route needs a few thousand lookups, so the difference is about 0.3 ms against 0.03 ms: it does not change what the user sees.

**What are the weaknesses of the experiment?**
One machine and compiler setting; uniformly random queries (real routes have locality); synthetic keys at the larger sizes.
The first version of the benchmark was flawed (absent keys all beyond the largest key flattered binary search); it was fixed, and the document says so.

## Traffic snapshot (`preprocess/fetch_traffic.py`, `src/core/traffic_reader.c`)

**Why a saved snapshot instead of live calls?**
The demo then works without internet, and stays within TomTom's free quota. The page shows when the data was taken.

**What rules decide which TomTom answers are used?**
- Only trunk, primary and secondary roads are queried (TomTom has little data on small roads).
- 50 m rule: if TomTom's road is more than 50 m from the edge's midpoint, it is a different road, so nothing is recorded (correct matches measured 0-15 m away, wrong ones 300 m and more).
- Confidence below 0.5 is ignored, in the script and again in the C reader.
- Segment reuse: a midpoint within 50 m of an already fetched segment reuses its answer.

**What did the first real run give?**
2,445 candidate edges, 168 requests, 2,047 edges recorded, 143 wrong-road answers. Only 23 distinct TomTom segments cover all 2,047 edges.

**What are the limits?** (say these before the examiner does)
- One TomTom segment can span many kilometres; one speed is applied to hundreds of edges (up to 405 edges for one segment in the first run).
- The first snapshot was taken on a Sunday morning and is 96% low traffic; it says little about congestion.
- Residential roads have no traffic data; they use provisional free-flow speeds (20 km/h and so on), still to be calibrated in the ETA study.
- OSM has only 6 signals in Dehradun, so the documented major junctions are assumed to be signalised and each adds a provisional 30 s wait.

## C++ layer (`src/oop/`)

| Pillar | Where |
|---|---|
| Abstraction | `TrafficProvider` and `Scenario` are abstract: routing code calls `trafficIndex(edge)` and `apply(state)` without knowing the concrete class |
| Encapsulation | `SnapshotProvider` keeps the C snapshot and hash table private; scenario constructors reject factors outside (0, 1], so an existing object is always valid |
| Inheritance | `RainScenario : Scenario`, `SnapshotProvider : TrafficProvider`, `LoadError : std::runtime_error` |
| Polymorphism | virtual `apply()` and `trafficIndex()`: one `std::unique_ptr<Scenario>` behaves as rain, rush hour or an incident (`tests/test_scenarios.cpp`); `operator<<` prints a scenario's name through the base reference |

**How do C and C++ work together?**
The C headers are wrapped in `extern "C"` so C++ can call them directly. C++ classes own the C structures and free them in their destructors (RAII): `rl::IdIndex` owns the hash table and `SnapshotProvider` owns the snapshot.

**Why is the base-class copy constructor deleted?**
To prevent slicing: copying a derived object into a base object would lose the derived part. Polymorphic objects are passed by pointer or reference.

## Memory safety

**How do you know there are no memory leaks?**
Every C test, and the C++ provider and scenario tests, run clean under AddressSanitizer, LeakSanitizer and UBSan.
Valgrind (named in the design) has not been run yet because it was not installed; run `valgrind --leak-check=full ./build/test_hashtable` once it is.
