# RouteLens traffic module: specification

Owner: Sneha Rawat (map data and traffic). This document describes what the
traffic module does, its inputs and outputs, every threshold it uses and why,
and how to call it from the server. All numbers below are the defaults in the
code (`src/traffic/`).

## 1. Files

| File | Purpose |
|---|---|
| `src/traffic/traffic.hpp/.cpp` | Rules, TomTom response parsing, matching TomTom segments to graph edges, per-edge traffic state, route sampling |
| `src/traffic/tomtom_client.hpp/.cpp` | TomTom Traffic Flow client with free-plan guardrails; `updateRouteTraffic()` |
| `src/traffic/eta.hpp/.cpp` | Route summary: ETA with and without traffic, traffic level, data coverage, junction count; display formatting |
| `src/traffic/junctions.hpp/.cpp` | Major-junction list: loading, snapping to the graph, counting per route |
| `src/data/http_client.*`, `json_util.hpp`, `geo.cpp` | libcurl HTTP client, cJSON helpers, distance/geometry helpers |
| `data/junctions/major_junctions.csv` | 41 documented junctions (+9 pending), with sources |
| `tests/test_traffic.cpp`, `tests/test_junctions.cpp` | Offline tests (110 + 31 checks) |

CMake library: `routelens_traffic` (links `routelens_osm`, libcurl, cJSON).
System packages: `sudo apt install -y libcurl4-openssl-dev libcjson-dev`.

## 2. Data flow for one route

```
route edges (from routing)
  -> sampleRoute(): a point every 400 m, only on road classes TomTom covers
  -> TomTomClient::fetch(point) for each sample (cache answers most of them)
  -> TrafficState::applySegment(): TomTom segment -> matching graph edges
  -> summarizeRoute(): ETA, level, coverage, junctions
```

## 3. TomTom request

- Endpoint: `GET {baseUrl}/traffic/services/4/flowSegmentData/absolute/{zoom}/json?point={lat},{lon}&unit=KMPH&key={key}`
- Defaults: `baseUrl = https://api.tomtom.com`, `zoom = 10`
- Fields used: `frc`, `currentSpeed`, `freeFlowSpeed`, `currentTravelTime`,
  `freeFlowTravelTime`, `confidence`, `roadClosure`, `coordinates.coordinate[]`
  (the segment shape, ordered in the direction of travel)
- The API key is passed in `TomTomOptions::apiKey` by the caller. The server
  should read it from the `TOMTOM_API_KEY` environment variable. The key appears
  only in request URLs, never in log or error messages.

Measured on Dehradun (Oct 2026): segments are long on major roads (6–22 km, one
speed for the whole stretch; zoom 22 returned the same 20.6 km segment as zoom 10)
and shorter on smaller roads (1.75 km for an FRC4 road). For points on minor
roads, TomTom answers with the nearest major road, up to 2.6 km away; the
distance check below rejects those.

## 4. Rules and thresholds (`TrafficRules`)

