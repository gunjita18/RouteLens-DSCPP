#!/usr/bin/env python3
"""
find_junctions.py - candidate "major junctions" from the OSM road network.

OpenStreetMap has only 6 traffic signals mapped in Dehradun, so signal counts
are meaningless. Instead we count MAJOR JUNCTIONS: places where traffic is
usually held up (signal or traffic police). This script finds candidates
automatically; a person then confirms them.

Rule:
  1. Nodes where at least 3 tertiary-or-bigger roads meet are merged with
     neighbours within --merge-m metres (divided roads give several nodes per
     junction).
  2. The roads leaving each merged junction are grouped by DIRECTION (arms
     within --angle degrees count as one direction). A junction needs roads in
     at least 3 directions, at least 2 of them secondary-or-bigger
     (motorway/trunk/primary/secondary). This rejects the points where a divided
     road splits into two carriageways and median openings: those only have
     2 directions.
  Score = sum over directions of the biggest road in that direction
  (trunk/primary 3, secondary 2, tertiary 1), so the biggest junctions rank first.

Outputs (in data/osm/):
  junction_candidates.csv   rank,lat,lon,score,arms,nodes_merged,osm_signal,name,confirmed,notes
  junction_candidates.html  map with numbered markers, to check candidates visually

Names come from Nominatim reverse geocoding (max 1 request per second, cached in
data/cache/junction_names.json). Give your email with --email (OSM usage policy).

Usage (from the project root):
  python3 preprocess/find_junctions.py --email you@example.com
  python3 preprocess/find_junctions.py --no-names          (offline, no names)
"""
import argparse
import csv
import json
import math
import os
import sys
import time
import urllib.parse
import urllib.request
from collections import defaultdict

WEIGHT = {"motorway": 3, "trunk": 3, "primary": 3, "secondary": 2, "tertiary": 1}


def road_weight(highway):
    base = highway[:-5] if highway.endswith("_link") else highway
    return WEIGHT.get(base, 0), base


def haversine_m(a, b):
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 2 * 6371000 * math.asin(math.sqrt(h))


def bearing_deg(a, b):
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    y = math.sin(lo2 - lo1) * math.cos(la2)
    x = math.cos(la1) * math.sin(la2) - math.sin(la1) * math.cos(la2) * math.cos(lo2 - lo1)
    return (math.degrees(math.atan2(y, x)) + 360.0) % 360.0


def parse_geometry(text):
    pts = []
    for p in text.split(";"):
        lon, lat = p.split()
        pts.append((float(lat), float(lon)))
    return pts


