# C routing core: min-heap, stack, merge sort, Dijkstra, penalty method

Hand-written C, as the simplified Phase III design asks (`src/core/`). Each piece has its own
test. **The server uses it** through the C++ router classes (see "C++ layer" and "In the
server"). The earlier C++ implementations (Dijkstra, penalty method, heap, Yen, A*, Pareto
ranking) were removed from the build in Phase III and are kept only in Git history.

| File | What | Cost |
|---|---|---|
| `minheap.c` | Binary min-heap of (key, node); array layout, children of `i` are `2i+1`, `2i+2` | push, pop O(log n); peek O(1) |
| `stack.c` | LIFO stack of `int` on a growable array | push, pop O(1) |
| `sort.c` | `merge_sort`: stable, iterative (bottom-up), any element type | O(n log n), O(n) extra memory |
| `dijkstra.c` | `dijkstra_path`: cheapest route over per-edge costs | O((V + E) log V) |
| `penalty.c` | `penalty_routes`: up to k alternative routes | at most 5k Dijkstra runs |

## Dijkstra (`dijkstra.c`)

- Priority queue: the min-heap, with **lazy deletion**. A node is pushed again with the smaller
  distance instead of lowering its key; when an outdated entry is popped (`key > dist[node]`) it
  is skipped. The search stops when the destination is popped, because its cost is then final.
- Costs are given per edge (`cost[e]`), so the same graph is searched under different priorities,
  scenarios, or penalised costs. Costs must be finite and **not negative**; a negative cost would
  break the assumption that a settled node can never get cheaper, so it is rejected (`-1`).
- Route rebuilding: only each node's parent *edge* is stored. Walking the parents from the
  destination gives the route backwards; the edges are pushed on the **stack** and popped into the
  result, which puts them in travelling order. Edge indices (not node pairs) tell parallel edges apart.
- `blocked[e]` excludes closed edges. Source equal to destination gives an empty route of cost 0.

## Penalty method (`penalty.c`)

1. Cheapest route with Dijkstra (the first result).
2. Multiply the cost of every edge on it by 1.4 and search again; the next route tends to use
   other roads. Repeat. The edges of a route are penalised even when the route is then dropped,
   so the search keeps moving away.
3. Overlap check: the edges of each accepted route go in a **hash set** (`hashtable.c`). A new
   route is dropped when more than 70 % of **its own** edges are in an accepted route's set.
   An optional stretch limit drops routes costing more than a multiple of the cheapest.
4. The cost reported for every route is the real cost (original, unpenalised costs).

Defaults: `k = 3`, factor 1.4, overlap 0.7, no stretch limit, at most `5k` searches.

## How it was checked

- **Heap:** 1,000,000 random keys pop in the same order as sorting them with `qsort`; the heap
  property holds after pushes and pops; interleaved operations match a plain-array model.
- **Merge sort:** same output as `qsort` for 17 sizes (including 0, 1 and powers of two +- 1);
  stable (equal scores keep their order); refuses invalid arguments and size overflow.
- **Dijkstra:** same cost as a Bellman-Ford reference on 400 random graphs (with zero costs,
  parallel edges and blocked edges); every returned route is a valid chain of edges whose costs add
  up to the reported cost; a 200,000-node chain (no recursion anywhere).
- **Against NetworkX** (`evaluation/run_evaluation.sh`, report in `docs/evaluation_phase3.md`):
  on the real network (31,802 nodes, 72,626 edges) the C Dijkstra, called through
  `DijkstraRouter`, finds the same cost as NetworkX on **1,000 random pairs for each of the time,
  distance and balanced profiles** (0 mismatches, max relative error 0).
- **Real network** (`test_c_routing_real`, runs in CI): every C route on 1,000 random pairs with
  time costs and 200 with distance costs is a valid chain of edges whose costs add up;
  `DijkstraRouter` and `PenaltyRouter` return exactly the routes of the C calls; the penalty method
  returned valid, different routes for each of 100 random pairs, the first equal to the cheapest.
  (Before Phase III this test compared against the C++ Dijkstra, which agreed on all pairs.)
- **Memory:** all of these tests run clean under Valgrind (`scripts/check_memory.sh`).

`graph.c` moved into the C library target so these plain-C tests can build graphs.

## C++ layer (`src/oop/route.hpp`, `src/oop/route_finder.hpp`)

