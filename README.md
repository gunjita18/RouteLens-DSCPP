# RouteLens — Live-Traffic Multiple Routes Visualization and Optimization System

RouteLens helps commuters in **Dehradun** compare several routes between two points on an interactive map. Instead of a single recommended route, it shows up to three alternatives together, each in its own colour, with an information panel: estimated time of arrival (ETA), distance, number of major junctions, traffic level (Low, Medium or High) and how much of the route is covered by traffic data. The recommended route follows the priorities the user sets (time, distance, junctions, traffic).

The road network is the real one, extracted from OpenStreetMap. Traffic speeds come from a **TomTom Traffic Flow snapshot saved before the demo**, so the application runs offline and shows when the data was taken; roads without snapshot data use free-flow speed estimates, and the page says so.

Users can also try what-if situations: rain, rush hour, an incident, or closures and slowdowns placed on the map, checking that the destination is still reachable.

## How it works

```
Prepare once (Python)          C core (hand-written C11)            C++17 layer              Server and page
─────────────────────          ─────────────────────────            ───────────             ────────────────
export_osm.py   ──► CSVs ──►   hash table, queue, BFS cleaning  ──► RoadNetwork loader  ──►  Crow REST API
fetch_traffic.py ─► snapshot ► snapshot reader, union-find,         TrafficProvider,         Leaflet map,
                               min-heap, stack, merge sort,         Scenario classes,        route cards,
                               Dijkstra, penalty method             route provider           comparison chart
```

1. **Data preparation (offline, once).** `preprocess/export_osm.py` writes the road network (nodes, directed edges, shapes) to CSV; `preprocess/fetch_traffic.py` saves TomTom speeds for the main roads to `traffic.csv`.
2. **Loading.** The loader maps OSM ids to graph indices with a hand-written hash table, and cleans the graph with forward and reverse breadth-first search, keeping the part of the network where every node can reach every other (31,802 of 32,110 nodes, the same as NetworkX).
3. **Traffic.** A `TrafficProvider` (snapshot or free-flow) gives each road its measured speed if it has one; a `Scenario` (normal, rain, rush hour, incident) changes speeds for what-if questions.
4. **Routing.** The server generates candidates with the hand-written C penalty method and Dijkstra (`src/core/`); routes are filtered for detours and overlap, then ranked by a weighted score of time, distance, junctions and traffic.
5. **Display.** The browser asks `/api/routes` (or `/api/scenario` for closures) and draws the routes, cards and a comparison chart.

### Data structures and algorithms written by hand (C, `src/core/`)

| Structure or algorithm | File | Used for |
|---|---|---|
| Dynamic array, singly linked list | `dynarray.c`, `list.c` | route edge lists, hash-table chains |
| Hash table (multiplicative hashing, chaining, rehash at load 0.75) | `hashtable.c` | OSM id to node index, snapshot lookup, edge sets |
| Circular queue, breadth-first search (forward and reverse) | `queue.c`, `bfs.c` | graph cleaning |
| Union-find (rank, path compression) | `dsu.c` | independent check of the network's connected parts (72, as NetworkX) |
| Binary min-heap, stack | `minheap.c`, `stack.c` | Dijkstra's priority queue, rebuilding a route |
| Merge sort (stable), binary search | `sort.c`, `search.c` | ranking, sorted-array lookup |
| Dijkstra, penalty method | `dijkstra.c`, `penalty.c` | cheapest route and alternatives, used by the server |
| Snapshot reader | `traffic_reader.c` | loads `traffic.csv` into the hash table |

The C++ layer (`src/oop/`) wraps these in class hierarchies: `TrafficProvider` (`FreeFlowProvider`, `SnapshotProvider`) and `Scenario` (`NormalScenario`, `RainScenario`, `RushHourScenario`, `IncidentScenario`), with RAII ownership of the C structures.

