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
- ~~KD-tree not connected to the OSM graph~~: done, see "OSM road network and
  KD-tree road snapping" below.
- The TomTom client (written by the map-data and traffic owner) is now wired
  into the server, see "TomTom key and traffic client wiring" below. Still open:
  my `RateLimiter` and `LRUCache` are not used by it, because the client already
  spaces its own requests and keeps its own spatial cache; real traffic data
  only flows once the routing engine calls `updateRouteTraffic()`.
- ~~`tests/test_kdtree.cpp` unused-variable warning~~: fixed. It was a symptom
  of a real bug: Release builds define `NDEBUG`, which turned every `assert`
  in that test into a no-op, so CI would have reported it as passing without
  checking anything.

## Phase III (Finish)

| Plan item | Status | Files |
|---|---|---|
| GitHub Actions CI | **New**: builds with CMake and runs `ctest` (which now includes the OSM loader and traffic tests), checks `web/app.js` syntax, and a `docker-build` job builds the image and checks that the container answers `/api/health`; runs on pushes to `main` and on every pull request | `.github/workflows/ci.yml` |
| Deploy with Docker | **New**: multi-stage image (build stage with CMake, asio, libcurl and cJSON, slim runtime stage running as a non-root user, healthcheck on `/api/health`) | `Dockerfile`, `.dockerignore` |
| Server configurable for containers | **New**: `ROUTELENS_BIND` (default `127.0.0.1`) and `ROUTELENS_PORT` (default `18080`) | `src/server/main.cpp` |
| Mobile / colour-blind polish, usability test | Mobile layout and colour-blind-safe palette started in Phase II; usability test **not started** | `web/style.css` |
| Tests for the graph loader and traffic module | **Not started** (needs agreement with the module owner) | none |

Run with Docker (clone with `--recurse-submodules`):
```bash
docker build -t routelens .
docker run --rm -p 18080:18080 routelens
```

With the road network mounted:
```bash
docker run --rm -p 18080:18080 -v "$PWD/data/osm:/app/data/osm:ro" routelens
```

Verification (2026-10-20, Docker 29.1.3 in WSL Ubuntu 26.04):
- The image built in 183 s and is 119 MB. It runs as a non-root user
  (`routelens`) and Docker's own healthcheck reported `healthy`.
- With the CSVs mounted it loaded 31,802 nodes and 72,626 edges and answered
  `/api/routes` (501 with `snapped` info, since the routing engine is not
  connected); the website returned 200. Without the mount it still started and
  answered 503 for `/api/routes`.
- **Docker build broke on `main` after the traffic module was merged**: CMake now
  requires libcurl and cJSON (`Could NOT find CURL`), and the image's build
  stage did not install them. Fixed by adding `libcurl4-openssl-dev` and
  `libcjson-dev` to the build stage. The runtime stage needs no extra libraries
  because `routelens_server` does not link the traffic library yet.
- The new `docker-build` CI job builds the image and checks `/api/health`, so a
  broken Dockerfile is caught on the pull request instead of at deploy time.
  Its first run happens on the pull request for this change.

## RouteInfo and website update (agreed with Sneha)

Changes proposed by Sneha (traffic and map data) and accepted:

| Change | Where |
|---|---|
| ETA is sent in **seconds** (`eta_s`); the website formats it as "37 min" / "1 h 5 min" and rounds only at display time | `src/server/route_provider.hpp`, `src/server/main.cpp`, `web/app.js` |
| **Free-flow ETA** (`free_flow_eta_s`) next to the traffic ETA, so the effect of traffic is visible | same files, plus the comparison chart |
| **Data coverage** (`coverage.live_pct`, `typical_pct`, `estimated_pct`) so the app does not overstate how much of a route has live data | same files |
| "Signals" renamed to **"major junctions"** (`junctions`, query parameter `w_junctions`): OSM maps only 6 signals in Dehradun | `request_params.hpp`, `tests/test_request_params.cpp`, `web/index.html`, `web/app.js` |
| Range sliders no longer overflow their section on narrow screens | `web/style.css` |

