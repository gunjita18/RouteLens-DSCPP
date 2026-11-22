#!/usr/bin/env python3
"""Offline unit tests for fetch_traffic.py (no network, no API key).

Run from the project root:  python3 preprocess/test_fetch_traffic.py
"""
import contextlib
import io
import json
import os
import sys
import tempfile
import unittest
import urllib.error

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fetch_traffic as ft  # noqa: E402

LAT0, LON0 = 30.30, 78.00
DEG_LAT_M = 111195.0                      # metres per degree of latitude


def segment(lat1, lon1, lat2, lon2, current=30.0, freeflow=40.0, confidence=0.9):
    return {"current": current, "freeflow": freeflow, "confidence": confidence,
            "shape": [(lat1, lon1), (lat2, lon2)]}


def edge(edge_id, lon, highway="primary", lat=LAT0):
    """An edge about 20 m long whose midpoint is (lat, lon)."""
    d = 0.0001
    return edge_id, highway, [(lat, lon - d), (lat, lon + d)]


class FakeFetch:
    def __init__(self, handler):
        self.handler, self.calls = handler, []

    def __call__(self, point):
        self.calls.append(point)
        return self.handler(point)


def run(edges, fetch, **kw):
    kw.setdefault("sleep", lambda s: None)
    return ft.build_snapshot(edges, fetch, **kw)


class GeometryTests(unittest.TestCase):
    def test_haversine_one_degree(self):
        self.assertAlmostEqual(ft.haversine_m((0, 0), (1, 0)), DEG_LAT_M, delta=100)
        self.assertEqual(ft.haversine_m((30, 78), (30, 78)), 0.0)

    def test_point_to_polyline(self):
        line = [(30.0, 78.0), (30.0, 78.01)]
        d = ft.point_to_polyline_m((30.0 + 0.00045, 78.005), line)      # about 50 m north
        self.assertTrue(49.0 < d < 51.0, d)
        end = ft.point_to_polyline_m((30.0, 78.02), line)               # beyond the end: distance to the endpoint
        self.assertAlmostEqual(end, ft.haversine_m((30.0, 78.02), (30.0, 78.01)), delta=15)
        self.assertAlmostEqual(ft.point_to_polyline_m((30.0, 78.001), [(30.0, 78.001)]), 0.0, places=6)

    def test_midpoint_straight_and_bent(self):
        mid = ft.polyline_midpoint([(30.0, 78.0), (30.0, 78.002)])
        self.assertAlmostEqual(mid[1], 78.001, places=6)
        # L-shape: 1 km east then 0.1 km north: the midpoint lies on the long leg.
        bent = [(30.0, 78.0), (30.0, 78.0104), (30.0009, 78.0104)]
        m = ft.polyline_midpoint(bent)
        self.assertAlmostEqual(m[0], 30.0, places=6)
        self.assertTrue(78.004 < m[1] < 78.006)
        self.assertEqual(ft.polyline_midpoint([(30.1, 78.1), (30.1, 78.1)]), (30.1, 78.1))

    def test_parse_geometry(self):
        self.assertEqual(ft.parse_geometry("78.0 30.0;78.1 30.1"), [(30.0, 78.0), (30.1, 78.1)])
        self.assertIsNone(ft.parse_geometry("78.0 30.0"))               # one point
        self.assertIsNone(ft.parse_geometry("78.0 30.0;abc 30.1"))
        self.assertIsNone(ft.parse_geometry("78.0 30.0;78.1"))
        self.assertIsNone(ft.parse_geometry("78.0 95.0;78.1 30.1"))     # latitude out of range
        self.assertIsNone(ft.parse_geometry(""))