| Name | Default | Meaning | Origin |
|---|---|---|---|
| `maxSnapM` | 50 m | A TomTom answer whose road is farther than this from the query point is "no coverage" | Measured: correct matches were 0–15 m away, wrong ones 305–2,593 m |
| `matchM` | 20 m | A graph edge matches a segment only if its 25%, 50% and 75% points are all within this distance of the segment | Design choice |
| `maxBearingDiffDeg` | 45° | ...and the edge points the same way as the nearest piece of the segment | Design choice; opposite-direction edges stay free-flow |
| `minConfidence` | 0.5 | Below: data ignored (free-flow) | TomTom: 0.5 = historical only |
| `liveConfidence` | 0.6 | Above: labelled "live"; 0.5–0.6: "typical for this time" | TomTom: > 0.6 = real-time probe data, 0.5–0.6 = weighted mix (documented in Azure Maps' description of the same API) |
| `lowRatio` | 0.8 | current/free-flow ≥ 0.8: Low traffic | Our design choice, to be tuned |
| `highRatio` | 0.5 | current/free-flow < 0.5: High traffic; between: Medium | Our design choice, to be tuned |

Sampling: every 400 m (`spacingM`), only on edges whose OSM class is
motorway, trunk, primary, secondary, tertiary or unclassified (and their `_link`
roads). Residential, service and living streets are skipped: TomTom has no
data there, and skipping them saves quota.

## 5. Free-plan guardrails (`TomTomOptions`)

TomTom Freemium: 2,500 non-tile requests per day, HTTP 429 when exceeded (no
charge without a card), about 5 requests per second.

| Name | Default | Behaviour |
|---|---|---|
| `cacheTtlS` | 300 s | A point within `maxSnapM` of a segment fetched in the last 5 minutes is answered from memory. Segments are cached even when they were too far from the point that fetched them (they are valid data for their own road). "No coverage" points are cached too. |
| `dailyCap` | 2000 | Own hard stop below TomTom's 2,500. Counted per local date in `usageFile` (`data/cache/tomtom_usage.txt`, format `YYYY-MM-DD N`); survives restarts; every request counts, including failed ones. |
| `minIntervalS` | 0.25 s | At most 4 requests per second |
| `maxConsecutiveFailures` | 3 | After 3 network or 5xx failures in a row, the client stops asking for the rest of the run (outage) |

Known gap: the segment cache has no size limit. Fine for a demo; for a
long-running server it should be bounded (planned: Aayush's `LRUCache`).

## 6. Fetch results (`FetchStatus`)

| Status | When | Request made? | Client afterwards |
|---|---|---|---|
| `Ok` | HTTP 200, TomTom road within `maxSnapM` | yes | |
| `CachedOk` | point lies on a fresh cached segment | no | |
| `NoCoverage` | road farther than `maxSnapM`, or HTTP 400 | yes | point cached as a miss |
| `CachedNoCoverage` | known miss within `maxSnapM`, still fresh | no | |
| `DailyCapReached` | `dailyCap` reached today | no | |
| `Disabled` | no key, or stopped after an earlier error | no | |
| `AuthError` | HTTP 401/403 | yes | disabled for this run |
| `QuotaExceeded` | HTTP 429 | yes | disabled for this run |
| `Unavailable` | network error or HTTP 5xx | yes | disabled after 3 in a row |
| `BadResponse` | other HTTP status, or unparsable body | yes | |

## 7. Per-edge traffic (`TrafficState`)

For each matched edge: `source` (Live / Historical), `ratio` = current ÷
free-flow speed, `confidence`, `durationS` = edge length ÷ TomTom's current
speed, and `graph->edges[e].traffic_index` = 1 − ratio (clamped to 0–1).
A road closure (`roadClosure` or speed 0) keeps the free-flow duration, sets
`closed` and `traffic_index = 1`. Better-confidence data is never overwritten
by worse. Unmatched edges stay free-flow (`durationS` = free-flow duration).

`computeCosts(weights, out)` gives edge costs like `graph_compute_costs()`,
but with the traffic-aware `durationS`, so the routing can use live times.

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

## 10. Using it in the server

```cpp
// once at startup
rl::JunctionIndex junctions;
std::string err;
rl::loadJunctions("data/junctions/major_junctions.csv", osm, 50.0, junctions, err);

rl::TomTomOptions opt;
opt.apiKey = getenv("TOMTOM_API_KEY") ? getenv("TOMTOM_API_KEY") : "";   // empty = free-flow only
rl::HttpClient http("RouteLens/1.0 (contact email)", 20);
rl::TomTomClient tomtom(opt, [&](const std::string& url) { return http.get(url); });

// per request, per candidate route
rl::TrafficState state(osm);   // note: writes traffic_index into osm.graph
rl::updateRouteTraffic(osm, state, tomtom, routeEdges, opt.rules);
rl::RouteSummary s = rl::summarizeRoute(osm, state, routeEdges, opt.rules, 30.0, &junctions);
// RouteInfo: eta = s.etaS (seconds), eta_free_flow = s.freeFlowS, distance = s.distanceM,
//            traffic level = rl::levelName(s.level), junctions = s.junctions,
//            data coverage = s.liveShare / s.historicalShare / s.estimatedShare
```

Thread safety: `TomTomClient` and `TrafficState` are not thread-safe. In a
multithreaded server, use one `TrafficState` per request on its own copy of
the graph's traffic fields, and guard the shared `TomTomClient` with a mutex.

## 11. Tests and suggestions

- `test_traffic` (110 checks): parsing, rules, matching (position and direction),
  `TrafficState`, every `FetchStatus`, cache expiry, daily cap across restarts and
  dates, rate limiting, outage cut-off, key never in messages, route summary.
- `test_junctions` (31 checks): loading (quoted CSV fields, pending rows, bad
  input), snapping, two nodes of one junction counted once, waits only on
  free-flow edges.
- Both run offline with a fake `FetchFn`, so they use no TomTom quota.

Useful additional tests: a route that leaves and re-enters the same TomTom
segment; a segment crossing the route at right angles (should not match);
`dailyCap = 0`; a usage file with garbage content; many concurrent requests
once the server adds a mutex.

## 12. Known limitations

- One speed per TomTom segment (up to ~20 km on major roads); local jams are averaged out.
- No live data on residential roads; they always use provisional free-flow speeds.
- TomTom may report only one direction of a two-way road; the other direction stays free-flow.
- Free-flow speeds per road class (`osm_loader.cpp`) and the 30 s junction wait are provisional and will be calibrated in the ETA accuracy study.
- A route passing within 50 m of a junction without going through it can be counted as passing it.
- 9 junctions reported in Aug 2025 have no verified coordinates yet.
## 13. Scenarios (`scenario.hpp`)

| Scenario | Starts from | Change | TomTom requests |
|---|---|---|---|
| `normal` | live TomTom + free-flow | none | yes |
| `rain` | free-flow (live ignored) | every road's speed x `rainFactor` = 0.88 | none |
| `rush_hour` | free-flow (live ignored) | main roads x `rushMainFactor` = 0.65, others x `rushOtherFactor` = 0.85 | none |
| `incident` | live TomTom + free-flow | chosen edges blocked, or slowed to `incidentSlowFactor` = 0.30 | yes |

- `rainFactor`: FHWA Road Weather Management reports free-flow speed reductions of 6-17%
  in heavy rain (US freeways); 0.88 is the middle. Dehradun monsoon conditions may be worse.
- `rushMainFactor` / `rushOtherFactor`: **provisional**, to be replaced by ratios measured
  with TomTom on Dehradun's main roads during the evening rush (a daytime run measured 0.70).
- Rain and rush hour ignore live data so a slowdown is never counted twice.
- Junction waits are added on scenario edges (a scenario estimate has no real waiting in it).
- Blocked edges: `EdgeTraffic::blocked`, routing cost 1e9 in `computeCosts()`, and
  `RouteSummary::anyBlocked` when a route uses one. Reachability (BFS) is the routing side's job
  and can read `state.edge(e).blocked`.
- The summary reports `scenarioShare`; `coverageLabel()` says e.g. "scenario estimate on 100% of the route".

Server usage:

```cpp
rl::Scenario sc;
if (!rl::parseScenario(scenarioText, sc)) { /* 400: unknown scenario */ }
rl::ScenarioSettings scenarioSettings;          // defaults above
rl::TrafficState state(osm);
if (rl::scenarioNeedsLiveTraffic(sc))
    rl::updateRouteTraffic(osm, state, tomtom, routeEdges, opt.rules);
rl::Incident incident;                          // only for "incident": edges the user picked
incident.edges = pickedEdges;
incident.blocked = userChoseBlock;
rl::applyScenario(state, sc, scenarioSettings, sc == rl::Scenario::Incident ? &incident : nullptr);
auto s = rl::summarizeRoute(osm, state, routeEdges, opt.rules, 30.0, &junctions);
```
