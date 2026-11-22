#!/usr/bin/env python3
"""
eta_reference.py - cross-check RouteLens against independent routing services.

This is NOT an accuracy measurement. It compares RouteLens's route time and distance with
what other routing engines say for the same pairs of points. It shows whether RouteLens is
in the same range as those engines and whether it ranks short and long trips the same way,
but a routing engine's estimate is a prediction too, not a measured trip. Real, timed trips
are still the only ground truth (see docs/ETA_STUDY.md).

Pairs of points: random pairs of the documented major junctions (data/junctions), fixed seed.

References
  osrm     OSRM public demo server, car profile. No key. Free-flow style durations from
           OpenStreetMap with generic speeds per road class (not calibrated for Dehradun).
           Light use only: the script waits 1 s between requests and names itself.
  tomtom   TomTom Routing API with traffic. Needs the TOMTOM_API_KEY environment variable
           (never printed or saved). It reports the time with current traffic and the time
           with no traffic, so both of RouteLens's ETAs (with snapshot, and free-flow) have a
           counterpart. The result depends on the moment the script is run.

Usage (server running, e.g. ./build/routelens_server):
    python3 evaluation/eta_reference.py                          # OSRM only, 30 pairs
    TOMTOM_API_KEY=... python3 evaluation/eta_reference.py --refs osrm,tomtom
    python3 evaluation/eta_reference.py --pairs 40 --out evaluation/results/eta_reference.csv
"""
import argparse
import csv
import json
import math
import os
import random
import statistics
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from eta_study import PredictionError, fetch_prediction  # noqa: E402

EARTH_RADIUS_M = 6371008.8
USER_AGENT = "RouteLensStudentProject/1.0 (reference check, light use)"
OSRM_URL = "https://router.project-osrm.org"
TOMTOM_URL = "https://api.tomtom.com"


class ReferenceError(Exception):
    """A reference service gave no usable answer."""


# ---------------------------------------------------------------- points and pairs
def haversine_m(a, b):
    p1, p2 = math.radians(a[0]), math.radians(b[0])
    dphi, dlmb = p2 - p1, math.radians(b[1] - a[1])
    h = math.sin(dphi / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dlmb / 2) ** 2
    return 2 * EARTH_RADIUS_M * math.asin(min(1.0, math.sqrt(h)))


def load_junctions(path):
    """[(name, lat, lon)] for the rows that have coordinates."""
    points = []
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            try:
                points.append((row["name"], float(row["lat"]), float(row["lon"])))
            except (KeyError, TypeError, ValueError):
                continue                                   # a pending row without coordinates
    return points


def make_pairs(points, count, min_km, max_km, seed):
    """Deterministic random pairs whose straight-line distance is between min_km and max_km."""
    rng = random.Random(seed)
    candidates = []
    for i in range(len(points)):
        for j in range(len(points)):
            if i == j:
                continue
            km = haversine_m(points[i][1:], points[j][1:]) / 1000.0
            if min_km <= km <= max_km:
                candidates.append((points[i], points[j]))
    rng.shuffle(candidates)
    return candidates[:count]


# ---------------------------------------------------------------- reference services
def _get_json(url, opener, timeout):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with opener(request, timeout=timeout) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as exc:
        exc.close()
        raise ReferenceError("HTTP %d" % exc.code) from None            # never include the URL: it may hold a key
    except (urllib.error.URLError, OSError, ValueError):
        raise ReferenceError("service not reachable or bad answer") from None


def fetch_osrm(a, b, opener=None, timeout=30):
    opener = opener or urllib.request.urlopen
    url = "%s/route/v1/driving/%.6f,%.6f;%.6f,%.6f?overview=false&alternatives=false" % (
        OSRM_URL, a[2], a[1], b[2], b[1])                                  # OSRM wants lon,lat
    body = _get_json(url, opener, timeout)
    if body.get("code") != "Ok" or not body.get("routes"):
        raise ReferenceError("OSRM found no route (%s)" % body.get("code"))
    route = body["routes"][0]
    return {"time_s": float(route["duration"]), "distance_m": float(route["distance"])}


