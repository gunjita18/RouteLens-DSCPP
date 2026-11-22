#!/usr/bin/env python3
"""
eta_study.py - compare RouteLens ETAs with real, timed trips.

For every trip in a CSV file the script asks a running RouteLens server for routes
between the trip's start and end, takes the fastest route's ETA, and compares it with
the time the trip really took. It reports how far off the ETA is, and whether a single
correction factor on all predicted times would have helped (checked fairly, with
leave-one-out, so a trip is never used to correct its own prediction).

Trip file (CSV, header required): see evaluation/trips_template.csv and docs/ETA_STUDY.md
    trip_id,from_lat,from_lon,to_lat,to_lon,actual_minutes[,depart_time,vehicle,notes]

Usage (server running, e.g. ./build/routelens_server):
    python3 evaluation/eta_study.py --trips evaluation/trips.csv
    python3 evaluation/eta_study.py --trips evaluation/trips.csv --scenario rush_hour --out evaluation/results/eta_study.csv

Sign convention: error = (predicted - actual) / actual. Positive: RouteLens predicted a
LONGER time than the trip took; negative: it was too optimistic.
"""
import argparse
import csv
import json
import math
import statistics
import sys
import urllib.error
import urllib.parse
import urllib.request

REQUIRED = ("trip_id", "from_lat", "from_lon", "to_lat", "to_lon", "actual_minutes")
MIN_TRIPS_FOR_LOO = 3


class PredictionError(Exception):
    """The server could not give a route for a trip."""


# ---------------------------------------------------------------- input
def load_trips(path):
    """Reads and validates the trip file. Raises ValueError naming the row and problem."""
    trips = []
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        missing = [c for c in REQUIRED if c not in (reader.fieldnames or [])]
        if missing:
            raise ValueError("missing column(s): " + ", ".join(missing))
        seen = set()
        for number, row in enumerate(reader, start=2):          # line 1 is the header
            if not any((v or "").strip() for v in row.values()):
                continue
            try:
                trip = {
                    "trip_id": row["trip_id"].strip(),
                    "from_lat": float(row["from_lat"]), "from_lon": float(row["from_lon"]),
                    "to_lat": float(row["to_lat"]), "to_lon": float(row["to_lon"]),
                    "actual_minutes": float(row["actual_minutes"]),
                    "depart_time": (row.get("depart_time") or "").strip(),
                    "vehicle": (row.get("vehicle") or "").strip(),
                    "notes": (row.get("notes") or "").strip(),
                }
            except (TypeError, ValueError):
                raise ValueError("line %d: a coordinate or actual_minutes is not a number" % number) from None
            if not trip["trip_id"]:
                raise ValueError("line %d: trip_id is empty" % number)
            if trip["trip_id"] in seen:
                raise ValueError("line %d: duplicate trip_id %s" % (number, trip["trip_id"]))
            seen.add(trip["trip_id"])
            for key in ("from_lat", "to_lat"):
                if not -90 <= trip[key] <= 90:
                    raise ValueError("line %d: %s out of range" % (number, key))
            for key in ("from_lon", "to_lon"):
                if not -180 <= trip[key] <= 180:
                    raise ValueError("line %d: %s out of range" % (number, key))
            if not math.isfinite(trip["actual_minutes"]) or trip["actual_minutes"] <= 0:
                raise ValueError("line %d: actual_minutes must be greater than 0" % number)
            trips.append(trip)
    return trips


def hour_bucket(depart_time):
    """'2026-11-24 09:15' or '09:15' -> a label for the time of day; '' if unknown."""
    text = depart_time.strip()
    if not text:
        return ""
    clock = text.split()[-1]
    try:
        hour = int(clock.split(":")[0])
    except ValueError:
        return ""
    if not 0 <= hour <= 23:
        return ""
    if 7 <= hour < 10:
        return "morning rush (07-10)"
    if 10 <= hour < 16:
        return "midday (10-16)"
    if 16 <= hour < 20:
        return "evening rush (16-20)"
    return "other hours"


