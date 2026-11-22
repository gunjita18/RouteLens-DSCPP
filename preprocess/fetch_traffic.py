#!/usr/bin/env python3
"""
fetch_traffic.py - save a traffic snapshot from TomTom for offline use.

Run once before a demo (needs internet and a TomTom key). The server then reads
the file with the C reader (src/core/traffic_reader.c) and never touches the
network, so the demo works offline and stays inside TomTom's free quota.

Input   data/osm/edges.csv            (written by export_osm.py)
Output  data/traffic/traffic.csv

    # fetched_at = 2026-11-10 09:30
    edge_id,current_kph,freeflow_kph,confidence
    1452,33,42,0.60

edge_id is the 0-based data row of the edge in edges.csv.

Method
  1. Keep edges of the chosen road classes (default: trunk, primary, secondary).
  2. For each edge take the midpoint of its shape.
  3. Segment cache: if the midpoint lies within 50 m of a TomTom segment that was
     already fetched, reuse that answer. One TomTom segment can cover many
     kilometres of a main road, so this turns thousands of edges into a few dozen
     requests.
  4. Otherwise ask TomTom (Flow Segment Data), at most 4 requests per second and
     at most --max-requests requests per run.
  5. 50 m rule: if the road TomTom answers about is more than 50 m from the
     midpoint, it is a different road, so record nothing for the edge.
  6. Ignore answers with confidence below 0.5.

The key is read from the TOMTOM_API_KEY environment variable only. It is never
written to a file or printed.

Usage (from the project root):
  export TOMTOM_API_KEY=...           # PowerShell: $env:TOMTOM_API_KEY = "..."
  python3 preprocess/fetch_traffic.py --dry-run     # count requests, no network
  python3 preprocess/fetch_traffic.py
"""
import argparse
import csv
import datetime
import json
import math
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

EARTH_RADIUS_M = 6371008.8
DEFAULT_CLASSES = ("trunk", "primary", "secondary")
MAX_SNAP_M = 50.0               # TomTom road farther than this is not our road
MIN_CONFIDENCE = 0.5            # the single confidence rule
MIN_INTERVAL_S = 0.25           # 4 requests per second, under TomTom's limit of 5
MAX_REQUESTS = 2000
MAX_CONSECUTIVE_FAILURES = 5
ZOOM = 10
BASE_URL = "https://api.tomtom.com"
TIMEOUT_S = 15


class FetchAbort(Exception):
    """Fetching cannot continue (bad key, quota, network down)."""


# ---------------------------------------------------------------- geometry
# Points are (lat, lon) tuples in degrees.

def haversine_m(a, b):
    lat1, lat2 = math.radians(a[0]), math.radians(b[0])
    dlat = lat2 - lat1
    dlon = math.radians(b[1] - a[1])
    h = math.sin(dlat / 2) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin(dlon / 2) ** 2
    return 2 * EARTH_RADIUS_M * math.asin(min(1.0, math.sqrt(h)))


def _xy(origin, pt):
    """Local flat projection of pt in metres, relative to origin (fine at city scale)."""
    x = math.radians(pt[1] - origin[1]) * math.cos(math.radians(origin[0])) * EARTH_RADIUS_M
    y = math.radians(pt[0] - origin[0]) * EARTH_RADIUS_M
    return x, y


def point_to_segment_m(p, a, b):
    ax, ay = _xy(p, a)
    bx, by = _xy(p, b)
    dx, dy = bx - ax, by - ay
    length2 = dx * dx + dy * dy
    t = 0.0 if length2 == 0 else max(0.0, min(1.0, -(ax * dx + ay * dy) / length2))
    return math.hypot(ax + t * dx, ay + t * dy)


def point_to_polyline_m(p, pts):
    if len(pts) == 1:
        return haversine_m(p, pts[0])
    return min(point_to_segment_m(p, pts[i], pts[i + 1]) for i in range(len(pts) - 1))


def polyline_midpoint(pts):
    """The point half-way along the polyline's length."""
    if len(pts) == 1:
        return pts[0]
    lengths = [haversine_m(pts[i], pts[i + 1]) for i in range(len(pts) - 1)]
    total = sum(lengths)
    if total == 0:
        return pts[0]
    half = total / 2
    for i, seg in enumerate(lengths):
        if half <= seg:
            f = 0.0 if seg == 0 else half / seg
            a, b = pts[i], pts[i + 1]
            return (a[0] + f * (b[0] - a[0]), a[1] + f * (b[1] - a[1]))
        half -= seg
    return pts[-1]


def parse_geometry(text):
    """'lon lat;lon lat;...' -> [(lat, lon), ...]; None if malformed or < 2 points."""
    pts = []
    for part in text.split(";"):
        bits = part.split()
        if len(bits) != 2:
            return None
        try:
            lon, lat = float(bits[0]), float(bits[1])
        except ValueError:
            return None
        if not (math.isfinite(lat) and math.isfinite(lon)) or abs(lat) > 90 or abs(lon) > 180:
            return None
        pts.append((lat, lon))
    return pts if len(pts) >= 2 else None


