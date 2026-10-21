#!/usr/bin/env python3
"""
geocode_junctions.py - fills in coordinates for junctions in
data/junctions/major_junctions.csv that have none yet (the 9 reported in
Aug 2025), using OpenStreetMap's Nominatim search.

* Rows that already have coordinates are never changed.
* Several spellings are tried per name ("X Chowk, Dehradun", "X, Dehradun", ...).
* Every filled row records the exact query and the place Nominatim matched,
  so the result can be checked; confidence is "low" until someone verifies it.
* Rows outside the road network's box, or far from any road node, are flagged.
* Max 1 request per second; your email identifies the project (OSM policy).

Usage (from the project root):
  python3 preprocess/geocode_junctions.py --email you@example.com
"""
import argparse
import csv
import json
import math
import os
import re
import sys
import time
import urllib.parse
import urllib.request

NETWORK_BOX = (77.90, 30.20, 78.15, 30.45)      # west, south, east, north (data/osm export)
SEARCH_BOX = "77.75,30.50,78.25,30.15"          # Nominatim viewbox: a bit wider than the city


def haversine_m(a, b):
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 2 * 6371000 * math.asin(math.sqrt(h))


def query_variants(name):
    base = re.sub(r"\s+(Chowk|Tiraha|Junction)$", "", name, flags=re.I).strip()
    out = [f"{name}, Dehradun", f"{base} Chowk, Dehradun", f"{base}, Dehradun", base]
    seen, uniq = set(), []
    for q in out:
        if q.lower() not in seen:
            seen.add(q.lower())
            uniq.append(q)
    return uniq


def nominatim_search(q, ua, fetch=None):
    params = urllib.parse.urlencode({"q": q, "format": "jsonv2", "limit": 1, "countrycodes": "in",
                                     "viewbox": SEARCH_BOX, "bounded": 1})
    url = "https://nominatim.openstreetmap.org/search?" + params
    if fetch:
        return fetch(url)
    req = urllib.request.Request(url, headers={"User-Agent": ua})
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.load(r)


def load_nodes(path):
    pts = []
    if os.path.exists(path):
        with open(path, newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                pts.append((float(row["lat"]), float(row["lon"])))
    return pts


def nearest_node_m(p, nodes):
    best = float("inf")
    for n in nodes:
        if abs(n[0] - p[0]) < 0.01 and abs(n[1] - p[1]) < 0.01:      # cheap pre-filter (~1 km)
            best = min(best, haversine_m(p, n))
    return best


def main(argv=None, fetch=None, sleep=time.sleep):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--email", required=True, help="your email (Nominatim usage policy)")
    ap.add_argument("--file", default="data/junctions/major_junctions.csv")
    ap.add_argument("--nodes", default="data/osm/nodes.csv")
    args = ap.parse_args(argv)
    ua = f"RouteLens/1.0 (Graphic Era student project; {args.email})"

    with open(args.file, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        fields = reader.fieldnames
        rows = list(reader)
    nodes = load_nodes(args.nodes)
    pending = [r for r in rows if not r["lat"].strip()]
    print(f"{len(rows)} junctions, {len(pending)} without coordinates")

    last = 0.0
    for r in pending:
        found = None
        for q in query_variants(r["name"]):
            wait = 1.1 - (time.time() - last)
            if wait > 0:
                sleep(wait)
            last = time.time()
            try:
                res = nominatim_search(q, ua, fetch)
            except Exception as e:
                print(f"  {r['name']}: lookup failed ({type(e).__name__})")
                res = []
            # Accept only junction-like matches; a road, area or building gives the wrong point.
            hit = res[0] if res else None
            if hit and (hit.get("type") in ("junction", "traffic_signals", "crossing")
                        or "chowk" in hit.get("name", "").lower() or "tiraha" in hit.get("name", "").lower()):
                found = (q, hit)
                break
            if hit:
                print(f"  {r['name']}: '{q}' matched {hit.get('category')}/{hit.get('type')} - not a junction, skipped")
        if not found:
            print(f"  {r['name']:<24} NOT FOUND - stays pending (add coordinates by hand if needed)")
            continue
        q, hit = found
        lat, lon = float(hit["lat"]), float(hit["lon"])
        place = ", ".join(hit.get("display_name", "").split(", ")[:3])
        r["lat"], r["lon"] = f"{lat:.6f}", f"{lon:.6f}"
        r["confidence"] = "low"
        r["coord_source"] = f"Nominatim search '{q}' -> {place} ({hit.get('category', '')}/{hit.get('type', '')})"
        w, s, e, n = NETWORK_BOX
        inside = w <= lon <= e and s <= lat <= n
        r["status"] = "reported_2025" if inside else "reported_2025_outside_network"
        near = nearest_node_m((lat, lon), nodes) if nodes and inside else float("inf")
        flag = "" if inside else "  OUTSIDE road network (ignored for routing)"
        if inside and near > 150:
            dist = "more than 1 km" if near == float("inf") else f"{near:.0f} m"
            flag = f"  WARNING: nearest road node {dist} away - check this one"
        print(f"  {r['name']:<24} {lat:.5f}, {lon:.5f}  <- {place}{flag}")

    with open(args.file, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)
    done = sum(1 for r in rows if r["lat"].strip())
    print(f"Saved {args.file}: {done} of {len(rows)} junctions have coordinates")


if __name__ == "__main__":
    main()