# ---------------------------------------------------------------- server
def fetch_prediction(server, trip, scenario="normal", opener=None, timeout=30):
    """Asks the server for routes; returns the fastest route's ETA and free-flow ETA (seconds)."""
    opener = opener or urllib.request.urlopen
    query = urllib.parse.urlencode({
        "from_lat": trip["from_lat"], "from_lon": trip["from_lon"],
        "to_lat": trip["to_lat"], "to_lon": trip["to_lon"],
        "scenario": scenario,
        "w_time": 1, "w_distance": 0, "w_junctions": 0, "w_traffic": 0,   # rank by time only
    })
    url = server.rstrip("/") + "/api/routes?" + query
    try:
        with opener(url, timeout=timeout) as resp:
            body = json.loads(resp.read())
    except urllib.error.HTTPError as exc:
        try:
            message = json.loads(exc.read()).get("error", "")
        except (ValueError, AttributeError):
            message = ""
        raise PredictionError("HTTP %d %s" % (exc.code, message)) from None
    except (urllib.error.URLError, OSError, ValueError) as exc:
        raise PredictionError("server not reachable or bad answer: %s" % exc) from None
    routes = body.get("routes") or []
    if not routes:
        raise PredictionError("the server returned no route")
    fastest = min(routes, key=lambda r: r["eta_s"])
    return {"eta_s": fastest["eta_s"], "free_flow_s": fastest.get("free_flow_eta_s") or 0.0,
            "routes": len(routes), "distance_km": fastest.get("distance_km", 0.0)}


# ---------------------------------------------------------------- statistics
def add_errors(trip, predicted_min, label):
    """Returns {label}_min and {label}_err_pct for one trip, in place."""
    trip[label + "_min"] = predicted_min
    trip[label + "_err_pct"] = 100.0 * (predicted_min - trip["actual_minutes"]) / trip["actual_minutes"]


def summarise(rows, label):
    """Error statistics of one prediction column over rows that have it."""
    use = [r for r in rows if r.get(label + "_min") is not None]
    if not use:
        return None
    errs = [r[label + "_err_pct"] for r in use]
    abs_errs = [abs(e) for e in errs]
    abs_min = [abs(r[label + "_min"] - r["actual_minutes"]) for r in use]
    return {
        "n": len(use),
        "mae_min": statistics.fmean(abs_min),
        "mape_pct": statistics.fmean(abs_errs),
        "median_abs_pct": statistics.median(abs_errs),
        "bias_pct": statistics.fmean(errs),
        "within_20pct": sum(1 for e in abs_errs if e <= 20.0) / len(use),
    }


def scale_factor(rows, label, exclude=None):
    """Factor f such that f * predicted best matches actual in total: sum(actual) / sum(predicted)."""
    use = [r for i, r in enumerate(rows) if i != exclude and r.get(label + "_min")]
    total_pred = sum(r[label + "_min"] for r in use)
    if not use or total_pred <= 0:
        return None
    return sum(r["actual_minutes"] for r in use) / total_pred


def leave_one_out(rows, label):
    """Mean absolute % error if each trip's prediction were corrected by a factor learned
    from the OTHER trips only. Returns None with fewer than MIN_TRIPS_FOR_LOO trips."""
    use = [r for r in rows if r.get(label + "_min")]
    if len(use) < MIN_TRIPS_FOR_LOO:
        return None
    errs, factors = [], []
    for i, r in enumerate(use):
        f = scale_factor(use, label, exclude=i)
        factors.append(f)
        errs.append(abs(100.0 * (f * r[label + "_min"] - r["actual_minutes"]) / r["actual_minutes"]))
    return {"mape_pct": statistics.fmean(errs), "factor_min": min(factors), "factor_max": max(factors)}


