# Scenario (what-if) API

`POST /api/scenario` closes or slows roads around chosen points, checks with a
breadth-first search (BFS) that the destination can still be reached, and returns
the routes that remain, next to the best route without the closures.

The routing core is in `src/algorithms/scenario.{hpp,cpp}`; the endpoint is in
`src/server/main.cpp`.

## Request

JSON body (`Content-Type: application/json`):

| Field | Required | Meaning |
|---|---|---|
| `from_lat`, `from_lon`, `to_lat`, `to_lon` | yes | Start and destination, inside the Dehradun box |
| `w_time`, `w_distance`, `w_junctions`, `w_traffic` | no | Priorities, same as `/api/routes` |
| `closures` | yes | 1 to 20 closure objects (below) |

Closure object:

| Field | Required | Default | Meaning |
|---|---|---|---|
| `lat`, `lon` | yes | | Centre of the closure, inside the Dehradun box |
| `radius_m` | no | 150 | 20 to 2000. Every road passing within this distance is affected |
| `mode` | no | `"block"` | `"block"`: road unusable. `"slow"`: travel time multiplied |
| `slow_factor` | no | 3 | 1.1 to 20, `"slow"` mode only |

A road is affected if its real shape (not just the line between its end
junctions) passes within `radius_m` of the closure centre. A block wins over a
slowdown on the same road; overlapping slowdowns multiply.

```bash
curl -s -X POST http://127.0.0.1:18080/api/scenario \
  -H 'Content-Type: application/json' \
  -d '{"from_lat":30.3243,"from_lon":78.0412,"to_lat":30.2870,"to_lon":78.0090,
       "closures":[{"lat":30.3046,"lon":78.0153,"radius_m":120,"mode":"block"}]}'
```

## Response (200)

```jsonc
{
  "scenario": "custom",
  "snapped":  { "from": {...}, "to": {...} },      // same as /api/routes
  "affected": { "blocked_edges": 32, "slowed_edges": 0 },
  "closures": [ { "lat": ..., "lon": ..., "radius_m": 120, "mode": "block", "edges_affected": 32 } ],
  "reachable": {
    "baseline": true, "with_closures": true,       // BFS result
    "hops_baseline": 63, "hops_with_closures": 64  // fewest road segments
  },
  "baseline": { "eta_s": 2312.1, "distance_km": 6.07 },  // recommended route, no closures
  "routes":   [ ... ],                              // same shape as /api/routes, with closures applied
  "impact": {
    "recommended_eta_delta_s": ..., "recommended_distance_delta_km": ...,
    "fastest_eta_delta_s": ...                      // fastest route shown, with vs without closures
  }
}
```

- `routes` is `[]` and `message` explains why when `reachable.with_closures` is `false`.
- `impact` is only present when there is a route both before and after.
- Prefer `fastest_eta_delta_s` for "how much slower is my trip": the
  recommended route is chosen by the user's priorities and can change for
  reasons unrelated to the closures, so the `recommended_*` deltas can be negative.

## Errors

| Status | When |
|---|---|
| 400 | Body is not a JSON object, a coordinate or weight is invalid, or `closures` is missing, empty, over 20 entries, outside the box, or has a bad `radius_m` / `mode` / `slow_factor` |
| 429 | More than 10 requests in one second (limiter shared with `/api/routes`); retry after 1 s |
| 422 | A click is more than 500 m from any road, or start and end snap to the same junction |
| 501 | Routing engine not connected |
| 503 | Road network not loaded |

## How it works

1. `applyClosures` marks affected edges (blocked flag and a travel-time factor per edge).
2. `bfsReachable` runs twice (with and without the blocked edges) for the
   reachability check and the hop counts.
3. `OsmRouteProvider::compute` generates candidates with the **penalty method**
   on the scenario costs (`scenarioCosts`: travel-time terms scaled by the
   slowdown factor), never using blocked edges. Yen's algorithm is skipped
   while closures are active because it works on the unmodified graph.
4. The usual filters (detour <= 1.5x, overlap < 0.85, at most 3 routes) and
   ranking apply.

## Relation to the traffic scenarios

`/api/routes?scenario=rain|rush_hour|incident` (traffic module,
`src/traffic/scenario.*`) changes the **speeds** in the traffic state.
`/api/scenario` (this document) changes the **routing**: it removes or slows the
roads around points the user picks and re-runs the route search. The two are
independent and not combined yet, so a closure here does not use the rain or
rush-hour speeds.

## Limits worth knowing

- The no-closure baseline comes from the same 5-minute route cache as
  `/api/routes`. The first request for a start/end pair takes about 3 s
  (Yen's algorithm is the slow part); after that, changing only the closures
  takes about 70 ms.

- A heavily slowed road can disappear from `routes` instead of showing a long
  ETA: the penalty search drops routes costing more than 1.5x the cheapest
  (`PenaltyOptions::maxStretch`), which is usually what a user wants but means
  "slow this road" can read as "route no longer offered".
- Closure matching scans every edge once per closure (about 73,000 edges), so
  20 large closures cost a few milliseconds more than a plain route request.
- Closures only change this request; nothing is stored on the server.
- Free-flow times only: live TomTom speeds are not combined with scenarios yet.
