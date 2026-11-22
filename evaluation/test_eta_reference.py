#!/usr/bin/env python3
"""Offline unit tests for eta_reference.py (no server, no network, no API key).

Run from the project root:  python3 evaluation/test_eta_reference.py
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
import eta_reference as er  # noqa: E402
from eta_study import PredictionError  # noqa: E402

A = ("Alpha", 30.3243, 78.0418)
B = ("Beta", 30.3207, 78.0507)


class FakeResponse(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


def opener_returning(body):
    seen = {}

    def opener(request, timeout):
        seen["url"] = request.full_url
        seen["ua"] = request.get_header("User-agent")
        return FakeResponse(json.dumps(body).encode())
    opener.seen = seen
    return opener


class PairTests(unittest.TestCase):
    def points(self):
        # a line of points roughly 1 km apart along latitude
        return [("p%d" % i, 30.30 + 0.009 * i, 78.0) for i in range(8)]

    def test_pairs_are_deterministic_and_within_distance(self):
        p1 = er.make_pairs(self.points(), 10, 1.5, 4.0, seed=1)
        p2 = er.make_pairs(self.points(), 10, 1.5, 4.0, seed=1)
        p3 = er.make_pairs(self.points(), 10, 1.5, 4.0, seed=2)
        self.assertEqual(p1, p2)
        self.assertNotEqual(p1, p3)
        self.assertEqual(len(p1), 10)
        for a, b in p1:
            km = er.haversine_m(a[1:], b[1:]) / 1000.0
            self.assertTrue(1.5 <= km <= 4.0, km)
            self.assertNotEqual(a, b)

    def test_not_enough_candidates(self):
        self.assertEqual(er.make_pairs(self.points(), 5, 50.0, 60.0, 1), [])
        self.assertLessEqual(len(er.make_pairs(self.points(), 1000, 1.5, 4.0, 1)), 56)

    def test_load_junctions_skips_pending_rows(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "j.csv")
            with open(path, "w", encoding="utf-8") as f:
                f.write("name,lat,lon,status\nClock Tower,30.32431,78.04183,ok\nPending,,,pending\nBad,x,y,ok\n")
            pts = er.load_junctions(path)
        self.assertEqual(pts, [("Clock Tower", 30.32431, 78.04183)])

    def test_haversine(self):
        self.assertAlmostEqual(er.haversine_m((0, 0), (1, 0)), 111195.0, delta=100)


class ServiceTests(unittest.TestCase):
    def test_osrm_uses_lon_lat_order_and_identifies_itself(self):
        op = opener_returning({"code": "Ok", "routes": [{"duration": 112.1, "distance": 1686.6}]})
        got = er.fetch_osrm(A, B, opener=op)
        self.assertEqual(got, {"time_s": 112.1, "distance_m": 1686.6})
        self.assertIn("/driving/78.041800,30.324300;78.050700,30.320700", op.seen["url"])
        self.assertIn("RouteLensStudentProject", op.seen["ua"])

    def test_osrm_no_route(self):
        with self.assertRaises(er.ReferenceError):
            er.fetch_osrm(A, B, opener=opener_returning({"code": "NoRoute", "routes": []}))

    def test_tomtom_summary_fields(self):
        body = {"routes": [{"summary": {"lengthInMeters": 2000, "travelTimeInSeconds": 300,
                                        "noTrafficTravelTimeInSeconds": 240, "trafficDelayInSeconds": 60}}]}
        op = opener_returning(body)
        got = er.fetch_tomtom(A, B, "KEY123", opener=op)
        self.assertEqual(got, {"time_s": 300.0, "no_traffic_s": 240.0, "distance_m": 2000.0})
        self.assertIn("/30.324300,78.041800:30.320700,78.050700/json", op.seen["url"])
        self.assertIn("traffic=true", op.seen["url"])

    def test_tomtom_without_no_traffic_field_falls_back(self):
        body = {"routes": [{"summary": {"lengthInMeters": 2000, "travelTimeInSeconds": 300}}]}
        self.assertEqual(er.fetch_tomtom(A, B, "K", opener=opener_returning(body))["no_traffic_s"], 300.0)

    def test_tomtom_bad_answers(self):
        for body in ({}, {"routes": []}, {"routes": [{"summary": {}}]},
                     {"routes": [{"summary": {"lengthInMeters": 5}}]}):
            with self.assertRaises(er.ReferenceError):
                er.fetch_tomtom(A, B, "K", opener=opener_returning(body))

    def test_errors_never_contain_the_url_or_key(self):
        def forbidden(request, timeout):
            raise urllib.error.HTTPError(request.full_url, 403, "no", {}, None)

        def down(request, timeout):
            raise urllib.error.URLError("refused")

        for opener in (forbidden, down):
            with self.assertRaises(er.ReferenceError) as ctx:
                er.fetch_tomtom(A, B, "SECRET-KEY", opener=opener)
            self.assertNotIn("SECRET-KEY", str(ctx.exception))
            self.assertNotIn("api.tomtom.com", str(ctx.exception))
            self.assertIsNone(ctx.exception.__cause__)


class StatsTests(unittest.TestCase):
    def test_compare(self):
        s = er.compare([(110, 100), (200, 200), (330, 300)])         # ratios 1.1, 1.0, 1.1
        self.assertEqual(s["n"], 3)
        self.assertAlmostEqual(s["median_ratio"], 1.1)
        self.assertAlmostEqual(s["mean_abs_diff_pct"], (10 + 0 + 10) / 3)
        self.assertGreater(s["r"], 0.99)

    def test_compare_edge_cases(self):
        self.assertIsNone(er.compare([]))
        self.assertIsNone(er.compare([(5, 0), (None, 3), (4, None)]))   # zero or missing reference
        self.assertIsNone(er.compare([(1, 1), (2, 2)])["r"])            # fewer than 3 points: no correlation
        self.assertIsNone(er.compare([(1, 5), (2, 5), (3, 5)])["r"])    # constant reference: undefined
        self.assertLess(er.compare([(1, 3), (2, 2), (3, 1)])["r"], -0.99)


class RunTests(unittest.TestCase):
    def fake_mine(self, a, b):
        return {"eta_s": 300.0, "free_flow_s": 270.0, "distance_km": 2.0}

    def test_run_collects_rows_and_failures(self):
        calls = []

        def osrm(a, b):
            calls.append((a[0], b[0]))
            return {"time_s": 250.0, "distance_m": 1900.0}

        def mine(a, b):
            if a[0] == "Gamma":
                raise PredictionError("HTTP 422")
            return self.fake_mine(a, b)

        pairs = [(A, B), (("Gamma", 30.0, 78.0), B)]
        sleeps = []
        rows, failures = er.run(pairs, "http://x", "normal", ["osrm"], routelens=mine, osrm=osrm,
                                sleep=sleeps.append, delay_s=1.0)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["osrm_time_s"], 250.0)
        self.assertEqual(rows[0]["distance_m"], 2000.0)
        self.assertEqual(failures, [("Gamma -> Beta", "RouteLens: HTTP 422")])
        self.assertEqual(calls, [("Alpha", "Beta")])                 # no reference call for a failed pair
        self.assertEqual(sleeps, [1.0])                              # one polite pause per reference call

    def test_reference_failure_keeps_the_row(self):
        def osrm(a, b):
            raise er.ReferenceError("HTTP 429")
        rows, failures = er.run([(A, B)], "http://x", "normal", ["osrm"], routelens=self.fake_mine,
                                osrm=osrm, sleep=lambda s: None)
        self.assertEqual(len(rows), 1)
        self.assertNotIn("osrm_time_s", rows[0])
        self.assertEqual(failures, [("Alpha -> Beta", "osrm: HTTP 429")])

    def test_missing_free_flow_falls_back_to_eta(self):
        mine = lambda a, b: {"eta_s": 300.0, "free_flow_s": 0.0, "distance_km": 2.0}   # noqa: E731
        rows, _ = er.run([(A, B)], "http://x", "normal", [], routelens=mine)
        self.assertEqual(rows[0]["free_flow_s"], 300.0)

    def test_report_text(self):
        rows = [{"pair": "x", "straight_km": 2.0, "eta_s": 300.0, "free_flow_s": 270.0, "distance_m": 2000.0,
                 "osrm_time_s": 250.0, "osrm_distance_m": 1900.0},
                {"pair": "y", "straight_km": 3.0, "eta_s": 600.0, "free_flow_s": 540.0, "distance_m": 4000.0,
                 "osrm_time_s": 500.0, "osrm_distance_m": 3900.0},
                {"pair": "z", "straight_km": 4.0, "eta_s": 900.0, "free_flow_s": 800.0, "distance_m": 6000.0,
                 "osrm_time_s": 760.0, "osrm_distance_m": 5800.0}]
        text = er.format_report(rows, [("w", "osrm: HTTP 429")], ["osrm"])
        self.assertIn("RouteLens ETA (with snapshot) vs OSRM time", text)
        self.assertNotIn("TomTom", text.split("READ THIS")[0])
        self.assertIn("agreement is not accuracy", text)
        self.assertIn("failed: w: osrm: HTTP 429", text)

    def test_csv(self):
        rows = [{"pair": "x", "straight_km": 2.0, "eta_s": 300.0, "free_flow_s": 270.0, "distance_m": 2000.0,
                 "osrm_time_s": 250.0}]
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "o.csv")
            er.write_csv(path, rows)
            with open(path, encoding="utf-8") as f:
                text = f.read()
        self.assertIn("pair,straight_km,eta_s", text)
        self.assertIn("300.0", text)


class MainTests(unittest.TestCase):
    def call(self, args, env=None):
        out, err = io.StringIO(), io.StringIO()
        saved = os.environ.pop("TOMTOM_API_KEY", None)
        if env:
            os.environ.update(env)
        try:
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                code = er.main(args)
        finally:
            os.environ.pop("TOMTOM_API_KEY", None)
            if saved is not None:
                os.environ["TOMTOM_API_KEY"] = saved
        return code, out.getvalue(), err.getvalue()

    def test_option_errors(self):
        code, _, err = self.call(["--refs", "google"])
        self.assertEqual(code, 2)
        code, _, err = self.call(["--refs", "tomtom"])
        self.assertEqual(code, 2)
        self.assertIn("TOMTOM_API_KEY", err)
        code, _, err = self.call(["--junctions", "/nonexistent.csv"])
        self.assertEqual(code, 2)

    def test_no_pairs_in_range(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "j.csv")
            with open(path, "w", encoding="utf-8") as f:
                f.write("name,lat,lon\nA,30.30,78.00\nB,30.31,78.01\n")
            code, _, err = self.call(["--junctions", path, "--min-km", "50", "--max-km", "60"])
        self.assertEqual(code, 2)
        self.assertIn("no junction pairs", err)

    def test_server_down(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "j.csv")
            with open(path, "w", encoding="utf-8") as f:
                f.write("name,lat,lon\nA,30.30,78.00\nB,30.31,78.01\n")
            code, _, err = self.call(["--junctions", path, "--min-km", "0.5", "--max-km", "5",
                                      "--server", "http://127.0.0.1:9", "--pairs", "1"])
        self.assertEqual(code, 1)
        self.assertIn("no pair was answered", err)


if __name__ == "__main__":
    unittest.main(verbosity=1)
