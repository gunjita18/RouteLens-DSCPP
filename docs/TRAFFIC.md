# RouteLens traffic module: specification

Owner: Sneha Rawat (map data and traffic). This document describes what the
traffic module does, its inputs and outputs, every threshold it uses and why,
and how to call it from the server. All numbers below are the defaults in the
code (`src/traffic/`).

> **Status: the server reads a saved traffic snapshot; it makes no TomTom calls.**
> TomTom data is fetched once, before the demo, by `preprocess/fetch_traffic.py` into
> `data/traffic/traffic.csv`; the server loads it at startup (`makeTrafficProvider`), so the
> demo works offline. The earlier live path (HTTP client, segment cache, daily cap,
> TomTom response parsing, matching of a segment to graph edges, route sampling) and its
> tests were removed; they remain in Git history. The server needs no HTTP or JSON library.
> See "Traffic snapshot (offline demo data)" and "C++ class hierarchies" at the end.

## 1. Files

| File | Purpose |
|---|---|
| `src/traffic/traffic.hpp/.cpp` | Traffic-level thresholds, per-edge traffic state (`TrafficState`) |
| `src/oop/traffic_provider.*`, `scenario.*`, `load_error.hpp` | Class hierarchies: `TrafficProvider` (free-flow, snapshot) and `Scenario` (normal, rain, rush hour, incident) |
| `src/core/traffic_reader.*` | C reader that loads the saved snapshot into a hash table |
| `preprocess/fetch_traffic.py` | Saves the TomTom snapshot (run once, before the demo) |
| `src/traffic/eta.hpp/.cpp` | Route summary: ETA with and without traffic, traffic level, data coverage, junction count; display formatting |
| `src/traffic/junctions.hpp/.cpp` | Major-junction list: loading, snapping to the graph, counting per route |
| `src/server/osm_route_provider.cpp` | Uses the traffic provider: edges with data take their measured travel time; route cards report the share of the route covered and the traffic level |
| `src/data/geo.cpp` | Distance and geometry helpers |
| `data/junctions/major_junctions.csv` | 41 documented junctions (+9 pending), with sources |
| `tests/test_traffic.cpp`, `test_junctions.cpp`, `test_scenarios.cpp`, `test_traffic_provider.cpp`, `test_route_provider.cpp` | Offline tests |

CMake library: `routelens_traffic` (links `routelens_osm`). It needs no system library:
only `preprocess/fetch_traffic.py` talks to TomTom, with the Python standard library.

## 2. Data flow

```
before the demo:   fetch_traffic.py --(TomTom)--> data/traffic/traffic.csv

at server start:   traffic.csv -> traffic_reader.c (hash table) -> SnapshotProvider
                   OsmRouteProvider reads it once: per-edge travel time and congestion

per request:       route search (edge costs include the measured times)
                   -> per route: ETA, share of the route covered by data, traffic level
```

The rain and rush-hour scenarios ignore the snapshot and start from free-flow speeds, so a
slowdown is never counted twice; the normal and incident scenarios use it.

## 3. TomTom request (made by `preprocess/fetch_traffic.py`)

- Endpoint: `GET {baseUrl}/traffic/services/4/flowSegmentData/absolute/{zoom}/json?point={lat},{lon}&unit=KMPH&key={key}`
- Defaults: `baseUrl = https://api.tomtom.com`, `zoom = 10`
- Fields used: `frc`, `currentSpeed`, `freeFlowSpeed`, `currentTravelTime`,
  `freeFlowTravelTime`, `confidence`, `roadClosure`, `coordinates.coordinate[]`
  (the segment shape, ordered in the direction of travel)
- The API key is read from the `TOMTOM_API_KEY` environment variable only. It appears
  only in the request URL, never in output, error messages or files.

Measured on Dehradun (Oct 2026): segments are long on major roads (6–22 km, one
speed for the whole stretch; zoom 22 returned the same 20.6 km segment as zoom 10)
and shorter on smaller roads (1.75 km for an FRC4 road). For points on minor
roads, TomTom answers with the nearest major road, up to 2.6 km away; the
50 m rule below rejects those.

