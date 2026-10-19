# RouteLens road network: data format and graph interface

Owner: Sneha Rawat (map data and traffic). This package contains the OSM road
network pipeline and the graph structure that the routing algorithms consume.
It does **not** contain any routing algorithm.

## Contents

| Path | Purpose |
|---|---|
| `preprocess/export_osm.py` | Downloads Dehradun's drivable roads with OSMnx and writes the CSVs |
| `data/osm/nodes.csv`, `edges.csv`, `meta.txt` | The exported network (CSVs are not in Git; see below) |
| `src/core/graph.h`, `graph.c` | Directed graph (forward-star adjacency list), the shared interface |
| `src/data/osm_loader.hpp`, `.cpp` | Loads the CSVs into a `Graph`: hash-map ID mapping, iterative Tarjan SCC, free-flow times |
| `src/app/osm_info_main.cpp` | Prints load statistics and checks the SCC result against NetworkX |
| `tests/test_osm.cpp` | Unit tests (Tarjan, speed rules, loader) |

Build and check (run from this folder):

```bash
make test                     # 48 checks, 0 failures
make && ./bin/routelens_osm_info
```

## Current network (export of 2026-10-18)

- Bounding box: west 77.90, south 30.20, east 78.15, north 30.45
- Exported: 32,110 nodes, 73,165 directed edges
- After loading (largest strongly connected component): **31,802 nodes, 72,626 edges**
- Verified: the C++ Tarjan result matches NetworkX exactly (89 SCCs, largest 31,802)
- Known data gaps: only 6 traffic signals are mapped in OSM; 98% of edges have no
  `maxspeed` tag, so free-flow times use provisional defaults (below)

To regenerate the CSVs instead of copying them: `python3 preprocess/export_osm.py`
(needs `osmnx` and a `user_agent` with your email in `config.txt`). OSM changes
over time, so a new export can differ slightly; compare against `meta.txt`.

## CSV format

`nodes.csv`: `osm_id,lat,lon,is_signal` (`is_signal` is 1 if the node is tagged
`highway=traffic_signals`).

`edges.csv`: `u,v,length_m,highway,maxspeed_kph,oneway,geometry`

- `u`, `v`: OSM node IDs; every row is one **directed** edge (two-way roads appear twice)
- `length_m`: road length in metres along the real road shape
- `highway`: OSM road class (`primary`, `residential`, ...)
- `maxspeed_kph`: tagged limit in km/h, empty if untagged
- `geometry`: road shape as `lon lat;lon lat;...`, ordered from `u` to `v`

## What the loader gives the routing code

`loadOsmNetwork("data/osm", net, err)` fills an `rl::OsmNetwork`:

- `net.graph`: a `Graph*` with nodes `0..node_count-1`. Every node can reach every other node.
- `net.osmId[i]`, `net.pos[i]`, `net.isSignal[i]`: per node
- `net.indexOf`: OSM ID to node index (hash map)
- `net.geometry[e]`, `net.roadClass[e]`: per edge, same index as `graph->edges[e]`

Each `Edge` in the graph has:

- `distance_m`: length in metres
- `duration_s`: **free-flow** travel time in seconds = length / speed
- `traffic_index`: 0.0 for now; will hold live congestion (0 = free, 1 = heavy) once the TomTom client exists
- `signal_ids` / `signal_count`: the signal at the edge's **destination** node, if any. A signal
  is counted once, on the edge that arrives at it.

Edge costs for a search come from `graph_compute_costs(graph, &weights, costs)`, a
non-negative weighted sum of km, minutes, congested minutes and signals, so
Dijkstra, A* and Yen's algorithm stay correct for any user priorities.

For A*: `net.pos` gives every node's coordinates for the haversine heuristic. If the
cost includes time, divide the straight-line distance by the **highest** speed used
in the graph so that the heuristic never overestimates.

## Provisional free-flow speeds (km/h)

Used only when an edge has no `maxspeed` tag. These are assumptions to be calibrated
in the ETA study, not measured values.

| Class | km/h | Class | km/h |
|---|---|---|---|
| motorway | 60 | tertiary | 30 |
| trunk | 50 | unclassified | 25 |
| primary | 40 | residential, road | 20 |
| secondary | 35 | service | 15 |
| *_link | 25–40 | living_street | 10 |

Unknown classes use 20 km/h.
