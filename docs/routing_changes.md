# Gunjita: change log (routing algorithms, scenario mode, evaluation)

Scope from the Phase II report: heap, Dijkstra, A\*, penalty method, Yen's
algorithm, Pareto ranking, scenario backend and the algorithm evaluation. Files
owned by other members were changed only where noted below.

## Phase II

| Report item | Status | Files |
|---|---|---|
| Hand-written binary min-heap | Done earlier (`8cc0b73`) | `src/algorithms/min_heap.hpp`, `tests/test_min_heap.cpp` |
| Dijkstra | Done earlier (`b4e4a64`); now also `dijkstraWithCosts` and per-edge paths | `src/algorithms/dijkstra.{hpp,cpp}` |
| A\* | **Fixed**: previously a zero heuristic (identical to Dijkstra); now straight-line + speed-bound heuristic, tested for equal cost to Dijkstra | `src/algorithms/astar.{hpp,cpp}`, `tests/test_astar_heuristic.cpp` |
| Yen's k-shortest paths | Done (`c26bb62`) | `src/algorithms/yen.{hpp,cpp}`, `tests/test_yen.cpp` |
| Pareto filter and weighted ranking | Done (`c26bb62`) | `src/algorithms/route_ranking.{hpp,cpp}`, `tests/test_route_ranking.cpp` |
| **Penalty method** | **New**: primary alternative-route generator | `src/algorithms/penalty.{hpp,cpp}`, `tests/test_penalty.cpp` |
| **Scenario backend** (block/slow roads, BFS, `/api/scenario`) | **New** | `src/algorithms/scenario.{hpp,cpp}`, `src/server/main.cpp`, `tests/test_scenario.cpp`, `docs/scenario_api.md` |
| **Algorithm evaluation** | **New** | `src/app/eval_main.cpp`, `evaluation/`, `docs/evaluation_phase2.md` |

### Changes to files other members own
- `src/server/osm_route_provider.cpp` (integration): penalty method first, Yen's
  as a top-up, closures supported; `tests/test_route_provider.cpp` added.
- `src/server/request_params.hpp`: `RouteRequest` gained `blockedEdges` and
  `durationFactor` (empty = no scenario). Existing validation is unchanged.
- `src/server/main.cpp`: route JSON output moved into `putRoutes()` (same
  fields as before) and `POST /api/scenario` added.
- `CMakeLists.txt`: new library sources and test targets.

### Results
See `docs/evaluation_phase2.md` (generated; reproduce with
`evaluation/run_evaluation.sh`). Short version: all three searches match
NetworkX on 3,000 queries; A\* is much faster for distance but not for travel
time; the own heap is about 3-4% slower than `std::priority_queue`; the penalty
method returns usefully different routes where Yen's returns near-duplicates.

### Known limits
- Scenario mode uses the penalty method only; Yen's is skipped while closures
  are active.
- A heavily slowed road can be dropped from the alternatives by the 1.5x
  stretch filter instead of being shown with a long ETA.
- Scenarios and the evaluation use free-flow times; live TomTom speeds are not
  combined with either.
- The scenario user interface is not built (assigned to Aayush).

## Phase III (simplified design: C core, C++ OOP layer)

Scope from the Phase III plan: min-heap, stack, Dijkstra, path reconstruction,
penalty method with overlap check and merge sort in C; the `RouteFinder`
hierarchy and `Route` class in C++; a NetworkX re-check of the C version; A\*
and Yen's algorithm out of the build. Files the Phase II table above lists
under `src/algorithms/` (except `scenario.*`) are no longer built; they stay in
Git history as explored work.

| Plan item | Status | Files |
|---|---|---|
| C min-heap, stack, merge sort, Dijkstra (lazy deletion, stack rebuild), penalty method (hash-set overlap check) | On `main` in `cd9655a` (committed by Sneha) | `src/core/{minheap,stack,sort,dijkstra,penalty}.{c,h}`, `tests/test_{minheap,stack,sort,c_dijkstra,c_penalty}.c` |
| `Route` class: private edges and cost, `operator<` (by cost), friend `operator<<` | **New** | `src/oop/route.{hpp,cpp}`, `tests/test_route.cpp` |
| `RouteFinder` (abstract) → `DijkstraRouter` → `PenaltyRouter` | **New** | `src/oop/route_finder.{hpp,cpp}`, `tests/test_route_finder.cpp` |
| Server generates routes through the router classes | **New** | `src/server/osm_route_provider.cpp` |
| Final ranking by weighted score with the C merge sort | **New** (was `std::stable_sort`) | `src/server/osm_route_provider.cpp` |
| Scenario reachability ("still reachable after the closure?") with the C BFS and shared C queue | **New**: `bfs_hops` (was `std::queue` in `bfsReachable`) | `src/core/bfs.{c,h}`, `src/algorithms/scenario.cpp`, `tests/test_bfs_hops.c` |
| NetworkX re-check of the C version | **Done**: 0 mismatches on 3,000 queries | `src/app/eval_main.cpp`, `evaluation/compare_networkx.py`, `docs/evaluation_phase3.md` |
| A\*, Yen's algorithm, Pareto ranking, C++ Dijkstra / penalty / heap out of the build | **Done** | `CMakeLists.txt`; `tests/test_c_routing_real.cpp` no longer uses the C++ Dijkstra |

### Where the four OOP pillars are in the routing layer
- **Encapsulation:** `Route` and `RouteFinder` keep their data private; the
  constructor checks the costs (finite, not negative, one per edge) once, so a
  finder that exists can always search.
- **Abstraction:** the server and the evaluation call `find(src, dst)` on a
  `RouteFinder&` without knowing which search runs.
- **Inheritance:** `PenaltyRouter : public DijkstraRouter : public RouteFinder`;
  the penalty router reuses the stored graph, costs and closures, and its first
  route is exactly the Dijkstra router's route.
- **Polymorphism:** `find()` is virtual (runtime); `Route` has `operator<`,
  `operator==` and `operator<<` (compile time).

### Changes to files other members own
- `src/server/osm_route_provider.cpp` (Aayush's server wiring): routes come from
  a `PenaltyRouter` and a `DijkstraRouter` instead of direct C calls, and the
  ranking uses `merge_sort`. Settings unchanged (k = 5, x1.4, 70 % overlap,
  1.5 stretch). On 80 random real-network requests (normal, rain, rush hour,
  other priorities, closures) all 236 routes are identical before and after.
- `CMakeLists.txt`: new `routelens_routing` library and tests; removed targets
  listed above.

### Results
`docs/evaluation_phase3.md` (generated; reproduce with
`evaluation/run_evaluation.sh`): the C Dijkstra matches NetworkX on 1,000
random pairs for each of the time, distance and balanced profiles (max
relative error 0), about 1.3-1.5 ms per search; the penalty method with the
server settings returns at least 2 usable routes for all 100 pairs tested.

### Known limits
- The server still drops a route costing over 1.5x the cheapest (penalty
  stretch limit) and then filters by distance (1.5x) and road overlap (85 %).
  The Phase III plan describes only the 70 % shared-edge rule; whether to keep
  the stretch limits is a team decision. A heavily slowed road can still be
  dropped instead of shown with a long ETA.
- The search charges a junction wait on every entry into a junction while the
  ETA counts each junction once per route, so the lowest-cost route is not
  always the lowest-ETA route (see `docs/C_ROUTING_CORE.md`).
- `test_c_routing_real` checks validity and that the classes match the C calls;
  shortest-cost correctness on the real network is checked only by the NetworkX
  evaluation, which needs Python with networkx and is not run in CI.