The API field names changed (`eta_min` to `eta_s`, `signals` to `junctions`).
Nothing consumed the old names yet, since the routing engine is not connected.
Anyone implementing `RouteProvider` should use the new `RouteInfo` fields.

Verification (2026-10-20):
- C++ build and `ctest`: 4/4 pass.
- The real `web/app.js` was run in headless Edge with a stand-in for `/api/routes`
  (test harness only, not part of the project): the map loads, two clicks set
  Start and End, no JavaScript errors, durations show as "37 min", "1 h 5 min",
  "1 h" and "< 1 min", coverage and junction labels show, the chart has four
  series, and the "routing engine not connected" banner shows on a 501.
- Layout was checked in 360 px and 400 px wide frames: no horizontal overflow.
- **Not verified:** real routes from the routing engine (not connected yet),
  real touch devices, and a screen reader.
- Usability note: on a phone the map is above the controls, so "Find routes" is
  below the fold. Worth watching in the classmate usability test.

## OSM road network and KD-tree road snapping

Uses the Dehradun export provided by the map-data owner (`nodes.csv`,
`edges.csv`; 31,802 nodes and 72,626 edges after loading, identical to
`data/osm/meta.txt`). The CSVs are **not** committed: `data/osm/*.csv` is now
in `.gitignore`.

| Plan item | Change | Files |
|---|---|---|
| KD-tree finds the nearest road to a clicked point | New `RoadSnapper`: the KD-tree holds points spaced at most 20 m apart along every road shape (609,723 points), each tagged with its road. Distances are computed in local metres (longitude is scaled by cos(latitude)). A click snaps to the nearest *road*, even in the middle of a long straight road, and to the nearer end of it as the routing node. | `src/spatial/road_snapper.{hpp,cpp}` (new), `src/spatial/kdtree.hpp` (`Point` gains an `id` payload; existing uses unchanged) |
| Server uses the road network | Loads the CSVs at startup (`ROUTELENS_DATA_DIR`, default `data/osm`). `/api/routes` snaps both clicks and passes graph node ids to the routing engine in `RouteRequest::fromNode/toNode`. Rejects a click farther than 500 m from any road (`422`), start and end on the same junction (`422`) and a missing network (`503`). Responses include `snapped` (node, position, road class, distance). `/api/health` reports whether the network is loaded. | `src/server/main.cpp`, `src/server/request_params.hpp` |
| Website shows the snap | White ring markers show where each click landed on the road network, with a tooltip (road class and distance). | `web/app.js` |
| Tests | 15 checks: a hand-made network (nearest road beats nearer vertices of another road, node choice, rejection, empty network) plus, when the CSVs are present, 200 random points on the real network compared against exact brute force (worst gap 3.6 m) and a Clock Tower landmark. Skips the real-data part when the CSVs are absent, so CI still passes. | `tests/test_road_snapper.cpp` (new) |
| Build | `routelens_osm` static library (the map-data module's `graph.c` and `osm_loader.cpp` plus `kdtree.cpp` and the snapper), used by the server and the new test. Their sources are only compiled, not modified. | `CMakeLists.txt` |
| Docker | `ROUTELENS_DATA_DIR=/app/data/osm`; the CSVs are mounted, not baked into the image: `docker run -p 18080:18080 -v "$PWD/data/osm:/app/data/osm:ro" routelens`. | `Dockerfile` |

Measured on the real network (WSL, Release build): KD-tree built in about
80 ms, server ready in 0.33 s, 56 MB resident, about 4 ms per `/api/routes`
request. Graphic Era snapped 7 m from a tertiary road, the Clock Tower 2.5 m from
a secondary road.

Verification (2026-10-20): `ctest` 5/5 pass; the map-data owner's `make test`
(48 checks) still passes; live requests against the running server returned
501 with `snapped` for a valid pair, 422 for a start in the hills and for
identical points, 400 outside Dehradun, and 503 when the CSVs are missing; the
real `web/app.js` was run in headless Edge with a stand-in API and drew 4
routes plus 2 snapped markers, or 2 markers plus the banner on a 501.

Limits: the snapped point is the nearer *end* of the nearest road, not the exact
spot on it, so very long roads can add a detour at either end. A click is
rejected only beyond 500 m. Routes themselves still need the routing engine.

## TomTom key and traffic client wiring

How the key is used (it is never in git, in the image, in a log or in any API
response):

| Where | What |
|---|---|
| Local | Put `TOMTOM_API_KEY=...` in `.env` (git-ignored) and start with `set -a; source .env; set +a; ./build/routelens_server`. |
| Server (`src/server/main.cpp`) | Reads `TOMTOM_API_KEY` from the environment only and passes it to `TomTomOptions.apiKey`. The client puts it in the request URL only. An empty or missing key disables the client, and the server reports free-flow data (`source: free_flow_fallback`). |
| Thread safety | The client is not thread-safe and Crow is multithreaded, so every use is guarded by a mutex. |
| Check that the key works | Start with `ROUTELENS_TRAFFIC_SELFTEST=1`: one real request (counts toward the daily quota) for the Clock Tower; the status is logged and shown in `/api/traffic/status` as `tomtom.self_test`. A rejected key turns `source` back to `free_flow_fallback`. |
| `/api/traffic/status` | New `tomtom` block: `requests_today`, `requests_this_run`, `cache_hits`, `daily_cap`, `self_test`. |
| Daily usage file | `ROUTELENS_CACHE_DIR` (default `data/cache`) holds `tomtom_usage.txt`, the client's own daily counter (cap 2,000 of TomTom's 2,500 free requests). |
| Docker | Runtime stage now has `libcurl4t64`, `libcjson1` and `ca-certificates` (without certificates HTTPS fails in the container) and a writable `/var/lib/routelens`. Run with `--env-file .env -v routelens-cache:/var/lib/routelens`. |
| Build | `routelens_server` links `routelens_traffic` (`CMakeLists.txt`). |