def fetch_tomtom(a, b, key, opener=None, timeout=30):
    opener = opener or urllib.request.urlopen
    query = urllib.parse.urlencode({"key": key, "traffic": "true", "travelMode": "car",
                                    "routeType": "fastest", "computeTravelTimeFor": "all"})
    url = "%s/routing/1/calculateRoute/%.6f,%.6f:%.6f,%.6f/json?%s" % (TOMTOM_URL, a[1], a[2], b[1], b[2], query)
    body = _get_json(url, opener, timeout)
    routes = body.get("routes") or []
    if not routes or "summary" not in routes[0]:
        raise ReferenceError("TomTom returned no route")
    s = routes[0]["summary"]
    if "travelTimeInSeconds" not in s or "lengthInMeters" not in s:
        raise ReferenceError("TomTom answer has no travel time")
    return {"time_s": float(s["travelTimeInSeconds"]),                      # with current traffic
            "no_traffic_s": float(s.get("noTrafficTravelTimeInSeconds", s["travelTimeInSeconds"])),
            "distance_m": float(s["lengthInMeters"])}


# ---------------------------------------------------------------- statistics
def compare(pairs_ab):
    """pairs_ab: [(routelens_value, reference_value)]. Returns summary or None if empty."""
    use = [(a, b) for a, b in pairs_ab if a is not None and b is not None and b > 0]
    if not use:
        return None
    ratios = [a / b for a, b in use]
    diffs = [100.0 * abs(a - b) / b for a, b in use]
    out = {"n": len(use), "median_ratio": statistics.median(ratios),
           "mean_abs_diff_pct": statistics.fmean(diffs), "r": None}
    if len(use) >= 3:
        xs, ys = [a for a, _ in use], [b for _, b in use]
        if len(set(xs)) > 1 and len(set(ys)) > 1:
            out["r"] = statistics.correlation(xs, ys)
    return out


COMPARISONS = (
    # (label, routelens column, reference name, reference column)
    ("RouteLens ETA (with snapshot) vs OSRM time", "eta_s", "osrm", "time_s"),
    ("RouteLens free-flow ETA vs OSRM time", "free_flow_s", "osrm", "time_s"),
    ("RouteLens distance vs OSRM distance", "distance_m", "osrm", "distance_m"),
    ("RouteLens ETA (with snapshot) vs TomTom time with traffic", "eta_s", "tomtom", "time_s"),
    ("RouteLens free-flow ETA vs TomTom time with no traffic", "free_flow_s", "tomtom", "no_traffic_s"),
    ("RouteLens distance vs TomTom distance", "distance_m", "tomtom", "distance_m"),
)


def format_report(rows, failures, refs):
    lines = ["%d pairs of major junctions compared (%d failed)." % (len(rows), len(failures)), ""]
    lines.append("%-62s %4s %12s %12s %6s" % ("comparison", "n", "median ratio", "mean |diff| %", "r"))
    for label, mine, ref, theirs in COMPARISONS:
        if ref not in refs:
            continue
        s = compare([(r.get(mine), r.get(ref + "_" + theirs)) for r in rows])
        if s is None:
            continue
        lines.append("%-62s %4d %12.2f %12.1f %6s" % (label, s["n"], s["median_ratio"], s["mean_abs_diff_pct"],
                                                      "n/a" if s["r"] is None else "%.2f" % s["r"]))
    lines.append("")
    speeds = [("RouteLens ETA (with snapshot)", "eta_s", "distance_m"),
              ("RouteLens free-flow ETA", "free_flow_s", "distance_m"),
              ("OSRM", "osrm_time_s", "osrm_distance_m"),
              ("TomTom with traffic", "tomtom_time_s", "tomtom_distance_m"),
              ("TomTom with no traffic", "tomtom_no_traffic_s", "tomtom_distance_m")]
    lines.append("median average speed over the pairs (own distance / own time):")
    for name, tcol, dcol in speeds:
        v = [3.6 * r[dcol] / r[tcol] for r in rows if r.get(tcol) and r.get(dcol)]
        if v:
            lines.append("  %-30s %5.1f km/h  (n=%d)" % (name, statistics.median(v), len(v)))
    lines.append("")
    lines.append("median ratio = RouteLens / reference (1.00 = same; above 1 = RouteLens slower or longer)")
    lines.append("r = correlation: do trips that are longer for the reference also come out longer in RouteLens?")
    lines.append("")
    lines.append("READ THIS BEFORE QUOTING THE NUMBERS")
    lines.append("- A reference engine's time is a prediction, not a measured trip: agreement is not accuracy.")
    if "osrm" in refs:
        lines.append("- OSRM uses generic speeds per road class and no traffic; it is a sanity check on the road")
        lines.append("  network and the routing, not on how long Dehradun trips take.")
    if "tomtom" in refs:
        lines.append("- The TomTom times describe the moment this script was run; the RouteLens snapshot is older.")
    for name, reason in failures[:5]:
        lines.append("failed: %s: %s" % (name, reason))
    return "\n".join(lines)


