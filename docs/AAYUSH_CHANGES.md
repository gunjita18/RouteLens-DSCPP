# Aayush: change log (server, website, supporting data structures)

Scope is taken from the team plan: *"Aayush: server, website and supporting data
structures, ~35%"*. Each row cites the plan item it satisfies. Files owned by
other members were **not modified** (see the last section).

## Phase II (Build)

| Plan item | Status | Files |
|---|---|---|
| KD-tree for nearest road to a clicked point | Already done, unchanged | `src/spatial/kdtree.{hpp,cpp}`, `tests/test_kdtree.cpp` |
| LRU cache (hash map + doubly linked list) for traffic data | Already done, unchanged | `src/traffic/lru_cache.hpp`, `tests/test_lru_cache.cpp` |
| Queue-based rate limiter for API calls | Already done, unchanged | `src/traffic/rate_limiter.hpp`, `tests/test_rate_limiter.cpp` |
| Crow server + `/api/health` | Done | `src/server/main.cpp` |
| Crow server + `/api/traffic/status` | **New** | `src/server/main.cpp` |
| Crow server + `/api/routes` | **New**: validates input; returns real routes once a `RouteProvider` is connected, otherwise `501` | `src/server/main.cpp`, `src/server/request_params.hpp`, `src/server/route_provider.hpp` |
| Website: Leaflet map, coloured routes, info boxes, route cards, Chart.js comparison, priority sliders | **New** | `web/index.html`, `web/style.css`, `web/app.js` |
| Scenario-mode buttons and controls | **New**: Normal / Rush hour / Rain / Incident, sent as `scenario=` | `web/index.html`, `web/app.js`, `src/server/request_params.hpp` |

### Other changes
- `CMakeLists.txt`: removed the **duplicated** `routelens_server` target (the
  file would not configure), added the `test_request_params` target. Existing
  targets are unchanged.
- `tests/test_request_params.cpp`: 12 checks for query validation (missing and
  non-numeric values, NaN, outside Dehradun, weight normalisation, zero and
  negative weights, unknown scenario).
- `docs/AAYUSH_CHANGES.md`: this file.

### API summary
| Endpoint | Behaviour |
|---|---|
| `GET /api/health` | `{"status":"ok"}` |
| `GET /api/traffic/status` | Source (`live` or `free_flow_fallback`, from whether `TOMTOM_API_KEY` is set), rate-limiter count, LRU cache size and capacity |
| `GET /api/routes?from_lat&from_lon&to_lat&to_lon[&w_time&w_distance&w_signals&w_traffic][&scenario]` | `400` on bad input, `501` until the routing engine is connected, `200` with `routes[]` after |
| `GET /`, `GET /web/<file>` | Website (rejects `..` in file names) |

### Design decisions
- **No fake routes.** The routing algorithms (Dijkstra, A\*, Yen) are not in the
  repo yet, so `/api/routes` returns `501` instead of invented numbers, and the
  website shows "Routing engine not connected yet". Showing made-up ETAs in a
  routing project would invalidate any usability test or demo.
- **`RouteProvider` interface** (`src/server/route_provider.hpp`) is the only
  seam between server and engine. The routing owner implements `compute()`;
  `main.cpp` swaps `NullRouteProvider` for the real one (one line).
- **Cache is guarded by a mutex** in the server: `LRUCache` is not thread-safe
  and Crow runs multithreaded.
- **Colour-blind-safe palette** (Okabe-Ito) with route number, dash pattern and
  border style as redundant cues. This starts the Phase III accessibility item.
- Priority weights are normalised to sum to 1 on the server; scenario names are
  validated against a whitelist.

### Verification (run 2026-10-20, WSL Ubuntu, GCC 15, CMake 4.2)
- `ctest`: 4/4 pass (KDTree, LRUCache, RateLimiter, RequestParams).
- Live check of all endpoints: `/api/routes` returns 501 for valid input and
  400 for out-of-area coordinates and unknown scenarios; static files serve with
  correct content types; `..` file names return 404.
- **Not verified:** the website in a browser (Leaflet and Chart.js load from
  cdnjs, and there is no routing data to draw yet). Open
  `http://127.0.0.1:18080/` after running `routelens_server`.

### Known gaps and open items (mine)
- KD-tree stores plain `(x, y)` points. It is not yet connected to the OSM graph
  nodes, so "nearest road to clicked point" will be wired once the loader data
  is available.
- The rate limiter and LRU cache are exposed in `/api/traffic/status` but not
  yet used by a TomTom client. That client has to be written.
- `tests/test_kdtree.cpp:45` has an unused-variable warning.

## Phase III (Finish): not started
Mobile and usability-test polish, Docker, GitHub Actions CI, repo maintenance,
and tests for the graph loader and traffic module.

## Files deliberately not touched (other members)
`src/core/*`, `src/data/*`, `src/algorithms/min_heap.hpp`,
`src/app/osm_info_main.cpp`, `preprocess/export_osm.py`, `tests/test_osm.cpp`,
`tests/test_min_heap.cpp`, `Makefile`, `docs/OSM_DATA.md`, `docs/reports/*`,
`README.md`, `external/Crow` (third-party submodule).
`src/core/dd.c` is an empty stray file; I left it for its owner to delete.