# ---------------------------------------------------------------- input
def read_edges(path):
    """Yields (edge_id, highway, shape or None) for every data row of edges.csv.

    edge_id counts data rows from 0, exactly like the C++ loader, so malformed rows
    still use up an id (their shape is None)."""
    with open(path, newline="", encoding="utf-8") as f:
        for edge_id, row in enumerate(csv.DictReader(f)):
            yield edge_id, row.get("highway", ""), parse_geometry(row.get("geometry", ""))


# ---------------------------------------------------------------- TomTom
def parse_flow_segment(doc):
    """TomTom Flow Segment Data JSON -> dict. Raises ValueError if unusable."""
    try:
        d = doc["flowSegmentData"]
        seg = {
            "current": float(d["currentSpeed"]),
            "freeflow": float(d["freeFlowSpeed"]),
            "confidence": float(d["confidence"]),
            "shape": [(float(c["latitude"]), float(c["longitude"]))
                      for c in d["coordinates"]["coordinate"]],
        }
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError("missing or malformed flowSegmentData fields") from exc
    if not seg["shape"]:
        raise ValueError("segment has no shape")
    if seg["current"] < 0 or seg["freeflow"] <= 0 or not 0.0 <= seg["confidence"] <= 1.0:
        raise ValueError("speed or confidence out of range")
    return seg


def make_fetch(api_key, base_url=BASE_URL, zoom=ZOOM, timeout=TIMEOUT_S, opener=None):
    """Returns fetch(point) -> ("ok", segment) | ("no_coverage", None) | ("error", None).

    Raises FetchAbort for a rejected key or an exhausted quota. Messages never
    contain the request URL, which includes the key."""
    opener = opener or urllib.request.urlopen

    def fetch(point):
        query = urllib.parse.urlencode({"point": "%.6f,%.6f" % point, "unit": "KMPH", "key": api_key})
        url = "%s/traffic/services/4/flowSegmentData/absolute/%d/json?%s" % (base_url, zoom, query)
        request = urllib.request.Request(url, headers={"User-Agent": "RouteLensSnapshot/1.0"})
        try:
            with opener(request, timeout=timeout) as resp:
                body = resp.read()
        except urllib.error.HTTPError as exc:
            exc.close()                         # release the connection
            if exc.code in (401, 403):
                raise FetchAbort("TomTom rejected the API key (HTTP %d)" % exc.code) from None
            if exc.code == 429:
                raise FetchAbort("TomTom request limit reached (HTTP 429)") from None
            if exc.code in (400, 404):          # no road near this point
                return "no_coverage", None
            return "error", None
        except (urllib.error.URLError, TimeoutError, OSError):
            return "error", None
        try:
            return "ok", parse_flow_segment(json.loads(body))
        except (ValueError, json.JSONDecodeError):
            return "error", None

    return fetch


# ---------------------------------------------------------------- snapshot
def build_snapshot(edges, fetch, classes=DEFAULT_CLASSES, max_requests=MAX_REQUESTS,
                   max_snap_m=MAX_SNAP_M, min_confidence=MIN_CONFIDENCE,
                   interval_s=MIN_INTERVAL_S, sleep=time.sleep, clock=time.monotonic):
    """Returns (rows, stats, abort_reason).

    rows: [(edge_id, current_kph, freeflow_kph, confidence)] in edge_id order.
    stats: counters for the run summary. abort_reason is None for a complete run,
    otherwise why fetching stopped early (rows holds what was collected)."""
    stats = dict(candidates=0, requests=0, reused=0, recorded=0, low_confidence=0,
                 wrong_road=0, no_coverage=0, errors=0, not_fetched=0, bad_geometry=0)
    segments = []        # TomTom segments already fetched
    misses = []          # points with no usable TomTom road: do not ask again nearby
    rows = []
    last_request = None
    consecutive_errors = 0
    abort_reason = None

    for edge_id, highway, shape in edges:
        if highway not in classes:
            continue
        if shape is None:
            stats["bad_geometry"] += 1
            continue
        stats["candidates"] += 1
        mid = polyline_midpoint(shape)

        seg = next((s for s in segments if point_to_polyline_m(mid, s["shape"]) <= max_snap_m), None)
        if seg is not None:
            stats["reused"] += 1
        elif any(haversine_m(mid, m) <= max_snap_m for m in misses):
            stats["no_coverage"] += 1
            continue
        else:
            if abort_reason is not None or stats["requests"] >= max_requests:
                if abort_reason is None:
                    abort_reason = "request limit of %d reached" % max_requests
                stats["not_fetched"] += 1
                continue
            if last_request is not None:
                wait = interval_s - (clock() - last_request)
                if wait > 0:
                    sleep(wait)
            last_request = clock()
            stats["requests"] += 1
            try:
                status, answer = fetch(mid)
            except FetchAbort as exc:
                abort_reason = str(exc)
                stats["not_fetched"] += 1
                continue
            if status == "error":
                stats["errors"] += 1
                consecutive_errors += 1
                if consecutive_errors >= MAX_CONSECUTIVE_FAILURES:
                    abort_reason = "TomTom unreachable %d times in a row" % consecutive_errors
                continue
            consecutive_errors = 0
            if status == "no_coverage":
                misses.append(mid)
                stats["no_coverage"] += 1
                continue
            segments.append(answer)
            if point_to_polyline_m(mid, answer["shape"]) > max_snap_m:
                misses.append(mid)                       # TomTom answered about another road
                stats["wrong_road"] += 1
                continue
            seg = answer

        if seg["confidence"] < min_confidence:
            stats["low_confidence"] += 1
            continue
        rows.append((edge_id, seg["current"], seg["freeflow"], seg["confidence"]))
        stats["recorded"] += 1

    return rows, stats, abort_reason


