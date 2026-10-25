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
