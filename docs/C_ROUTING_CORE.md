# C routing core: min-heap, stack, merge sort, Dijkstra, penalty method

Hand-written C, as the simplified Phase III design asks (`src/core/`). Each piece has its own
test. **The server uses it:** `OsmRouteProvider` generates routes with `penalty_routes` and
`dijkstra_path` (see "In the server"). The earlier C++ implementations in `src/algorithms/`
(Dijkstra, penalty method, Yen, A*) are kept with their tests but the server no longer calls them.

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
- **Real network** (`test_c_routing_real`, 31,802 nodes and 72,626 edges): the C Dijkstra and the
  existing C++ `dijkstraWithCosts` find the same cost on **1,000 random pairs with time costs and
  200 with distance costs**; the penalty method returned three valid, different routes for every one
  of 100 random pairs, the first equal to the cheapest.
- **Memory:** all of these tests run clean under Valgrind (`scripts/check_memory.sh`).

`graph.c` moved into the C library target so these plain-C tests can build graphs.

## In the server (`src/server/osm_route_provider.cpp`)

Route generation per request:

1. Edge costs for the user's priorities (including junction waits and measured traffic).
2. `penalty_routes`: up to 5 routes, factor 1.4, a route dropped if more than 70 % of its edges
   are shared with an earlier one or if it costs over 1.5 times the cheapest.
3. `dijkstra_path` on time-only costs: the fastest route, added if the penalty method did not
   already return it.
4. As before: routes over 1.5 times the shortest distance and routes sharing 85 % or more of their
   roads with an accepted route are dropped, at most three are kept, and they are ranked by the
   weighted score of time, distance, junctions and traffic.

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

## Not done

- A re-check against NetworkX (the plan's version) was not run; the check above is against the C++
  implementation on the same costs.
- Timing in the real-network test shows the C version faster in total on this machine (about
  3.5 s against 8.1 s for 1,000 pairs, one run); this is not a controlled benchmark and the C
  version stops at the destination while the comparison runs share no other setting.

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