class InputTests(unittest.TestCase):
    def test_edge_ids_count_data_rows_like_the_loader(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "edges.csv")
            with open(path, "w", encoding="utf-8") as f:
                f.write("u,v,length_m,highway,maxspeed_kph,oneway,geometry\n"
                        "1,2,10,primary,,0,78.0 30.0;78.1 30.1\n"
                        "\n"
                        "2,3,10,primary,,0,not a shape\n"
                        "3,4,10,residential,,0,78.0 30.0;78.2 30.2\n")
            rows = list(ft.read_edges(path))
        self.assertEqual([r[0] for r in rows], [0, 1, 2])               # blank line is not a row
        self.assertIsNotNone(rows[0][2])
        self.assertIsNone(rows[1][2])                                   # bad shape keeps its id
        self.assertEqual(rows[2][1], "residential")


class ParseTests(unittest.TestCase):
    DOC = {"flowSegmentData": {"frc": "FRC2", "currentSpeed": 33, "freeFlowSpeed": 42,
                               "confidence": 0.6,
                               "coordinates": {"coordinate": [{"latitude": 30.1, "longitude": 78.1},
                                                              {"latitude": 30.2, "longitude": 78.2}]}}}

    def test_good(self):
        s = ft.parse_flow_segment(self.DOC)
        self.assertEqual((s["current"], s["freeflow"], s["confidence"]), (33.0, 42.0, 0.6))
        self.assertEqual(s["shape"], [(30.1, 78.1), (30.2, 78.2)])

    def test_bad(self):
        for mutate in (lambda d: d.pop("flowSegmentData"),
                       lambda d: d["flowSegmentData"].pop("currentSpeed"),
                       lambda d: d["flowSegmentData"].update(confidence=1.5),
                       lambda d: d["flowSegmentData"].update(freeFlowSpeed=0),
                       lambda d: d["flowSegmentData"].update(currentSpeed=-1),
                       lambda d: d["flowSegmentData"]["coordinates"].update(coordinate=[])):
            doc = json.loads(json.dumps(self.DOC))
            mutate(doc)
            with self.assertRaises(ValueError):
                ft.parse_flow_segment(doc)


