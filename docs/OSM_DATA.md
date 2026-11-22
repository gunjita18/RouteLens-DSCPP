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
| `src/core/hashtable.*`, `list.*`, `dynarray.*`, `queue.*` | Hand-written C data structures used by the loader (see below) |
| `src/core/bfs.h`, `bfs.c` | Forward and reverse breadth-first search; graph cleaning |
| `src/data/osm_loader.hpp`, `.cpp` | Loads the CSVs into a `Graph`: C hash table for ID mapping, BFS cleaning, free-flow times |
| `src/app/osm_info_main.cpp` | Prints load statistics and checks the cleaning result against NetworkX |
| `tests/test_osm.cpp`, `test_bfs.c`, `test_hashtable.c`, ... | Unit tests (one file per structure, plus the loader) |

Build and check (run from the repository root):

```bash
cmake -S . -B build && cmake --build build -j
(cd build && ctest --output-on-failure)
./build/routelens_osm_info          # prints statistics and "CHECK PASS" against meta.txt
```

## Graph cleaning (forward + reverse BFS)

A route must exist between any two nodes, so nodes that cannot be reached, or
from which the main network cannot be reached (dead-end one-way stubs), are
removed. The loader does this with two breadth-first searches:

1. Pick a centre node: the node nearest Clock Tower (`OsmLoadOptions::centre`).
2. BFS along the roads from the centre: mark every node it can reach.
3. BFS along the reversed roads: mark every node that can reach it.
4. Keep the nodes marked by both. They are exactly the centre's strongly
   connected component; every other node and its edges are dropped.

Each BFS is O(V + E) and uses the circular queue in `queue.c`. If the centre lies on
an isolated stub, the component found is small. The loader then tries the next
nearest nodes (at most 20) and keeps the largest result. It stops as soon as one
component holds more than half of all nodes, because two disjoint components cannot
both be that large.

The OSM ID to node index map is the C hash table in `hashtable.c` (multiplicative
hashing, separate chaining, rehash when the load factor exceeds 0.75), wrapped in
`rl::IdIndex`, which frees it automatically.

## Current network (export of 2026-10-18)

- Bounding box: west 77.90, south 30.20, east 78.15, north 30.45
- Exported: 32,110 nodes, 73,165 directed edges
- After loading (strongly connected core): **31,802 nodes, 72,626 edges**
- Verified: the BFS cleaning keeps exactly the node count of NetworkX's largest strongly
  connected component (31,802, recorded in `meta.txt`); `test_osm` and `routelens_osm_info` check it
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
- `net.indexOf.find(osmId)`: OSM ID to node index, or -1 if the ID is not in the kept network
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