def write_csv(path, rows):
    cols = ["pair", "straight_km", "eta_s", "free_flow_s", "distance_m",
            "osrm_time_s", "osrm_distance_m", "tomtom_time_s", "tomtom_no_traffic_s", "tomtom_distance_m"]
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow({k: ("%.1f" % v if isinstance(v, float) else v) for k, v in r.items()})


# ---------------------------------------------------------------- run
def run(pairs, server, scenario, refs, tomtom_key=None, delay_s=1.0, sleep=time.sleep,
        routelens=None, osrm=fetch_osrm, tomtom=fetch_tomtom):
    routelens = routelens or (lambda a, b: fetch_prediction(
        server, {"from_lat": a[1], "from_lon": a[2], "to_lat": b[1], "to_lon": b[2]}, scenario))
    rows, failures = [], []
    for a, b in pairs:
        name = "%s -> %s" % (a[0], b[0])
        try:
            mine = routelens(a, b)
        except PredictionError as exc:
            failures.append((name, "RouteLens: " + str(exc)))
            continue
        row = {"pair": name, "straight_km": haversine_m(a[1:], b[1:]) / 1000.0, "eta_s": float(mine["eta_s"]),
               "free_flow_s": float(mine["free_flow_s"]) or float(mine["eta_s"]),
               "distance_m": float(mine["distance_km"]) * 1000.0}
        for ref in refs:
            try:
                got = osrm(a, b) if ref == "osrm" else tomtom(a, b, tomtom_key)
            except ReferenceError as exc:
                failures.append((name, "%s: %s" % (ref, exc)))
                continue
            for key, value in got.items():
                row[ref + "_" + key] = value
            sleep(delay_s)                                         # be polite to public services
        rows.append(row)
    return rows, failures


def main(argv=None):
    ap = argparse.ArgumentParser(description="Cross-check RouteLens against independent routing services.")
    ap.add_argument("--server", default="http://127.0.0.1:18080")
    ap.add_argument("--junctions", default="data/junctions/major_junctions.csv")
    ap.add_argument("--refs", default="osrm", help="comma list of: osrm, tomtom (default osrm)")
    ap.add_argument("--pairs", type=int, default=30)
    ap.add_argument("--min-km", type=float, default=1.5)
    ap.add_argument("--max-km", type=float, default=8.0)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--scenario", default="normal", choices=("normal", "rush_hour", "rain", "incident"))
    ap.add_argument("--out", help="write the per-pair results to this CSV file")
    args = ap.parse_args(argv)

    refs = [r.strip() for r in args.refs.split(",") if r.strip()]
    if not refs or any(r not in ("osrm", "tomtom") for r in refs):
        print("error: --refs must be osrm and/or tomtom", file=sys.stderr)
        return 2
    key = None
    if "tomtom" in refs:
        key = os.environ.get("TOMTOM_API_KEY", "").strip()
        if not key:
            print("error: set the TOMTOM_API_KEY environment variable to use --refs tomtom", file=sys.stderr)
            return 2
    try:
        points = load_junctions(args.junctions)
    except OSError as exc:
        print("error: %s: %s" % (args.junctions, exc), file=sys.stderr)
        return 2
    pairs = make_pairs(points, args.pairs, args.min_km, args.max_km, args.seed)
    if not pairs:
        print("error: no junction pairs between %.1f and %.1f km" % (args.min_km, args.max_km), file=sys.stderr)
        return 2

    rows, failures = run(pairs, args.server, args.scenario, refs, key)
    if not rows:
        print("error: no pair was answered (is the RouteLens server running at %s?)" % args.server, file=sys.stderr)
        for name, reason in failures[:3]:
            print("  %s: %s" % (name, reason), file=sys.stderr)
        return 1
    print(format_report(rows, failures, refs))
    if args.out:
        write_csv(args.out, rows)
        print("\nper-pair results written to " + args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