class SnapshotTests(unittest.TestCase):
    def test_segment_reuse_saves_requests(self):
        road = segment(LAT0, 78.00, LAT0, 78.05)                        # about 5 km of road
        f = FakeFetch(lambda p: ("ok", road))
        edges = [edge(10, 78.01), edge(11, 78.02), edge(12, 78.03), edge(13, 78.03, "residential")]
        rows, st, abort = run(edges, f)
        self.assertEqual(len(f.calls), 1)
        self.assertEqual((st["candidates"], st["requests"], st["reused"], st["recorded"]), (3, 1, 2, 3))
        self.assertEqual([r[0] for r in rows], [10, 11, 12])            # residential edge ignored
        self.assertEqual(rows[0][1:], (30.0, 40.0, 0.9))
        self.assertIsNone(abort)

    def test_wrong_road_is_not_recorded_and_not_asked_again(self):
        far = segment(LAT0 + 0.001, 78.00, LAT0 + 0.001, 78.05)         # about 111 m north
        f = FakeFetch(lambda p: ("ok", far))
        rows, st, _ = run([edge(1, 78.01), edge(2, 78.0101)], f)        # second midpoint is ~10 m from the first
        self.assertEqual(rows, [])
        self.assertEqual((st["wrong_road"], st["requests"], st["no_coverage"]), (1, 1, 1))

    def test_just_inside_fifty_metres_is_accepted(self):
        near = segment(LAT0 + 0.00040, 78.00, LAT0 + 0.00040, 78.05)    # about 44 m north
        rows, st, _ = run([edge(1, 78.01)], FakeFetch(lambda p: ("ok", near)))
        self.assertEqual(len(rows), 1)
        self.assertEqual(st["wrong_road"], 0)

    def test_low_confidence_is_ignored(self):
        road = segment(LAT0, 78.00, LAT0, 78.05, confidence=0.49)
        f = FakeFetch(lambda p: ("ok", road))
        rows, st, _ = run([edge(1, 78.01), edge(2, 78.02)], f)
        self.assertEqual(rows, [])
        self.assertEqual((st["low_confidence"], st["requests"]), (2, 1))

    def test_confidence_exactly_half_is_kept(self):
        road = segment(LAT0, 78.00, LAT0, 78.05, confidence=0.5)
        rows, _, _ = run([edge(1, 78.01)], FakeFetch(lambda p: ("ok", road)))
        self.assertEqual(len(rows), 1)

    def test_no_coverage_is_remembered(self):
        f = FakeFetch(lambda p: ("no_coverage", None))
        rows, st, _ = run([edge(1, 78.01), edge(2, 78.0101)], f)
        self.assertEqual(rows, [])
        self.assertEqual((st["requests"], st["no_coverage"]), (1, 2))

    def test_request_budget(self):
        f = FakeFetch(lambda p: ("ok", segment(p[0], p[1] - 0.0005, p[0], p[1] + 0.0005)))
        edges = [edge(i, 78.0 + 0.01 * i) for i in range(4)]            # about 1 km apart: no reuse
        rows, st, abort = run(edges, f, max_requests=2)
        self.assertEqual((st["requests"], st["not_fetched"], len(rows)), (2, 2, 2))
        self.assertIn("limit", abort)

    def test_abort_on_rejected_key(self):
        def handler(p):
            raise ft.FetchAbort("TomTom rejected the API key (HTTP 403)")
        rows, st, abort = run([edge(1, 78.01), edge(2, 78.05)], FakeFetch(handler))
        self.assertEqual(rows, [])
        self.assertIn("rejected", abort)
        self.assertEqual(st["requests"], 1)                             # stopped after the first call

    def test_abort_after_consecutive_errors(self):
        f = FakeFetch(lambda p: ("error", None))
        edges = [edge(i, 78.0 + 0.01 * i) for i in range(7)]
        rows, st, abort = run(edges, f)
        self.assertEqual(st["requests"], ft.MAX_CONSECUTIVE_FAILURES)
        self.assertIn("unreachable", abort)
        self.assertEqual(st["not_fetched"], 2)

    def test_success_resets_error_streak(self):
        answers = iter([("error", None)] * 4 + [("ok", segment(LAT0, 78.0, LAT0, 78.0001))] + [("error", None)] * 4)
        f = FakeFetch(lambda p: next(answers))
        edges = [edge(i, 78.0 + 0.01 * i) for i in range(9)]
        _, st, abort = run(edges, f)
        self.assertIsNone(abort)                                        # never 5 failures in a row
        self.assertEqual(st["errors"], 8)

    def test_rate_limit_sleeps_between_requests(self):
        sleeps = []
        f = FakeFetch(lambda p: ("no_coverage", None))
        edges = [edge(i, 78.0 + 0.01 * i) for i in range(4)]
        run(edges, f, sleep=sleeps.append, clock=lambda: 0.0)           # time never advances
        self.assertEqual(len(sleeps), 3)                                # between 4 requests
        self.assertTrue(all(abs(s - ft.MIN_INTERVAL_S) < 1e-9 for s in sleeps))

    def test_bad_geometry_counted(self):
        rows, st, _ = run([(1, "primary", None)], FakeFetch(lambda p: ("ok", None)))
        self.assertEqual((st["bad_geometry"], st["candidates"]), (1, 0))