| Class | What | OOP point |
|---|---|---|
| `Route` | Edge indices and cost of one route, copied out of a C `PathResult` | private data, `operator<` (by cost), `operator==`, friend `operator<<` |
| `RouteFinder` | Abstract base: `virtual std::vector<Route> find(int src, int dst) const = 0` | abstraction; copies and checks costs and closures once |
| `DijkstraRouter : RouteFinder` | Calls `dijkstra_path`; at most one route | inheritance, `override` |
| `PenaltyRouter : DijkstraRouter` | Calls `penalty_routes`; first route equals `DijkstraRouter`'s | inheritance of a concrete class; runtime polymorphism through `RouteFinder&` |

C results are freed by small RAII guards, so no C memory leaks if an exception is thrown. A bad
node gives `std::invalid_argument`; invalid costs or options are refused in the constructor.

## In the server (`src/server/osm_route_provider.cpp`)

Route generation per request:

1. Edge costs for the user's priorities (including junction waits and measured traffic).
2. `PenaltyRouter` (`penalty_routes`): up to 5 routes, factor 1.4, a route dropped if more than
   70 % of its edges are shared with an earlier one or if it costs over 1.5 times the cheapest.
3. `DijkstraRouter` (`dijkstra_path`) on time-only costs: the fastest route, added if the penalty
   method did not already return it.
4. As before: routes over 1.5 times the shortest distance and routes sharing 85 % or more of their
   roads with an accepted route are dropped, and at most three are kept.
5. Ranking by the weighted score of time, distance, junctions and traffic with the C
   **`merge_sort`** on plain (score, position) pairs; stable, so equal scores keep their order.

**What changed in behaviour.** The earlier code used the C++ penalty method (factor 1.5) and, when no
closure was active, topped the candidates up with Yen's k-shortest paths. Yen's algorithm is outside
the syllabus and is no longer called. To see the effect, the server before and after the change was
asked the same 120 questions (40 random pairs of major junctions, 1.5 to 9 km apart, in the normal,
rain and rush-hour scenarios; 0 errors on either side):

| Measure | Result |
|---|---|
| Routes per question | 2.97 before, 2.97 after (two questions got fewer, two more) |
| Fastest route's ETA | identical in 119 of 120; in one (Globe Chowk to Ajanta Chowk, normal) 6.8 % slower |
| Recommended route's ETA | identical in 113 of 120; mean change -0.3 %; individual changes from 27 % faster to 13 % slower |

So the change is small on average and not an improvement everywhere: with Yen's extra candidates gone,
a few questions lose a slightly better route and a few find a better one. The likely reason for the one
slower fastest route (not verified) is that the search charges a junction wait on every entry while the
ETA counts each junction once per route, so the route with the lowest ETA is not always the one with
the lowest search cost; Yen's extra candidates sometimes found it. Asking for 8 penalty routes instead of
5 did not change this.

## Limits

- The NetworkX check needs Python with networkx and is run by hand (`evaluation/run_evaluation.sh`),
  not in CI.
- Run times in `docs/evaluation_phase3.md` (about 1.3-1.5 ms per search) are from one machine and
  one run; they are not a controlled benchmark.

## Viva questions

**Why a min-heap in Dijkstra?** It returns the nearest unsettled node in O(log n), which gives
O((V + E) log V) overall; a plain array would make every pop O(V).

**What is lazy deletion?** Instead of lowering the key of a node already in the heap, push the node
again with the smaller distance and skip outdated entries when they are popped.

**Why can costs not be negative?** Dijkstra assumes a node that has been settled can never get
cheaper; a negative edge later on the route would break that, and the result would be wrong.

**How is the route rebuilt?** Follow the parent edges from the destination to the start, push them on
a stack, then pop them: the stack reverses the order.

**What is the penalty method?** Raise the cost of the roads of the routes already found (x1.4) and
search again, dropping routes that share more than 70 % of their edges with an earlier one.

**Why merge sort?** Always O(n log n) and stable (equal scores keep their order).

**Why sort (score, position) pairs and not the routes?** `merge_sort` is C: it copies elements byte
by byte with `memcpy`. A C++ object holding a `std::string` or `std::vector` must not be copied that
way, so the plain pairs are sorted and the routes are then taken in that order.

**Where is inheritance in routing?** `PenaltyRouter : DijkstraRouter : RouteFinder`. The penalty
router reuses the graph, costs and closures stored by its parents, and its first route is the
Dijkstra router's route.

**Where is polymorphism?** The server passes either router to one helper taking a `RouteFinder&`
and calls `find()`; the virtual call runs the right search. `Route` overloads `operator<` and
`operator<<` (compile-time polymorphism).

**How do C and C++ work together here?** The C headers have `extern "C"` guards; the router classes
call `dijkstra_path` and `penalty_routes`, copy the result into `Route` objects and free the C memory
in a destructor (RAII), even if an exception is thrown.
