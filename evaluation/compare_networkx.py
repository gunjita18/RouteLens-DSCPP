#!/usr/bin/env python3
"""Check routelens_eval results against NetworkX and write the Phase III report.

Reads the CSV files written by routelens_eval (the hand-written C routing core,
run through DijkstraRouter and PenaltyRouter), rebuilds the same weighted graph
in NetworkX for every weight profile, recomputes the shortest-path cost for each
random origin-destination pair, and compares it with the cost the C Dijkstra
found. It then summarises the C Dijkstra's run time and the alternative routes
from the penalty method.

Usage: compare_networkx.py <results_dir> [--report docs/evaluation_phase3.md]

The Phase II version of this script (A*, Yen's algorithm, std::priority_queue)
is in Git history together with docs/evaluation_phase2.md.
"""

import argparse
import csv
import math
import os
import platform
import statistics
import sys
import time

try:
    import networkx as nx
except ImportError:
    sys.exit("networkx is required (the project .venv has it): pip install networkx")

# Allowed difference between two costs that should be identical. Costs are sums
# of up to a few thousand doubles, so allow a small relative error.
REL_TOL = 1e-9


def read_csv(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def load_profiles(results):
    return {
        r["name"]: (
            float(r["per_km"]),
            float(r["per_minute"]),
            float(r["per_congested_minute"]),
            float(r["per_signal"]),
        )
        for r in read_csv(os.path.join(results, "profiles.csv"))
    }


def build_graph(results, weights):
    """DiGraph keeping the cheapest of any parallel edges, as Dijkstra effectively does."""
    per_km, per_min, per_cong, per_sig = weights
    g = nx.DiGraph()
    for r in read_csv(os.path.join(results, "eval_graph.csv")):
        km = float(r["distance_m"]) / 1000.0
        minutes = float(r["duration_s"]) / 60.0
        cost = (
            per_km * km
            + per_min * minutes
            + per_cong * float(r["traffic_index"]) * minutes
            + per_sig * int(r["signal_count"])
        )
        u, v = int(r["from"]), int(r["to"])
        if not g.has_edge(u, v) or cost < g[u][v]["w"]:
            g.add_edge(u, v, w=cost)
    return g


def percentile(values, p):
    ordered = sorted(values)
    if not ordered:
        return float("nan")
    k = (len(ordered) - 1) * p / 100.0
    lo, hi = math.floor(k), math.ceil(k)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (k - lo)


def fmt(x, digits=1):
    return "n/a" if x != x else f"{x:,.{digits}f}"


def correctness(results, profiles):
    """Per profile: pairs, mismatches against NetworkX, worst relative error."""
    rows = read_csv(os.path.join(results, "pairs.csv"))
    out = {}
    for name, weights in profiles.items():
        graph = build_graph(results, weights)
        subset = [r for r in rows if r["profile"] == name]
        stats = {"pairs": len(subset), "unreachable": 0, "mismatches": 0, "max_rel_err": 0.0}
        start = time.time()
        for r in subset:
            src, dst = int(r["src"]), int(r["dst"])
            try:
                ref = nx.dijkstra_path_length(graph, src, dst, weight="w")
            except nx.NetworkXNoPath:
                stats["unreachable"] += 1
                if r["found"] != "0":     # C found a route NetworkX says does not exist
                    stats["mismatches"] += 1
                continue
            got = float(r["cost"])
            err = abs(got - ref) / max(1.0, abs(ref))
            stats["max_rel_err"] = max(stats["max_rel_err"], err)
            if err > REL_TOL or r["found"] != "1":
                stats["mismatches"] += 1
        stats["networkx_seconds"] = time.time() - start
        out[name] = stats
        print(f"networkx check '{name}': {len(subset)} pairs in {stats['networkx_seconds']:.0f}s",
              file=sys.stderr)
    return out


def performance(results, profiles):
    rows = read_csv(os.path.join(results, "pairs.csv"))
    out = {}
    for name in profiles:
        subset = [r for r in rows if r["profile"] == name and r["found"] == "1"]
        us = [float(r["time_us"]) for r in subset]
        edges = [float(r["route_edges"]) for r in subset]
        out[name] = {
            "pairs": len(subset),
            "mean_ms": statistics.mean(us) / 1000.0 if us else float("nan"),
            "median_ms": statistics.median(us) / 1000.0 if us else float("nan"),
            "p95_ms": percentile(us, 95) / 1000.0,
            "mean_edges": statistics.mean(edges) if edges else float("nan"),
        }
    return out


def alternatives(results):
    rows = [r for r in read_csv(os.path.join(results, "alternatives.csv")) if r["method"] == "penalty"]
    if not rows:
        return None

    def col(key):
        return [float(r[key]) for r in rows]

    multi = [r for r in rows if int(r["routes"]) >= 2]

    def mean_multi(key):
        return statistics.mean(float(r[key]) for r in multi) if multi else float("nan")

    return {
        "pairs": len(rows),
        "routes": statistics.mean(col("routes")),
        "usable": statistics.mean(col("usable")),
        "pairs_with_2plus_usable": sum(1 for r in rows if int(r["usable"]) >= 2) / len(rows),
        "pairs_with_3plus_usable": sum(1 for r in rows if int(r["usable"]) >= 3) / len(rows),
        "mean_overlap": mean_multi("mean_overlap"),
        "max_overlap": mean_multi("max_overlap"),
        "mean_stretch": mean_multi("mean_stretch"),
        "max_stretch": mean_multi("max_stretch"),
        "mean_ms": statistics.mean(col("time_ms")),
        "median_ms": statistics.median(col("time_ms")),
        "p95_ms": percentile(col("time_ms"), 95),
    }


def read_meta(results):
    meta = {}
    path = os.path.join(results, "run_meta.txt")
    if os.path.exists(path):
        for line in open(path):
            if "=" in line:
                k, v = line.strip().split("=", 1)
                meta[k] = v
    return meta


def cpu_name():
    try:
        for line in open("/proc/cpuinfo"):
            if line.startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "unknown CPU"


def write_report(path, meta, profiles, corr, perf, alt, findings_path):
    L = []
    add = L.append
    add("# Phase III algorithm evaluation: the C routing core\n")
    add("_Generated by `evaluation/compare_networkx.py` from the output of "
        "`routelens_eval`. Re-run `evaluation/run_evaluation.sh` to reproduce._\n")

    add("## Setup\n")
    add(f"- Road network: Dehradun OSM export, largest strongly connected component, "
        f"{int(meta.get('nodes', 0)):,} vertices, {int(meta.get('edges', 0)):,} directed edges.")
    add(f"- Origin-destination pairs: {meta.get('pairs')} uniformly random vertex pairs "
        f"(seed {meta.get('seed')}); every pair is reachable because the graph is one SCC.")
    add("- Code under test: the hand-written C core (`src/core/dijkstra.c`, `penalty.c`, "
        "`minheap.c`, `stack.c`) called through `DijkstraRouter` and `PenaltyRouter` "
        "(`src/oop/route_finder.hpp`), exactly as the server calls it.")
    add("- Edge costs use free-flow travel times only (no traffic snapshot, `traffic_index` = 0), "
        "so these results measure the algorithms, not ETA accuracy.")
    add(f"- Machine: {cpu_name()}, {os.cpu_count()} logical CPUs, {platform.system()} {platform.release()}; "
        f"single-threaded, Release build.")
    add(f"- Reference: NetworkX {nx.__version__} `dijkstra_path_length` on the same edge costs "
        f"(parallel edges collapsed to the cheapest), Python {platform.python_version()}.")
    add("- Weight profiles: " + "; ".join(
        f"**{n}** (per km {w[0]:g}, per minute {w[1]:g}, per congested minute {w[2]:g}, per signal {w[3]:g})"
        for n, w in profiles.items()) + ".\n")

    add("## 1. C Dijkstra against NetworkX\n")
    add(f"A result counts as a mismatch when its cost differs from NetworkX's by more than "
        f"{REL_TOL:g} (relative), or when one finds a route and the other does not.\n")
    add("| Profile | Pairs | Mismatches | Max relative error |")
    add("|---|---:|---:|---:|")
    for name, s in corr.items():
        add(f"| {name} | {s['pairs']} | {s['mismatches']} | {s['max_rel_err']:.2e} |")
    add("")

    add("## 2. C Dijkstra run time\n")
    add("Time for one `DijkstraRouter::find` call (search, route rebuild with the stack, copy into a "
        "`Route`). The search stops when the destination is settled.\n")
    add("| Profile | Pairs | Mean ms | Median ms | 95th pct ms | Mean route length (edges) |")
    add("|---|---:|---:|---:|---:|---:|")
    for name, p in perf.items():
        add(f"| {name} | {p['pairs']} | {fmt(p['mean_ms'], 2)} | {fmt(p['median_ms'], 2)} | "
            f"{fmt(p['p95_ms'], 2)} | {fmt(p['mean_edges'], 0)} |")
    add("")

    add("## 3. Penalty method (alternative routes)\n")
    add(f"`PenaltyRouter` with the server's settings: {meta.get('alt_routes')} routes requested, edge "
        f"penalty x{meta.get('penalty_factor')}, a route dropped when more than "
        f"{float(meta.get('max_overlap', 0)) * 100:.0f}% of its edges belong to an earlier route or it "
        f"costs over {meta.get('max_stretch')}x the cheapest; balanced profile; first "
        f"{meta.get('alt_pairs')} pairs. *Overlap* is the share of the shorter route's length another "
        "route also uses (averaged over route pairs). *Stretch* is a route's cost divided by the "
        "cheapest. *Usable* counts routes that pass the server's later filter (stretch <= 1.5 and "
        "overlap < 0.85 with every route kept).\n")
    add("| Metric | Value |")
    add("|---|---:|")
    if alt:
        def row(label, key, digits=2, pct=False):
            v = alt[key]
            add(f"| {label} | {f'{v * 100:.0f}%' if pct else fmt(v, digits)} |")
        row("Pairs evaluated", "pairs", 0)
        row("Routes returned (mean)", "routes")
        row("Usable routes after filter (mean)", "usable")
        row("Pairs with at least 2 usable routes", "pairs_with_2plus_usable", pct=True)
        row("Pairs with at least 3 usable routes", "pairs_with_3plus_usable", pct=True)
        row("Mean overlap between routes", "mean_overlap", 3)
        row("Mean of worst overlap", "max_overlap", 3)
        row("Mean stretch", "mean_stretch", 3)
        row("Mean of worst stretch", "max_stretch", 3)
        row("Run time mean (ms)", "mean_ms", 1)
        row("Run time median (ms)", "median_ms", 1)
        row("Run time 95th percentile (ms)", "p95_ms", 1)
    add("")

    if findings_path and os.path.exists(findings_path):
        add(open(findings_path).read().rstrip() + "\n")

    with open(path, "w") as f:
        f.write("\n".join(L))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("results", help="directory written by routelens_eval")
    ap.add_argument("--report", default="docs/evaluation_phase3.md")
    ap.add_argument("--findings", default=os.path.join(os.path.dirname(__file__), "findings_phase3.md"),
                    help="hand-written interpretation appended to the report if present")
    args = ap.parse_args()

    profiles = load_profiles(args.results)
    meta = read_meta(args.results)

    corr = correctness(args.results, profiles)
    perf = performance(args.results, profiles)
    alt = alternatives(args.results)
    write_report(args.report, meta, profiles, corr, perf, alt, args.findings)

    bad = sum(s["mismatches"] for s in corr.values())
    print(f"report written to {args.report}; total mismatches vs NetworkX: {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