## 4. Rules and thresholds

**Used when the snapshot is made** (`preprocess/fetch_traffic.py`, again in `traffic_reader.c`):

| Rule | Value | Meaning | Origin |
|---|---|---|---|
| Wrong-road distance | 50 m | A TomTom answer whose road is farther than this from the query point is a different road: nothing is recorded | Measured: correct matches were 0–15 m away, wrong ones 305–2,593 m |
| Minimum confidence | 0.5 | Below: data ignored (free flow) | TomTom: 0.5 = historical only |
| Roads queried | trunk, primary, secondary | TomTom has little data on smaller roads | Measured |

**Used when routes are summarised** (`TrafficRules` in `traffic.hpp`):

| Name | Default | Meaning | Origin |
|---|---|---|---|
| `lowRatio` | 0.8 | current/free-flow ≥ 0.8: Low traffic | Our design choice, to be tuned |
| `highRatio` | 0.5 | current/free-flow < 0.5: High traffic; between: Medium | Our design choice, to be tuned |

A snapshot edge is never labelled "live": a saved snapshot is not live traffic, and the old
live/typical split by confidence (0.6) went away with the live client.

## 5. Free-plan guardrails (`preprocess/fetch_traffic.py`)

TomTom Freemium: 2,500 non-tile requests per day, HTTP 429 when exceeded (no
charge without a card), about 5 requests per second.

| Setting | Value | Behaviour |
|---|---|---|
| `--max-requests` | 2000 | Own hard stop below TomTom's 2,500 per run; edges not reached are counted as "not fetched" |
| `MIN_INTERVAL_S` | 0.25 s | At most 4 requests per second |
| Segment reuse | 50 m | A midpoint within 50 m of an already fetched TomTom segment reuses its answer; a point with no usable TomTom road is remembered so that neighbours are not asked again |
| `MAX_CONSECUTIVE_FAILURES` | 5 | After 5 network or 5xx failures in a row the run stops |
| HTTP 401 / 403 / 429 | | The run stops at once (rejected key, quota); the message never contains the URL or key |

The script keeps no usage counter between runs: the daily limit is TomTom's, enforced by HTTP 429.

## 6. Outcome of each edge in a fetch run

| Outcome | When | Request made? |
|---|---|---|
| recorded | TomTom road within 50 m and confidence 0.5 or more | once per segment; later edges reuse it |
| `reused` | midpoint within 50 m of a fetched segment | no |
| `wrong_road` | TomTom's road is more than 50 m from the midpoint | yes (the edge records nothing) |
| `no_coverage` | HTTP 400/404, or next to a remembered miss | yes, or no |
| `low_confidence` | confidence below 0.5 | as for recorded |
| `errors` | network error, HTTP 5xx or an unusable body | yes |
| `not_fetched` | the request limit or an abort was reached first | no |

The first real run: 2,445 candidate edges, 168 requests, 2,047 recorded (see "Traffic snapshot").

## 7. Per-edge traffic (`TrafficState`)

`applyProvider(state, provider)` writes measured data into the state. For each edge that has
data: `source` = Historical (a saved snapshot), `ratio` = current ÷ free-flow speed,
`confidence`, `durationS` = edge length ÷ measured speed, and
`graph->edges[e].traffic_index` = 1 − ratio (clamped to 0–1). A measured speed of 0
marks the road closed: the free-flow duration is kept, `closed` is set and `traffic_index = 1`.
Edges without data stay free-flow (`durationS` = free-flow duration).

`computeCosts(weights, out)` gives edge costs like `graph_compute_costs()`,
but with the measured `durationS`, so the routing can use the measured times.

## 8. Route summary (`summarizeRoute`)