def _num(x):
    return ("%g" % x) if abs(x - round(x)) > 1e-9 else str(int(round(x)))


def format_snapshot(rows, fetched_at, note=None):
    lines = ["# traffic.csv (TomTom Flow Segment Data snapshot; see preprocess/fetch_traffic.py)",
             "# fetched_at = " + fetched_at]
    if note:
        lines.append("# note = " + note)
    lines.append("edge_id,current_kph,freeflow_kph,confidence")
    for edge_id, cur, free, conf in rows:
        lines.append("%d,%s,%s,%.2f" % (edge_id, _num(cur), _num(free), conf))
    return "\n".join(lines) + "\n"


def write_snapshot(path, rows, fetched_at, note=None):
    """Writes atomically so an interrupted run never leaves a half-written file."""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(format_snapshot(rows, fetched_at, note))
    os.replace(tmp, path)


# ---------------------------------------------------------------- command line
def main(argv=None):
    ap = argparse.ArgumentParser(description="Save a TomTom traffic snapshot for offline use.")
    ap.add_argument("--edges", default="data/osm/edges.csv")
    ap.add_argument("--out", default="data/traffic/traffic.csv")
    ap.add_argument("--classes", default=",".join(DEFAULT_CLASSES),
                    help="comma-separated OSM road classes to query (default: %(default)s)")
    ap.add_argument("--max-requests", type=int, default=MAX_REQUESTS)
    ap.add_argument("--dry-run", action="store_true",
                    help="count candidate edges and estimate requests; no network, no key needed")
    args = ap.parse_args(argv)
    classes = tuple(c.strip() for c in args.classes.split(",") if c.strip())

    if not os.path.exists(args.edges):
        print("error: %s not found (run preprocess/export_osm.py)" % args.edges, file=sys.stderr)
        return 2

    if args.dry_run:
        rows, stats, _ = build_snapshot(read_edges(args.edges), lambda p: ("no_coverage", None),
                                        classes, args.max_requests, sleep=lambda s: None)
        print("dry run: %d candidate edges of classes %s; no network used." % (stats["candidates"], ",".join(classes)))
        print("An upper bound is one request per candidate edge; segment reuse lowers it a lot.")
        return 0

    api_key = os.environ.get("TOMTOM_API_KEY", "").strip()
    if not api_key:
        print("error: set the TOMTOM_API_KEY environment variable first", file=sys.stderr)
        return 2

    fetched_at = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    rows, stats, abort_reason = build_snapshot(read_edges(args.edges), make_fetch(api_key),
                                               classes, args.max_requests)

    print("candidate edges:      %d" % stats["candidates"])
    print("TomTom requests:      %d (limit %d)" % (stats["requests"], args.max_requests))
    print("reused a segment:     %d" % stats["reused"])
    print("recorded:             %d" % stats["recorded"])
    print("skipped, confidence<%.1f: %d" % (MIN_CONFIDENCE, stats["low_confidence"]))
    print("skipped, wrong road:  %d (TomTom road more than %d m away)" % (stats["wrong_road"], MAX_SNAP_M))
    print("skipped, no coverage: %d" % stats["no_coverage"])
    print("request errors:       %d" % stats["errors"])
    if stats["not_fetched"]:
        print("not fetched:          %d" % stats["not_fetched"])

    if abort_reason:
        print("stopped early: " + abort_reason, file=sys.stderr)
        if not rows:
            print("nothing recorded; no file written", file=sys.stderr)
            return 1
        write_snapshot(args.out, rows, fetched_at, note="partial: " + abort_reason)
        print("wrote PARTIAL snapshot %s (%d edges)" % (args.out, len(rows)), file=sys.stderr)
        return 1

    write_snapshot(args.out, rows, fetched_at)
    print("wrote %s (%d edges, fetched_at %s)" % (args.out, len(rows), fetched_at))
    return 0


if __name__ == "__main__":
    sys.exit(main())
