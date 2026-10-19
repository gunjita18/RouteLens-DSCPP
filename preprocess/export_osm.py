#!/usr/bin/env python3
"""
export_osm.py - export Dehradun's drivable road network from OpenStreetMap
for the RouteLens C++ loader.

Outputs (in data/osm/ by default):
  nodes.csv  osm_id,lat,lon,is_signal
  edges.csv  u,v,length_m,highway,maxspeed_kph,oneway,geometry
  meta.txt   download date, versions, bounding box and validation numbers

The graph is deliberately NOT reduced to its largest component here: that is
done in C++ with Tarjan's SCC algorithm. meta.txt records the expected SCC
numbers (computed with NetworkX) so the C++ result can be checked against them.

Usage (from the project root):
  python3 preprocess/export_osm.py
  python3 preprocess/export_osm.py --bbox 77.90,30.20,78.15,30.45 --out data/osm

Map data (c) OpenStreetMap contributors, available under the ODbL.
"""
import argparse
import csv
import datetime
import os
import re
import sys

import networkx as nx
import osmnx as ox

DEFAULT_BBOX = (77.90, 30.20, 78.15, 30.45)  # west, south, east, north


def read_config(path):
    """Read key = value lines from config.txt, ignoring comments."""
    cfg = {}
    if not os.path.exists(path):
        return cfg
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = re.split(r"\s#", line, maxsplit=1)[0].strip()  # drop inline comments
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            cfg[key.strip()] = value.strip()
    return cfg


def bbox_from_viewbox(text):
    """config's nominatim_viewbox is lon1,lat1,lon2,lat2 in any corner order."""
    lon1, lat1, lon2, lat2 = (float(x) for x in text.split(","))
    return (min(lon1, lon2), min(lat1, lat2), max(lon1, lon2), max(lat1, lat2))


def first(value):
    """OSMnx stores a list when merged OSM ways disagree; keep the first value."""
    if isinstance(value, list):
        return value[0] if value else ""
    return "" if value is None else value