def load_network(osm_dir):
    nodes = {}
    with open(os.path.join(osm_dir, "nodes.csv"), newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            nodes[int(row["osm_id"])] = (float(row["lat"]), float(row["lon"]), row["is_signal"] == "1")
    # node -> {neighbour: (weight, class, bearing of the road as it leaves the node)}
    arms = defaultdict(dict)
    with open(os.path.join(osm_dir, "edges.csv"), newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            u, v = int(row["u"]), int(row["v"])
            if u == v or u not in nodes or v not in nodes:
                continue
            w, cls = road_weight(row["highway"])
            try:
                g = parse_geometry(row["geometry"])
            except ValueError:
                g = []
            if len(g) < 2:
                g = [nodes[u][:2], nodes[v][:2]]
            # direction of the road where it leaves u, and where it leaves v
            for a, b, brg in ((u, v, bearing_deg(g[0], g[1])), (v, u, bearing_deg(g[-1], g[-2]))):
                if w > arms[a].get(b, (-1, "", 0.0))[0]:
                    arms[a][b] = (w, cls, brg)
    return nodes, arms


def candidate_nodes(arms):
    """Loose first pass: at least 3 tertiary+ arms, at least 2 of them secondary+."""
    out = set()
    for n, nb in arms.items():
        weights = [w for w, _, _ in nb.values()]
        if sum(1 for w in weights if w >= 1) >= 3 and sum(1 for w in weights if w >= 2) >= 2:
            out.add(n)
    return out


def angle_diff(a, b):
    d = abs(a - b) % 360.0
    return 360.0 - d if d > 180.0 else d


def directions(arm_list, max_angle):
    """Groups arms (weight, class, bearing) into directions; returns [(weight, class)] per direction."""
    groups = []   # [first_bearing, best_weight, best_class]
    for w, cls, brg in sorted(arm_list, key=lambda t: -t[0]):   # biggest roads define directions first
        for g in groups:
            if angle_diff(g[0], brg) <= max_angle:
                break
        else:
            groups.append([brg, w, cls])
    return [(g[1], g[2]) for g in groups]


def cluster(cands, nodes, arms, merge_m, max_angle):
    """Union-find over candidate nodes closer than merge_m metres, then the direction rule."""
    ids = sorted(cands)
    parent = {n: n for n in ids}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    cell = merge_m / 111000.0          # grid cell in degrees (latitude)
    grid = defaultdict(list)
    for n in ids:
        lat, lon, _ = nodes[n]
        grid[(int(lat / cell), int(lon / cell))].append(n)
    for (gy, gx), members in grid.items():
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                for a in members:
                    for b in grid.get((gy + dy, gx + dx), []):
                        if a < b and haversine_m(nodes[a][:2], nodes[b][:2]) <= merge_m:
                            parent[find(a)] = find(b)
    groups = defaultdict(list)
    for n in ids:
        groups[find(n)].append(n)

    junctions, rejected = [], 0
    order = ["motorway", "trunk", "primary", "secondary", "tertiary"]
    for members in groups.values():
        inside = set(members)
        external = [arm for n in members for nb, arm in arms[n].items() if nb not in inside and arm[0] >= 1]
        dirs = directions(external, max_angle)
        if sum(1 for w, _ in dirs) < 3 or sum(1 for w, _ in dirs if w >= 2) < 2:
            rejected += 1
            continue
        counts = defaultdict(int)
        for _, cls in dirs:
            counts[cls] += 1
        lat = sum(nodes[n][0] for n in members) / len(members)
        lon = sum(nodes[n][1] for n in members) / len(members)
        junctions.append({"lat": lat, "lon": lon, "score": sum(w for w, _ in dirs),
                          "arms": ", ".join(f"{c} x{counts[c]}" for c in order if counts[c]),
                          "nodes_merged": len(members)})
    junctions.sort(key=lambda j: (-j["score"], -j["nodes_merged"], j["lat"]))
    return junctions, rejected


def mark_osm_signals(junctions, nodes, radius_m=50.0):
    signals = [(lat, lon) for lat, lon, s in nodes.values() if s]
    for j in junctions:
        j["osm_signal"] = int(any(haversine_m((j["lat"], j["lon"]), s) <= radius_m for s in signals))
    return len(signals)


def name_junctions(junctions, email, top, cache_path):
    cache = {}
    if os.path.exists(cache_path):
        with open(cache_path, encoding="utf-8") as f:
            cache = json.load(f)
    ua = f"RouteLens/1.0 (Graphic Era student project; {email})"
    todo = junctions if top <= 0 else junctions[:top]
    fresh = sum(1 for j in todo if f"{j['lat']:.5f},{j['lon']:.5f}" not in cache)
    print(f"[3/4] Naming {len(todo)} junctions ({fresh} new lookups, about {fresh * 1.1:.0f} s)")
    last = 0.0
    for i, j in enumerate(todo, 1):
        key = f"{j['lat']:.5f},{j['lon']:.5f}"
        if key not in cache:
            wait = 1.1 - (time.time() - last)        # Nominatim policy: max 1 request/second
            if wait > 0:
                time.sleep(wait)
            last = time.time()
            q = urllib.parse.urlencode({"format": "jsonv2", "lat": j["lat"], "lon": j["lon"],
                                        "zoom": 17, "addressdetails": 1})
            req = urllib.request.Request("https://nominatim.openstreetmap.org/reverse?" + q,
                                         headers={"User-Agent": ua})
            try:
                with urllib.request.urlopen(req, timeout=20) as r:
                    d = json.load(r)
                a = d.get("address", {})
                area = a.get("neighbourhood") or a.get("suburb") or a.get("city_district") or a.get("village") or ""
                cache[key] = ", ".join(p for p in (a.get("road", ""), area) if p)
            except Exception as e:
                print(f"  lookup {i} failed ({type(e).__name__}); left unnamed")
                cache[key] = ""
            if i % 20 == 0:
                print(f"  {i}/{len(todo)}")
        j["name"] = cache[key]
    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
    with open(cache_path, "w", encoding="utf-8") as f:
        json.dump(cache, f, indent=1, ensure_ascii=False)


def write_csv(junctions, path):
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["rank", "lat", "lon", "score", "arms", "nodes_merged", "osm_signal", "name", "confirmed", "notes"])
        for i, j in enumerate(junctions, 1):
            w.writerow([i, f"{j['lat']:.6f}", f"{j['lon']:.6f}", j["score"], j["arms"], j["nodes_merged"],
                        j["osm_signal"], j.get("name", ""), "", ""])


def write_map(junctions, path):
    pts = [{"r": i, "lat": round(j["lat"], 6), "lon": round(j["lon"], 6), "s": j["score"], "a": j["arms"],
            "n": j.get("name", ""), "sig": j["osm_signal"]} for i, j in enumerate(junctions, 1)]
    html = """<!doctype html><html><head><meta charset="utf-8">
<title>RouteLens - major junction candidates</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/leaflet.min.css">
<script src="https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/leaflet.min.js"></script>
<style>html,body,#map{height:100%;margin:0;font-family:system-ui,sans-serif}
.num{background:#c0392b;color:#fff;border-radius:50%;width:22px;height:22px;line-height:22px;
text-align:center;font-size:11px;font-weight:600;border:2px solid #fff;box-shadow:0 0 2px #000}
.num.mid{background:#e67e22}.num.low{background:#7f8c8d}.num.sig{background:#27ae60}
#legend{position:absolute;bottom:12px;left:12px;z-index:1000;background:#fff;padding:8px 10px;
border-radius:6px;font-size:12px;box-shadow:0 0 4px rgba(0,0,0,.3)}</style></head><body>
<div id="map"></div><div id="legend"><b>Junction candidates</b> (number = rank)<br>
<span style="color:#c0392b">&#9679;</span> score 9+ &nbsp;<span style="color:#e67e22">&#9679;</span> 7-8
&nbsp;<span style="color:#7f8c8d">&#9679;</span> below 7 &nbsp;<span style="color:#27ae60">&#9679;</span> signal mapped in OSM</div>
<script>
const P = __DATA__;
const map = L.map('map');
L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {maxZoom: 19,
  attribution: '&copy; OpenStreetMap contributors'}).addTo(map);
const b = [];
for (const p of P) {
  const cls = p.sig ? 'sig' : (p.s >= 9 ? '' : (p.s >= 7 ? 'mid' : 'low'));
  L.marker([p.lat, p.lon], {icon: L.divIcon({className: '', html: '<div class="num ' + cls + '">' + p.r + '</div>',
    iconSize: [26, 26], iconAnchor: [13, 13]})}).addTo(map)
   .bindPopup('<b>#' + p.r + ' ' + (p.n || '(unnamed)') + '</b><br>score ' + p.s + '<br>' + p.a +
              (p.sig ? '<br>signal mapped in OSM' : '') + '<br>' + p.lat + ', ' + p.lon +
              '<br><a target="_blank" href="https://www.openstreetmap.org/?mlat=' + p.lat + '&mlon=' + p.lon +
              '#map=18/' + p.lat + '/' + p.lon + '">open in OpenStreetMap</a>');
  b.push([p.lat, p.lon]);
}
if (b.length) map.fitBounds(b); else map.setView([30.32, 78.03], 13);
</script></body></html>"""
    with open(path, "w", encoding="utf-8") as f:
        f.write(html.replace("__DATA__", json.dumps(pts, ensure_ascii=False)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--osm-dir", default="data/osm")
    ap.add_argument("--merge-m", type=float, default=40.0, help="merge nodes closer than this (metres)")
    ap.add_argument("--angle", type=float, default=35.0, help="arms within this angle are one direction (degrees)")
    ap.add_argument("--email", help="your email, required for naming (Nominatim usage policy)")
    ap.add_argument("--top", type=int, default=80, help="name only the N highest-ranked (0 = all)")
    ap.add_argument("--no-names", action="store_true", help="skip naming (offline)")
    args = ap.parse_args()
    if not args.no_names and not args.email:
        sys.exit("error: give --email you@example.com for naming, or use --no-names")

    print(f"[1/4] Loading {args.osm_dir}/nodes.csv and edges.csv")
    nodes, arms = load_network(args.osm_dir)
    cands = candidate_nodes(arms)
    print(f"  {len(nodes)} nodes, {len(cands)} candidate nodes")

    print(f"[2/4] Merging nodes within {args.merge_m:.0f} m, then the direction rule ({args.angle:.0f} deg)")
    junctions, rejected = cluster(cands, nodes, arms, args.merge_m, args.angle)
    n_sig = mark_osm_signals(junctions, nodes)
    print(f"  {rejected} rejected (only 2 directions: carriageway splits, median openings)")
    print(f"  {len(junctions)} junctions; {sum(j['osm_signal'] for j in junctions)} of them near one of the "
          f"{n_sig} OSM-mapped signals")
    by_score = defaultdict(int)
    for j in junctions:
        by_score["9+" if j["score"] >= 9 else ("7-8" if j["score"] >= 7 else "below 7")] += 1
    print(f"  by score: 9+ = {by_score['9+']}, 7-8 = {by_score['7-8']}, below 7 = {by_score['below 7']}")

    if args.no_names:
        print("[3/4] Naming skipped (--no-names)")
    else:
        name_junctions(junctions, args.email, args.top, "data/cache/junction_names.json")

    csv_path = os.path.join(args.osm_dir, "junction_candidates.csv")
    map_path = os.path.join(args.osm_dir, "junction_candidates.html")
    write_csv(junctions, csv_path)
    write_map(junctions, map_path)
    print(f"[4/4] Wrote {csv_path} and {map_path}")
    print("Top 10:")
    for i, j in enumerate(junctions[:10], 1):
        print(f"  {i:2}. score {j['score']:2}  {j['arms']:<32} {j.get('name', '')}")


if __name__ == "__main__":
    main()
    