| Field | Meaning |
|---|---|
| `distanceM` | route length |
| `freeFlowS` | ETA with no traffic data: free-flow times + waits |
| `etaS` | traffic-aware ETA |
| `level`, `meanRatio` | Low/Medium/High from the length-weighted ratio over edges with data; `Unknown` if no edge has data |
| `liveShare`, `historicalShare`, `estimatedShare` | fractions of route length by data source |
| `junctions`, `junctionNames` | major junctions on the route, in order (when a `JunctionIndex` is given) |
| `signals` | OSM-mapped signals (information only; only 6 exist in Dehradun) |
| `anyClosure` | TomTom reports a closure on the route |

Waiting time: `signalDelayS` (30 s, provisional) per junction, added **only on
edges without TomTom data**, because TomTom's speeds already include waiting.
Without a `JunctionIndex`, OSM signals are used instead (old behaviour).

Display helpers: `formatDuration()` ("< 1 min", "37 min", "1 h 5 min", "2 h"),
`formatDistance()` ("850 m", "9.4 km"), `coverageLabel()`. Keep times in
seconds internally and format only for display.

## 9. Major junctions (`junctions.hpp`)

- Source list: `data/junctions/major_junctions.csv`. Rows without coordinates
  (status `reported_2025_pending`) are skipped.
- Snapping: a junction claims every graph node within 50 m (divided roads give
  one junction several nodes); a node near two junctions goes to the nearer.
  On the real network: 41 of 41 junctions snapped onto 188 nodes.
- Counting: a junction counts when the route **arrives** at one of its nodes,
  once per route; the start node does not count.

## 10. Using it in the server (`src/server/main.cpp`)

```cpp
// once at startup (after the road network and junctions are loaded)
std::string warning;
auto traffic = rl::makeTrafficProvider(network, "data/traffic/traffic.csv", &warning);
// missing or unreadable file: a FreeFlowProvider and a warning; speeds are "estimated"
provider = std::make_unique<rl::OsmRouteProvider>(network, &junctions, traffic.get());
```

`OsmRouteProvider` reads the traffic provider once, into per-edge arrays, and never writes to
the shared network, so concurrent requests need no lock. For each edge with data:

- travel time = length / measured speed (a measured speed of 0 means a closed road and keeps the free-flow time);
- congestion = `1 - current / free-flow`, which feeds the "traffic" priority and the route's traffic score.

Per route it reports: `eta` (with traffic) and the free-flow ETA without traffic;
`coverage.typical_pct` = share of the route length with snapshot data (the rest is
`estimated_pct`; `live_pct` is always 0, because a snapshot is not live);
`traffic` = Low / Medium / High from the length-weighted speed ratio over the edges with data
(0.8 or more Low, 0.5 to 0.8 Medium, below 0.5 High), or "Estimated" if no edge has data.

`GET /api/traffic/status` returns `source` (`snapshot` or `free_flow_fallback`), `fetched_at`,
`edges_with_data` and any load `warning`. The page shows "Traffic data: <fetched_at>".

Thread safety: the provider and its arrays are read-only after construction. `TrafficState`
(used by the older summary code and the tests) is not thread-safe; use one per request.

## 11. Tests

- `test_traffic` (51 checks): traffic levels, display formatting, `TrafficState`, route summary,
  with measured data supplied by a fake `TrafficProvider` (`tests/fake_traffic.hpp`).
- `test_junctions` (30 checks): loading (quoted CSV fields, pending rows, bad
  input), snapping, two nodes of one junction counted once, waits only on
  free-flow edges.
- `test_traffic_provider`, `test_scenarios`: the class hierarchies, the factory, the fallback.
- `test_route_provider`: routes with measured traffic (slower, faster than free flow, closed
  road, rain ignoring the data, the congestion priority) using a fake `TrafficProvider`.
- `test_traffic_reader`, `fetch_traffic_test`: the snapshot file and the fetch script.
- All run offline and use no TomTom quota.

## 12. Known limitations