Verification (2026-10-20/21), using a deliberately fake key (`invalid-test-key`,
not a secret) so no real key was needed or seen:
- `ctest` 7/7 pass.
- No key: self-test reports `disabled` and makes no network request.
- Fake key: a real HTTPS request reached TomTom and was answered `key rejected`;
  one request was counted, the usage file was written, `source` stayed
  `free_flow_fallback`. The key text appeared nowhere in the log, status, health
  or routes output.
- Same test inside the Docker image (131 MB): HTTPS worked, so the certificates
  and libraries are right; the non-root user wrote the usage file into the
  mounted volume; the key did not appear in the container log.
- **Verified with a real key** (2026-10-20, key loaded from the git-ignored
  `.env`, never printed): a direct request returned HTTP 200; the server
  self-test returned `ok`; `/api/traffic/status` showed `source: live`,
  `self_test: ok`, `requests_this_run: 1`; the website badge read
  "traffic: live" with no JavaScript errors; the daily counter persisted across
  restarts (`requests_today: 2`). The same run inside Docker (key passed with
  `--env-file .env`) also gave `self_test: ok`. In every case the key appeared
  0 times in the server log, container log, status, health and website
  responses. The server and the 7-test suite also build warning-free in a
  fresh Release build.
- Caveat: without `ROUTELENS_TRAFFIC_SELFTEST=1`, `source: live` only means a key
  is configured, not that it was checked. Real fetches happen only when the
  routing engine calls `updateRouteTraffic()`.
- `data/cache/` (the daily-usage counter) is now git-ignored.

Limits: the client's segment cache is an unbounded vector (a long-running server
should cap it; ask the traffic owner). Traffic reaches routes only through the
routing engine calling `updateRouteTraffic()`, which does not exist yet.

## Files deliberately not touched (other members)
`src/core/*`, `src/data/*`, `src/algorithms/min_heap.hpp`,
`src/app/osm_info_main.cpp`, `preprocess/export_osm.py`, `tests/test_osm.cpp`,
`tests/test_min_heap.cpp`, `Makefile`, `docs/OSM_DATA.md`, `docs/reports/*`,
`README.md`, `external/Crow` (third-party submodule).
`src/core/dd.c` is an empty stray file; I left it for its owner to delete.