# ---------------------------------------------------------------- report
def format_report(rows, failed, scenario):
    lines = []
    lines.append("%-10s %-22s %8s %9s %9s %8s %8s" %
                 ("trip", "when", "actual", "RouteLens", "free-flow", "err %", "ff err %"))
    for r in rows:
        if r.get("eta_min") is None:
            continue
        lines.append("%-10s %-22s %7.1f %9.1f %9.1f %+8.1f %+8.1f" % (
            r["trip_id"][:10], (hour_bucket(r["depart_time"]) or "unknown time")[:22],
            r["actual_minutes"], r["eta_min"], r["free_flow_min"], r["eta_err_pct"], r["free_flow_err_pct"]))
    for trip_id, reason in failed:
        lines.append("%-10s FAILED: %s" % (trip_id[:10], reason))
    lines.append("")
    lines.append("scenario: %s; minutes; err %% = (predicted - actual) / actual, positive = predicted too long" % scenario)

    for label, name in (("eta", "RouteLens ETA (with traffic data)"), ("free_flow", "Free-flow ETA (no traffic data)")):
        s = summarise(rows, label)
        if not s:
            continue
        lines.append("")
        lines.append("%s over %d trips:" % (name, s["n"]))
        lines.append("  mean absolute error  %.1f min  (%.1f %%), median %.1f %%" %
                     (s["mae_min"], s["mape_pct"], s["median_abs_pct"]))
        lines.append("  bias                 %+.1f %%   (%s)" % (s["bias_pct"],
                     "too long on average" if s["bias_pct"] > 0 else "too short on average"))
        lines.append("  within 20 %% of real  %.0f %% of trips" % (100 * s["within_20pct"]))
        f = scale_factor(rows, label)
        loo = leave_one_out(rows, label)
        if f is not None:
            lines.append("  one correction factor (all trips): x%.2f" % f)
        if loo:
            lines.append("  after a factor learned from the other trips (leave-one-out): %.1f %% mean abs error"
                         "  (factor range x%.2f to x%.2f)" % (loo["mape_pct"], loo["factor_min"], loo["factor_max"]))
        elif s["n"] < MIN_TRIPS_FOR_LOO:
            lines.append("  (fewer than %d trips: no leave-one-out check)" % MIN_TRIPS_FOR_LOO)

    buckets = {}
    for r in rows:
        if r.get("eta_min") is not None:
            buckets.setdefault(hour_bucket(r["depart_time"]) or "unknown time", []).append(r)
    if len(buckets) > 1:
        lines.append("")
        lines.append("By time of day (RouteLens ETA):")
        for name in sorted(buckets):
            s = summarise(buckets[name], "eta")
            lines.append("  %-24s n=%-2d  mean abs %.1f %%, bias %+.1f %%" % (name, s["n"], s["mape_pct"], s["bias_pct"]))

    n = sum(1 for r in rows if r.get("eta_min") is not None)
    lines.append("")
    if n < 10:
        lines.append("NOTE: %d trips is a small sample. Report these as a pilot, with the trips listed, "
                     "not as a statistically reliable accuracy." % n)
    lines.append("NOTE: a correction factor fitted on so few trips can only be trusted if the leave-one-out "
                 "error above is clearly lower than the uncorrected one.")
    return "\n".join(lines)


def write_csv(path, rows):
    cols = ["trip_id", "depart_time", "vehicle", "actual_minutes", "eta_min", "eta_err_pct",
            "free_flow_min", "free_flow_err_pct", "distance_km", "notes"]
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            if r.get("eta_min") is not None:
                w.writerow({k: ("%.2f" % v if isinstance(v, float) else v) for k, v in r.items() if k in cols})


def run(trips, server, scenario, fetch=fetch_prediction):
    rows, failed = [], []
    for trip in trips:
        try:
            p = fetch(server, trip, scenario)
        except PredictionError as exc:
            failed.append((trip["trip_id"], str(exc)))
            continue
        row = dict(trip)
        row["distance_km"] = p["distance_km"]
        add_errors(row, p["eta_s"] / 60.0, "eta")
        add_errors(row, p["free_flow_s"] / 60.0 if p["free_flow_s"] > 0 else p["eta_s"] / 60.0, "free_flow")
        rows.append(row)
    return rows, failed


def main(argv=None):
    ap = argparse.ArgumentParser(description="Compare RouteLens ETAs with real timed trips.")
    ap.add_argument("--trips", required=True, help="CSV file of timed trips (see evaluation/trips_template.csv)")
    ap.add_argument("--server", default="http://127.0.0.1:18080")
    ap.add_argument("--scenario", default="normal", choices=("normal", "rush_hour", "rain", "incident"))
    ap.add_argument("--vehicle", help="only use trips with this vehicle value (e.g. car or bike)")
    ap.add_argument("--out", help="write the per-trip results to this CSV file")
    args = ap.parse_args(argv)

    try:
        trips = load_trips(args.trips)
    except (OSError, ValueError) as exc:
        print("error: %s: %s" % (args.trips, exc), file=sys.stderr)
        return 2
    if args.vehicle:
        trips = [t for t in trips if t["vehicle"].lower() == args.vehicle.lower()]
    if not trips:
        print("error: no trips to evaluate", file=sys.stderr)
        return 2

    rows, failed = run(trips, args.server, args.scenario)
    if not rows:
        print("error: no trip got a prediction (is the server running at %s?)" % args.server, file=sys.stderr)
        for trip_id, reason in failed:
            print("  %s: %s" % (trip_id, reason), file=sys.stderr)
        return 1
    print(format_report(rows, failed, args.scenario))
    if args.out:
        write_csv(args.out, rows)
        print("\nper-trip results written to " + args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