def parse_maxspeed_kph(value):
    """'40', '40 mph', ['40', '50'] -> km/h as float; None if unknown.
    For a list (merged ways with different limits) the lowest limit is used,
    which is the conservative choice for travel-time estimates."""
    values = value if isinstance(value, list) else [value]
    speeds = []
    for v in values:
        if v is None:
            continue
        m = re.match(r"\s*(\d+(?:\.\d+)?)\s*(mph)?", str(v))
        if m:
            kph = float(m.group(1)) * (1.609344 if m.group(2) else 1.0)
            speeds.append(kph)
    return min(speeds) if speeds else None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", default="config.txt")
    ap.add_argument("--bbox", help="west,south,east,north (default: nominatim_viewbox from config)")
    ap.add_argument("--out", default="data/osm")
    args = ap.parse_args()

    cfg = read_config(args.config)
    if args.bbox:
        west, south, east, north = (float(x) for x in args.bbox.split(","))
        bbox = (west, south, east, north)
    elif cfg.get("nominatim_viewbox"):
        bbox = bbox_from_viewbox(cfg["nominatim_viewbox"])
    else:
        bbox = DEFAULT_BBOX

    user_agent = cfg.get("user_agent", "")
    if not user_agent or "CHANGE_ME" in user_agent:
        sys.exit("error: set user_agent in config.txt (the OSM usage policy requires it)")
    ox.settings.http_user_agent = user_agent
    ox.settings.cache_folder = "data/cache/osmnx"
    ox.settings.log_console = False
    os.makedirs(args.out, exist_ok=True)

    # ---- 1. download the full, unsimplified drivable network
    print(f"[1/4] Downloading drivable roads in bbox W{bbox[0]} S{bbox[1]} E{bbox[2]} N{bbox[3]}")
    print("      (first run can take a few minutes; later runs use data/cache/osmnx)")
    G = ox.graph_from_bbox(bbox, network_type="drive", simplify=False, retain_all=True)
    print(f"  raw graph: {G.number_of_nodes()} nodes, {G.number_of_edges()} directed edges")

    # Mark traffic signals so that simplification keeps them as graph nodes
    # (otherwise a signal in the middle of a road would be merged away).
    raw_signals = 0
    for _, d in G.nodes(data=True):
        if d.get("highway") == "traffic_signals":
            d["rl_signal"] = True
            raw_signals += 1
    print(f"  traffic signals tagged in OSM: {raw_signals}")

    # ---- 2. simplify: merge chains of degree-2 nodes into single edges
    print("[2/4] Simplifying graph (signal nodes are kept)")
    try:
        G = ox.simplify_graph(G, node_attrs_include=["rl_signal"])
    except TypeError:
        sys.exit(f"error: OSMnx {ox.__version__} does not accept node_attrs_include; "
                 "report this version so the script can be adapted")
    print(f"  simplified graph: {G.number_of_nodes()} nodes, {G.number_of_edges()} directed edges")

    # ---- 3. connectivity numbers the C++ Tarjan implementation must reproduce
    print("[3/4] Connectivity check (expected values for the C++ SCC test)")
    wcc_count = nx.number_weakly_connected_components(G)
    sccs = list(nx.strongly_connected_components(G))
    largest_scc = max(len(c) for c in sccs) if sccs else 0
    print(f"  weakly connected components:   {wcc_count}")
    print(f"  strongly connected components: {len(sccs)}")
    print(f"  largest SCC: {largest_scc} of {G.number_of_nodes()} nodes "
          f"({100.0 * largest_scc / max(1, G.number_of_nodes()):.1f}%)")

    # ---- 4. write CSV files
    print(f"[4/4] Writing CSV files to {args.out}/")
    nodes_path = os.path.join(args.out, "nodes.csv")
    edges_path = os.path.join(args.out, "edges.csv")

    signal_nodes = 0
    with open(nodes_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["osm_id", "lat", "lon", "is_signal"])
        for n, d in G.nodes(data=True):
            is_signal = 1 if d.get("rl_signal") else 0
            signal_nodes += is_signal
            w.writerow([n, f"{d['y']:.7f}", f"{d['x']:.7f}", is_signal])

    missing_speed = 0
    with open(edges_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["u", "v", "length_m", "highway", "maxspeed_kph", "oneway", "geometry"])
        for u, v, d in G.edges(data=True):
            kph = parse_maxspeed_kph(d.get("maxspeed"))
            if kph is None:
                missing_speed += 1
            if "geometry" in d:
                coords = list(d["geometry"].coords)  # ordered from u to v
            else:
                coords = [(G.nodes[u]["x"], G.nodes[u]["y"]),
                          (G.nodes[v]["x"], G.nodes[v]["y"])]
            geometry = ";".join(f"{x:.6f} {y:.6f}" for x, y in coords)  # "lon lat;lon lat;..."
            w.writerow([
                u,
                v,
                f"{float(d['length']):.2f}",
                first(d.get("highway")),
                "" if kph is None else f"{kph:g}",
                1 if first(d.get("oneway")) is True else 0,
                geometry,
            ])

    edge_count = G.number_of_edges()
    meta = [
        f"generated_utc = {datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds')}",
        f"python_version = {sys.version.split()[0]}",
        f"osmnx_version = {ox.__version__}",
        f"networkx_version = {nx.__version__}",
        f"bbox_west_south_east_north = {bbox[0]},{bbox[1]},{bbox[2]},{bbox[3]}",
        f"network_type = drive",
        f"nodes = {G.number_of_nodes()}",
        f"edges = {edge_count}",
        f"signal_nodes = {signal_nodes}",
        f"weakly_connected_components = {wcc_count}",
        f"strongly_connected_components = {len(sccs)}",
        f"largest_scc_nodes = {largest_scc}",
        f"edges_without_maxspeed = {missing_speed}",
        "attribution = Map data (c) OpenStreetMap contributors, ODbL",
    ]
    with open(os.path.join(args.out, "meta.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(meta) + "\n")

    print(f"  {nodes_path}: {G.number_of_nodes()} nodes ({signal_nodes} signals)")
    print(f"  {edges_path}: {edge_count} edges")
    print(f"  edges without a maxspeed tag: {missing_speed} "
          f"({100.0 * missing_speed / max(1, edge_count):.1f}%) - the C++ loader needs a "
          "default speed per road class for these")
    print("Done.")


if __name__ == "__main__":
    main()