- One speed per TomTom segment (up to ~20 km on major roads); local jams are averaged out.
- No live data on residential roads; they always use provisional free-flow speeds.
- TomTom may report only one direction of a two-way road; the other direction stays free-flow.
- Free-flow speeds per road class (`osm_loader.cpp`) and the 30 s junction wait are provisional and will be calibrated in the ETA accuracy study.
- A route passing within 50 m of a junction without going through it can be counted as passing it.
- 9 junctions reported in Aug 2025 have no verified coordinates yet.
- Dehradun has only 6 traffic signals mapped in OSM. The major junctions (documented in `data/junctions/major_junctions.csv`) are therefore **assumed** to be signalised for the waiting-time estimate; this is a modelling assumption, not surveyed signal data, and the 30 s wait per junction is provisional.
- The snapshot is one moment in time. The first real snapshot (Sunday 10:08) was 96% low traffic, so it says little about rush hour; a weekday snapshot is needed to show congestion.

## 13. Scenarios (`src/oop/scenario.hpp`)

| Scenario | Starts from | Change | Uses the traffic snapshot |
|---|---|---|---|
| `normal` | snapshot + free-flow | none | yes |
| `rain` | free-flow (snapshot ignored) | every road's speed x `rainFactor` = 0.88 | no |
| `rush_hour` | free-flow (snapshot ignored) | main roads x `rushMainFactor` = 0.65, others x `rushOtherFactor` = 0.85 | no |
| `incident` | snapshot + free-flow | chosen edges blocked, or slowed to `incidentSlowFactor` = 0.30 | yes |

- `rainFactor`: FHWA Road Weather Management reports free-flow speed reductions of 6-17%
  in heavy rain (US freeways); 0.88 is the middle. Dehradun monsoon conditions may be worse.
- `rushMainFactor` / `rushOtherFactor`: **provisional**, to be replaced by ratios measured
  with TomTom on Dehradun's main roads during the evening rush (a daytime run measured 0.70).
- Rain and rush hour ignore the snapshot so a slowdown is never counted twice.
- Junction waits are added on scenario edges (a scenario estimate has no real waiting in it).
- Blocked edges: `EdgeTraffic::blocked`, routing cost 1e9 in `computeCosts()`, and
  `RouteSummary::anyBlocked` when a route uses one. Reachability (BFS) is the routing side's job
  and can read `state.edge(e).blocked`.
- The summary reports `scenarioShare`; `coverageLabel()` says e.g. "scenario estimate on 100% of the route".

Usage with the class hierarchy (see "C++ class hierarchies" below):

```cpp
rl::ScenarioKind kind;
if (!rl::parseScenario(scenarioText, kind)) { /* 400: unknown scenario */ }
rl::TrafficState state(osm);
rl::applyProvider(state, *traffic);              // snapshot data, where the scenario uses it
rl::Incident incident;                           // only for "incident": edges the user picked
incident.edges = pickedEdges;
incident.blocked = userChoseBlock;
auto scenario = rl::makeScenario(kind, rl::ScenarioSettings{}, &incident);
scenario->apply(state);
auto s = rl::summarizeRoute(osm, state, routeEdges, rl::TrafficRules{}, 30.0, &junctions);
```

The running server computes the same effects per request from read-only arrays instead of
a shared `TrafficState` (see section 10), so parallel requests need no lock.


## Traffic snapshot (offline demo data)

The server runs from a saved snapshot instead of calling TomTom live, so a demo
needs no internet and stays inside TomTom's free quota.

**Create it** (once, before the demo; needs internet and a key):

```bash
export TOMTOM_API_KEY=...                         # never commit or paste the key
python3 preprocess/fetch_traffic.py --dry-run     # count candidate edges, no network
python3 preprocess/fetch_traffic.py               # writes data/traffic/traffic.csv
```

**File format** (`data/traffic/traffic.csv`):

```
# fetched_at = 2026-11-10 09:30
edge_id,current_kph,freeflow_kph,confidence
1452,33,42,0.60
```

`edge_id` is the 0-based data row of the edge in `data/osm/edges.csv`. After loading,
`OsmNetwork::csvRow[e]` gives that id for graph edge `e`.