**The C routing core is what the server runs.** The C Dijkstra agrees with the earlier C++ Dijkstra on 1,200 random pairs of the real network; replacing the earlier route generation (C++ penalty method plus Yen's algorithm) changed the answers little on 120 test questions (details and one case where the fastest route got 6.8 % slower: `docs/C_ROUTING_CORE.md`). The earlier C++ implementations, including Yen's algorithm and A*, are kept with their tests but are not used by the server; A* is used only by the evaluation program (`routelens_eval`).

## Repository layout

| Path | Contents |
|---|---|
| `src/core/` | Hand-written C data structures and algorithms (above) |
| `src/oop/` | C++ class hierarchies: traffic providers, scenarios, `LoadError` |
| `src/data/` | OSM loader (`osm_loader`), geometry helpers |
| `src/traffic/` | Traffic levels, per-edge state, ETA and route summary, major junctions, LRU cache, rate limiter |
| `src/algorithms/` | What-if closures and reachability (used by the server); earlier C++ Dijkstra, penalty method, Yen, A* and Pareto ranking (kept with tests, not used by the server) |
| `src/spatial/` | KD-tree and the road snapper that places clicks on the network |
| `src/server/` | Crow REST API, route provider, request validation, route cache |
| `src/app/` | Command-line tools: `routelens_osm_info`, `routelens_junctions_info`, `routelens_eval`, `routelens_lookup_benchmark` |
| `web/` | The page: Leaflet map, Chart.js comparison, scenario and closure controls |
| `preprocess/` | `export_osm.py`, `fetch_traffic.py`, junction scripts (Python) |
| `evaluation/` | Algorithm evaluation, ETA cross-check and the (not yet run) timed-trip study |
| `data/` | `junctions/major_junctions.csv` (in git); `osm/` and `traffic/` are generated (CSV not in git) |
| `docs/` | Specifications, studies and viva notes (index below) |
| `tests/` | One test file per structure and module; fixtures |
| `scripts/` | `check_memory.sh` (Valgrind) |

## Getting started

**Prerequisites** (Ubuntu or WSL): a C11/C++17 compiler, CMake 3.16+, and the Crow dependencies.

```bash
sudo apt install -y cmake g++ make libasio-dev libssl-dev zlib1g-dev
git clone --recurse-submodules https://github.com/gunjita18/RouteLens-DSCPP.git
cd RouteLens-DSCPP
```

**Build and test**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
(cd build && ctest --output-on-failure)      # 37 tests; a few use data/osm and skip themselves without it
```

**Road network data** (not in git, about 13 MB). Either get `data/osm/nodes.csv` and `edges.csv` from a teammate or generate them (`pip install osmnx networkx`, then `python3 preprocess/export_osm.py`; details and the `config.txt` it needs are in `docs/OSM_DATA.md`).

**Check the data**

```bash
./build/routelens_osm_info        # prints load statistics and "CHECK PASS" against data/osm/meta.txt
```

**Traffic snapshot (optional).** Without `data/traffic/traffic.csv` the server uses free-flow speeds and says "estimated". To create one you need a TomTom API key, read only from the environment and never stored:

```bash
export TOMTOM_API_KEY=...                       # do not commit or paste the key
python3 preprocess/fetch_traffic.py --dry-run   # counts the requests it would make
python3 preprocess/fetch_traffic.py
```

**Run**

```bash
./build/routelens_server        # then open http://127.0.0.1:18080
```

| Environment variable | Default | Meaning |
|---|---|---|
| `ROUTELENS_PORT`, `ROUTELENS_BIND` | `18080`, `127.0.0.1` | where the server listens |
| `ROUTELENS_DATA_DIR` | `data/osm` | folder with `nodes.csv` and `edges.csv` |
| `ROUTELENS_JUNCTIONS_FILE` | `data/junctions/major_junctions.csv` | major junctions |
| `ROUTELENS_TRAFFIC_FILE` | `data/traffic/traffic.csv` | traffic snapshot (optional) |
| `ROUTELENS_WEB_DIR` | `web` | the page |
| `ROUTELENS_PREVIEW=1` | off | serve clearly labelled SAMPLE routes (interface demo only) |

**Docker** (the image contains no data; mount it):

```bash
docker build -t routelens .
docker run -p 18080:18080 -v "$PWD/data/osm:/app/data/osm:ro" -v "$PWD/data/traffic:/app/data/traffic:ro" routelens
```

## Testing and quality

- **37 CTest suites:** one test file per data structure and module, offline, using hand-made fixtures; random graphs are checked against reference implementations (a transitive closure for BFS and union-find, Bellman-Ford for Dijkstra, `qsort` for sorting).
- **Memory safety:** `scripts/check_memory.sh build` runs the 18 tests that own memory under Valgrind; all are clean. AddressSanitizer and UBSan were also run during development.
- **Continuous integration** (GitHub Actions): build and test, web syntax check, Docker build.
- **Verification on real data:** the cleaned network matches NetworkX (31,802 nodes, 72 weakly connected parts); the C Dijkstra matches the C++ Dijkstra on 1,200 random pairs.

## Studies and results

| Study | Result | Document |
|---|---|---|
| Routing algorithms (Phase II) | Dijkstra, A*, penalty and Yen compared on 1,000 pairs | `docs/evaluation_phase2.md` |
| Hash table vs binary search | Hash table 4 to 9 times faster on 2,000 to 1,000,000 keys (one machine) | `docs/LOOKUP_EXPERIMENT.md` |
| ETA cross-check | Route lengths agree with OSRM (median ratio 0.99); implied speeds consistent with published Indian-city figures. **A plausibility check, not accuracy.** | `docs/ETA_CROSS_CHECK.md` |
| ETA accuracy against timed trips | **Not done** (no trips collected); tooling and method are ready | `docs/ETA_STUDY.md` |

## Documentation

| Document | About |
|---|---|
| `docs/OSM_DATA.md` | Road network export, CSV format, graph cleaning, loader interface |
| `docs/TRAFFIC.md` | Traffic module: snapshot, rules, route summary, junctions, scenarios, class hierarchies |
| `docs/C_ROUTING_CORE.md` | C min-heap, stack, merge sort, Dijkstra, penalty method; how they were checked |
| `docs/VIVA_NOTES_DATA_STRUCTURES.md` | Questions and answers on the data structures and traffic part |
| `docs/scenario_api.md`, `docs/routing_changes.md`, `docs/changes_.md` | Earlier API, routing and change notes (some describe the live TomTom client that has since been removed) |
| `docs/reports/` | Phase reports |

## Known limitations

- **Traffic is one saved snapshot.** The first capture (a Sunday morning) shows almost no congestion; a weekday rush-hour snapshot would show more. Only trunk, primary and secondary roads have data, and 23 TomTom segments cover about 2,000 edges, so one speed applies to many roads. Residential roads always use estimated speeds.
- **ETAs are not validated against measured trips.** Road-class speeds (for example 20 km/h on residential roads) and the 30 s wait per major junction are provisional assumptions; the rain and rush-hour factors are literature or provisional values.
- **OpenStreetMap maps only 6 traffic signals in Dehradun.** The documented major junctions are therefore assumed to be signalised for the waiting-time estimate; this is a modelling assumption, not surveyed data.
- **Route generation changed late.** Switching the server to the C routing core and dropping Yen's algorithm slightly changed the answers on a few test questions; see `docs/C_ROUTING_CORE.md`.
- The snapshot may be subject to TomTom's terms on storing their data; it is not committed to the repository.

## Data and attribution

Map data © OpenStreetMap contributors, available under the Open Database Licence (ODbL). Traffic speeds from the TomTom Traffic Flow API. Map display with Leaflet; charts with Chart.js. Junction coordinates are from the UKMRC Comprehensive Mobility Plan annexure (2024), as recorded per row, with its source, in `data/junctions/major_junctions.csv`.

## Team and AI assistance

RouteLens is a Graphic Era PBL project by Sneha (map data, traffic, hand-written C structures), Gunjita (routing) and Aayush (server and website). Parts of the code and documentation were written with the help of an AI assistant (Claude); the pull request descriptions state where, and everyone presenting the project is expected to be able to explain their part without notes.
