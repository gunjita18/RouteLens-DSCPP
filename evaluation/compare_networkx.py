#!/usr/bin/env python3
"""Check routelens_eval results against NetworkX and write the Phase II report.

Reads the CSV files written by routelens_eval, rebuilds the same weighted graph
in NetworkX for every weight profile, recomputes the shortest-path cost for each
random origin-destination pair, and compares it with the cost found by
  - Dijkstra on RouteLens's own binary min-heap,
  - A* with the straight-line heuristic,
  - Dijkstra on std::priority_queue.
It then summarises run time, vertices expanded, and the quality of the
alternative routes from the penalty method and from Yen's algorithm.

Usage: compare_networkx.py <results_dir> [--report docs/evaluation_phase2.md]
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
    """DiGraph keeping the cheapest of any parallel edges, like the C++ search."""
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
    rows = read_csv(os.path.join(results, "pairs.csv"))
    out = {}
    for name, weights in profiles.items():
        graph = build_graph(results, weights)
        subset = [r for r in rows if r["profile"] == name]
        stats = {"pairs": len(subset), "unreachable": 0}
        for algo in ("dijkstra", "astar", "pq"):
            stats[algo] = {"mismatches": 0, "max_rel_err": 0.0}

        start = time.time()
        for r in subset:
            src, dst = int(r["src"]), int(r["dst"])
            try:
                ref = nx.dijkstra_path_length(graph, src, dst, weight="w")
            except nx.NetworkXNoPath:
                stats["unreachable"] += 1
                continue
            for algo in ("dijkstra", "astar", "pq"):
                got = float(r[f"{algo}_cost"])
                err = abs(got - ref) / max(1.0, abs(ref))
                stats[algo]["max_rel_err"] = max(stats[algo]["max_rel_err"], err)
                if err > REL_TOL or r["found"] != "1":
                    stats[algo]["mismatches"] += 1
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
        entry = {"pairs": len(subset)}
        for algo in ("dijkstra", "astar", "pq"):
            us = [float(r[f"{algo}_us"]) for r in subset]
            ex = [float(r[f"{algo}_expanded"]) for r in subset]
            entry[algo] = {
                "mean_ms": statistics.mean(us) / 1000.0,
                "median_ms": statistics.median(us) / 1000.0,
                "p95_ms": percentile(us, 95) / 1000.0,
                "mean_expanded": statistics.mean(ex),
            }
        # Per-pair ratios are more robust than a ratio of means.
        ratios_astar = [float(r["dijkstra_us"]) / float(r["astar_us"])
                        for r in subset if float(r["astar_us"]) > 0]
        ratios_heap = [float(r["pq_us"]) / float(r["dijkstra_us"])
                       for r in subset if float(r["dijkstra_us"]) > 0]
        entry["astar_speedup_median"] = statistics.median(ratios_astar)
        entry["heap_vs_pq_median"] = statistics.median(ratios_heap)
        entry["expansion_ratio"] = (
            entry["dijkstra"]["mean_expanded"] / entry["astar"]["mean_expanded"])
        out[name] = entry
    return out


def alternatives(results):
    rows = read_csv(os.path.join(results, "alternatives.csv"))
    out = {}
    for method in ("penalty", "yen"):
        subset = [r for r in rows if r["method"] == method]
        if not subset:
            continue

        def col(key):
            return [float(r[key]) for r in subset]

        multi = [r for r in subset if int(r["routes"]) >= 2]
        out[method] = {
            "pairs": len(subset),
            "routes": statistics.mean(col("routes")),
            "usable": statistics.mean(col("usable")),
            "pairs_with_2plus_usable": sum(1 for r in subset if int(r["usable"]) >= 2) / len(subset),
            "pairs_with_3plus_usable": sum(1 for r in subset if int(r["usable"]) >= 3) / len(subset),
            "mean_overlap": statistics.mean(float(r["mean_overlap"]) for r in multi) if multi else float("nan"),
            "max_overlap": statistics.mean(float(r["max_overlap"]) for r in multi) if multi else float("nan"),
            "mean_stretch": statistics.mean(float(r["mean_stretch"]) for r in multi) if multi else float("nan"),
            "max_stretch": statistics.mean(float(r["max_stretch"]) for r in multi) if multi else float("nan"),
            "mean_ms": statistics.mean(col("time_ms")),
            "median_ms": statistics.median(col("time_ms")),
            "p95_ms": percentile(col("time_ms"), 95),
        }
    return out


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


def write_report(path, meta, corr, perf, alts, findings_path):
    L = []
    add = L.append
    add("# Phase II algorithm evaluation\n")
    add("_Generated by `evaluation/compare_networkx.py` from the output of "
        "`routelens_eval`. Re-run `evaluation/run_evaluation.sh` to reproduce._\n")

    add("## Setup\n")
    add(f"- Road network: Dehradun OSM export, largest strongly connected component, "
        f"{int(meta.get('nodes', 0)):,} vertices, {int(meta.get('edges', 0)):,} directed edges.")
    add(f"- Origin-destination pairs: {meta.get('pairs')} uniformly random vertex pairs "
        f"(seed {meta.get('seed')}); every pair is reachable because the graph is one SCC.")
    add(f"- Alternative-route study: first {meta.get('alt_pairs')} of those pairs, 5 routes requested per method.")
    add(f"- Fastest edge in the graph: {float(meta.get('max_speed_kph', 0)):.0f} km/h "
        f"(the A* time heuristic assumes this speed everywhere, so it is weak when this is high).")
    add("- Edge costs use free-flow travel times only (no live traffic, `traffic_index` = 0), "
        "so these results measure the algorithms, not ETA accuracy.")
    add(f"- Machine: {cpu_name()}, {os.cpu_count()} logical CPUs, {platform.system()} {platform.release()}; "
        f"single-threaded, Release build.")
    add(f"- Reference: NetworkX {nx.__version__} `dijkstra_path_length` on the same edge costs "
        f"(parallel edges collapsed to the cheapest), Python {platform.python_version()}.")
    add("- Weight profiles: " + "; ".join(
        f"**{n}** (per km {w[0]:g}, per minute {w[1]:g}, per congested minute {w[2]:g}, per signal {w[3]:g})"
        for n, w in PROFILES.items()) + ".\n")

    add("## 1. Correctness against NetworkX\n")
    add(f"A result counts as a mismatch when its cost differs from NetworkX's by more than "
        f"{REL_TOL:g} (relative).\n")
    add("| Profile | Pairs | Dijkstra (own heap) | A* | Dijkstra (`std::priority_queue`) | Max relative error |")
    add("|---|---:|---:|---:|---:|---:|")
    for name, s in corr.items():
        worst = max(s[a]["max_rel_err"] for a in ("dijkstra", "astar", "pq"))
        add(f"| {name} | {s['pairs']} | {s['dijkstra']['mismatches']} mismatches | "
            f"{s['astar']['mismatches']} mismatches | {s['pq']['mismatches']} mismatches | {worst:.2e} |")
    add("")

    add("## 2. Dijkstra vs A*\n")
    add("A* uses an admissible, consistent heuristic: straight-line distance, plus a time bound from the "
        "fastest edge speed in the graph. Same optimal cost, fewer vertices expanded.\n")
    add("| Profile | Algorithm | Mean ms | Median ms | 95th pct ms | Mean vertices expanded |")
    add("|---|---|---:|---:|---:|---:|")
    for name, p in perf.items():
        for algo, label in (("dijkstra", "Dijkstra"), ("astar", "A*")):
            a = p[algo]
            add(f"| {name} | {label} | {fmt(a['mean_ms'], 2)} | {fmt(a['median_ms'], 2)} | "
                f"{fmt(a['p95_ms'], 2)} | {fmt(a['mean_expanded'], 0)} |")
    add("")
    add("| Profile | Vertices expanded: Dijkstra / A* | Median per-pair speedup of A* |")
    add("|---|---:|---:|")
    for name, p in perf.items():
        add(f"| {name} | {p['expansion_ratio']:.2f}x | {p['astar_speedup_median']:.2f}x |")
    add("")

    add("## 3. Own binary min-heap vs `std::priority_queue`\n")
    add("Both run the same Dijkstra (lazy deletion, early exit) on the same precomputed costs; "
        "only the priority queue differs. Ratio above 1 means the own heap is faster.\n")
    add("| Profile | Own heap mean ms | `std::priority_queue` mean ms | Median per-pair ratio (std / own) |")
    add("|---|---:|---:|---:|")
    for name, p in perf.items():
        add(f"| {name} | {fmt(p['dijkstra']['mean_ms'], 2)} | {fmt(p['pq']['mean_ms'], 2)} | "
            f"{p['heap_vs_pq_median']:.2f}x |")
    add("")

    add("## 4. Penalty method vs Yen's algorithm\n")
    add("Both methods were asked for 5 routes under the balanced profile. *Overlap* is the share of the "
        "shorter route's length that another route also uses (averaged over route pairs). *Stretch* is a "
        "route's cost divided by the cheapest route's cost. *Usable* counts routes that survive the "
        "RouteLens filter (stretch <= 1.5 and overlap < 0.85 with every route kept).\n")
    add("| Metric | Penalty method | Yen's algorithm |")
    add("|---|---:|---:|")
    p, y = alts.get("penalty"), alts.get("yen")
    if p and y:
        def row(label, key, digits=2, pct=False):
            def f(v):
                return f"{v * 100:.0f}%" if pct else fmt(v, digits)
            add(f"| {label} | {f(p[key])} | {f(y[key])} |")
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


PROFILES = {}


def main():
    global PROFILES
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("results", help="directory written by routelens_eval")
    ap.add_argument("--report", default="docs/evaluation_phase2.md")
    ap.add_argument("--findings", default=os.path.join(os.path.dirname(__file__), "findings.md"),
                    help="hand-written interpretation appended to the report if present")
    args = ap.parse_args()

    PROFILES = load_profiles(args.results)
    meta = read_meta(args.results)

    corr = correctness(args.results, PROFILES)
    perf = performance(args.results, PROFILES)
    alts = alternatives(args.results)
    write_report(args.report, meta, corr, perf, alts, args.findings)

    bad = sum(s[a]["mismatches"] for s in corr.values() for a in ("dijkstra", "astar", "pq"))
    print(f"report written to {args.report}; total mismatches vs NetworkX: {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