**Rules** (all in `preprocess/fetch_traffic.py`):

1. Only trunk, primary and secondary edges are queried; the point used is the midpoint of the edge's shape.
2. Segment cache: a midpoint within 50 m of a TomTom segment already fetched reuses its answer.
3. At most 4 requests per second and 2,000 per run.
4. 50 m rule: if TomTom's road is farther than 50 m from the midpoint it is a different road; nothing is recorded.
5. Confidence below 0.5 is ignored (the same rule is applied again by the reader).

**Read it from C** (`src/core/traffic_reader.c`): `ts_load` fills a hash table from edge id to
record; `traffic_index = 1 - current/freeflow`, clamped to 0..1. Edges not in the file, or a
missing file, mean traffic index 0 (free-flow speed); the UI must then label the data "estimated".
Malformed, duplicate and low-confidence rows are skipped and counted (`rows_skipped`).

**Status and limits**

- Unit-tested offline with hand-made fixtures; the fetch script has not yet been run against the live TomTom API in this repository.
- A dry run on the current network finds 2,445 candidate edges, more than the 2,000-request cap, so the run relies on segment reuse. Check the printed request count after the first real run.
- TomTom has no data for small roads, and one segment can span many kilometres (see the limits above); the snapshot is one moment in time, shown to users as "Traffic data: <fetched_at>".


## C++ class hierarchies (`src/oop/`)

Thin C++ classes wrap the C core. The C structures are owned privately and freed in
destructors (RAII); callers use the abstract interfaces and never see the C types.

| Hierarchy | Base (abstract) | Derived | Chosen by |
|---|---|---|---|
| Traffic data | `TrafficProvider` | `FreeFlowProvider`, `SnapshotProvider` | `makeTrafficProvider(net, path)` at startup |
| Scenarios | `Scenario` | `NormalScenario`, `RainScenario`, `RushHourScenario`, `IncidentScenario` | `makeScenario(kind, settings, incident)` per request |
| Errors | `std::runtime_error` | `LoadError` | thrown by `SnapshotProvider` |

- **Abstraction:** routing code calls `provider.trafficIndex(edge)` and `scenario->apply(state)` without knowing the concrete class.
- **Polymorphism:** `apply()` and `trafficIndex()` are virtual; one `std::unique_ptr<Scenario>` behaves as rain, rush hour or incident.
- **Inheritance:** four scenario classes and two providers reuse their base interface; `LoadError` extends `std::runtime_error`.
- **Encapsulation:** `SnapshotProvider` keeps the C `TrafficSnapshot` private; scenario constructors reject factors outside (0, 1] (`std::invalid_argument`), so an existing object is always valid.

`ScenarioKind` (the enum the website's names map to) is separate from the `Scenario` class.
`applyScenario()` remains as a wrapper (`makeScenario(...)->apply(state)`) that returns -1 on invalid settings.

**Using a snapshot:**

```cpp
std::string warning;
auto provider = rl::makeTrafficProvider(net, "data/traffic/traffic.csv", &warning);
// missing file -> FreeFlowProvider and a warning; speeds are then "estimated"
rl::TrafficState state(net);
rl::applyProvider(state, *provider);                       // edges with data get measured speeds
auto scenario = rl::makeScenario(kind, rl::ScenarioSettings{});
scenario->apply(state);
// show provider->description(), e.g. "snapshot 2026-11-22 10:08"
```

`applyProvider` sets travel time to length / measured speed, as the design specifies; a
measured speed of 0 marks the edge closed and keeps its free-flow time. Snapshot edges are
marked `DataSource::Historical` (named "saved snapshot" by `sourceName`) because a saved
snapshot is not live traffic; route summaries count them as "typical", and the page shows
the snapshot time (`provider->description()`) next to the data label.

The server uses these classes: `main.cpp` calls `makeTrafficProvider` at startup and gives
the result to `OsmRouteProvider` (section 10).