class FakeResponse(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class MakeFetchTests(unittest.TestCase):
    KEY = "SECRET-KEY-123"

    def fetch_with(self, behaviour):
        seen = {}

        def opener(request, timeout):
            seen["url"] = request.full_url
            return behaviour(request)
        return ft.make_fetch(self.KEY, opener=opener), seen

    def http_error(self, code):
        def behave(request):
            raise urllib.error.HTTPError(request.full_url, code, "msg", {}, None)
        return behave

    def test_ok(self):
        body = json.dumps(ParseTests.DOC).encode()
        fetch, seen = self.fetch_with(lambda r: FakeResponse(body))
        status, seg = fetch((30.3, 78.0))
        self.assertEqual(status, "ok")
        self.assertEqual(seg["current"], 33.0)
        self.assertIn("point=30.300000%2C78.000000", seen["url"])
        self.assertIn("/absolute/10/json", seen["url"])

    def test_statuses(self):
        self.assertEqual(self.fetch_with(self.http_error(400))[0]((30.3, 78.0)), ("no_coverage", None))
        self.assertEqual(self.fetch_with(self.http_error(500))[0]((30.3, 78.0)), ("error", None))

        def down(request):
            raise urllib.error.URLError("no route to host")
        self.assertEqual(self.fetch_with(down)[0]((30.3, 78.0)), ("error", None))
        self.assertEqual(self.fetch_with(lambda r: FakeResponse(b"not json"))[0]((30.3, 78.0)), ("error", None))
        self.assertEqual(self.fetch_with(lambda r: FakeResponse(b"{}"))[0]((30.3, 78.0)), ("error", None))

    def test_abort_messages_never_contain_the_key(self):
        for code in (401, 403, 429):
            fetch, seen = self.fetch_with(self.http_error(code))
            with self.assertRaises(ft.FetchAbort) as ctx:
                fetch((30.3, 78.0))
            self.assertNotIn(self.KEY, str(ctx.exception))
            self.assertNotIn("api.tomtom.com", str(ctx.exception))
            self.assertIsNone(ctx.exception.__cause__)                  # no chained error carrying the URL


class OutputTests(unittest.TestCase):
    def test_format_matches_the_c_reader_contract(self):
        text = ft.format_snapshot([(1452, 33.0, 42.0, 0.6), (7, 20.5, 40.0, 0.95)], "2026-11-10 09:30")
        self.assertEqual(text.splitlines()[1:], [
            "# fetched_at = 2026-11-10 09:30",
            "edge_id,current_kph,freeflow_kph,confidence",
            "1452,33,42,0.60",
            "7,20.5,40,0.95"])
        self.assertTrue(text.startswith("# traffic.csv"))

    def test_partial_note(self):
        text = ft.format_snapshot([], "2026-11-10 09:30", note="partial: limit")
        self.assertIn("# note = partial: limit\n", text)

    def test_write_is_atomic(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "sub", "traffic.csv")
            ft.write_snapshot(path, [(1, 30.0, 40.0, 0.9)], "2026-01-01 00:00")
            self.assertTrue(os.path.exists(path))
            self.assertFalse(os.path.exists(path + ".tmp"))
            with open(path, encoding="utf-8") as f:
                self.assertIn("1,30,40,0.90", f.read())


class MainTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.edges = os.path.join(self.dir.name, "edges.csv")
        with open(self.edges, "w", encoding="utf-8") as f:
            f.write("u,v,length_m,highway,maxspeed_kph,oneway,geometry\n"
                    "1,2,10,primary,,0,78.0 30.3;78.001 30.3\n")
        self.saved = os.environ.pop("TOMTOM_API_KEY", None)

    def tearDown(self):
        self.dir.cleanup()
        if self.saved is not None:
            os.environ["TOMTOM_API_KEY"] = self.saved

    def call_main(self, args):
        """Runs main() with its console output captured; returns (exit code, stdout, stderr)."""
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = ft.main(args)
        return code, out.getvalue(), err.getvalue()

    def test_missing_key_is_an_error(self):
        out = os.path.join(self.dir.name, "t.csv")
        code, _, err = self.call_main(["--edges", self.edges, "--out", out])
        self.assertEqual(code, 2)
        self.assertIn("TOMTOM_API_KEY", err)
        self.assertFalse(os.path.exists(out))

    def test_missing_edges_file(self):
        code, _, err = self.call_main(["--edges", os.path.join(self.dir.name, "nope.csv")])
        self.assertEqual(code, 2)
        self.assertIn("not found", err)

    def test_dry_run_needs_no_key_and_writes_nothing(self):
        out = os.path.join(self.dir.name, "t.csv")
        code, stdout, _ = self.call_main(["--edges", self.edges, "--out", out, "--dry-run"])
        self.assertEqual(code, 0)
        self.assertIn("1 candidate edges", stdout)
        self.assertFalse(os.path.exists(out))


if __name__ == "__main__":
    unittest.main(verbosity=1)
