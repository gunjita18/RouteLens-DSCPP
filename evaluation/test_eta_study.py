#!/usr/bin/env python3
"""Offline unit tests for eta_study.py (no server, no network).

Run from the project root:  python3 evaluation/test_eta_study.py
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
import eta_study as es  # noqa: E402

HEADER = "trip_id,from_lat,from_lon,to_lat,to_lon,actual_minutes,depart_time,vehicle,notes\n"


def write(text):
    d = tempfile.TemporaryDirectory()
    path = os.path.join(d.name, "trips.csv")
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return d, path


class LoadTests(unittest.TestCase):
    def test_valid_file_with_optional_columns(self):
        d, path = write(HEADER +
                        "a,30.30,78.00,30.32,78.04,12.5,2026-11-24 09:15,car,school run\n"
                        "\n"
                        "b,30.31,78.01,30.33,78.05,20,,,\n")
        with d:
            trips = es.load_trips(path)
        self.assertEqual([t["trip_id"] for t in trips], ["a", "b"])
        self.assertEqual(trips[0]["actual_minutes"], 12.5)
        self.assertEqual(trips[0]["vehicle"], "car")
        self.assertEqual(trips[1]["depart_time"], "")

    def test_minimal_columns_only(self):
        d, path = write("trip_id,from_lat,from_lon,to_lat,to_lon,actual_minutes\na,30.3,78.0,30.32,78.04,10\n")
        with d:
            self.assertEqual(len(es.load_trips(path)), 1)

    def test_errors_name_the_line(self):
        bad = {
            "missing column": "trip_id,from_lat\na,30\n",
            "not a number": HEADER + "a,30.3,abc,30.32,78.04,10,,,\n",
            "zero minutes": HEADER + "a,30.3,78.0,30.32,78.04,0,,,\n",
            "negative minutes": HEADER + "a,30.3,78.0,30.32,78.04,-5,,,\n",
            "nan minutes": HEADER + "a,30.3,78.0,30.32,78.04,nan,,,\n",
            "latitude range": HEADER + "a,95,78.0,30.32,78.04,10,,,\n",
            "empty id": HEADER + ",30.3,78.0,30.32,78.04,10,,,\n",
            "duplicate id": HEADER + "a,30.3,78.0,30.32,78.04,10,,,\na,30.3,78.0,30.32,78.04,11,,,\n",
        }
        for name, text in bad.items():
            d, path = write(text)
            with d, self.subTest(name):
                with self.assertRaises(ValueError):
                    es.load_trips(path)
        d, path = write(HEADER + "a,30.3,abc,30.32,78.04,10,,,\n")
        with d:
            with self.assertRaises(ValueError) as ctx:
                es.load_trips(path)
        self.assertIn("line 2", str(ctx.exception))

    def test_hour_buckets(self):
        self.assertEqual(es.hour_bucket("2026-11-24 09:15"), "morning rush (07-10)")
        self.assertEqual(es.hour_bucket("12:00"), "midday (10-16)")
        self.assertEqual(es.hour_bucket("2026-11-24 18:30"), "evening rush (16-20)")
        self.assertEqual(es.hour_bucket("22:10"), "other hours")
        self.assertEqual(es.hour_bucket(""), "")
        self.assertEqual(es.hour_bucket("soon"), "")
        self.assertEqual(es.hour_bucket("25:00"), "")


class FakeResponse(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


TRIP = {"from_lat": 30.3, "from_lon": 78.0, "to_lat": 30.32, "to_lon": 78.04}


class FetchTests(unittest.TestCase):
    def test_picks_the_fastest_route(self):
        seen = {}

        def opener(url, timeout):
            seen["url"] = url
            body = {"routes": [
                {"eta_s": 900, "free_flow_eta_s": 800, "distance_km": 5.0},
                {"eta_s": 600, "free_flow_eta_s": 550, "distance_km": 4.0},
                {"eta_s": 700, "free_flow_eta_s": 650, "distance_km": 4.5}]}
            return FakeResponse(json.dumps(body).encode())

        p = es.fetch_prediction("http://x:1/", TRIP, "rain", opener=opener)
        self.assertEqual((p["eta_s"], p["free_flow_s"], p["routes"], p["distance_km"]), (600, 550, 3, 4.0))
        self.assertIn("scenario=rain", seen["url"])
        self.assertIn("w_time=1", seen["url"])
        self.assertTrue(seen["url"].startswith("http://x:1/api/routes?"))

    def test_failures_become_prediction_errors(self):
        def http_error(url, timeout):
            raise urllib.error.HTTPError(url, 422, "x", {}, io.BytesIO(b'{"error":"no road within 500 m"}'))

        def down(url, timeout):
            raise urllib.error.URLError("refused")

        def empty(url, timeout):
            return FakeResponse(b'{"routes": []}')

        def garbage(url, timeout):
            return FakeResponse(b"<html>")

        with self.assertRaises(es.PredictionError) as ctx:
            es.fetch_prediction("http://x", TRIP, opener=http_error)
        self.assertIn("422", str(ctx.exception))
        self.assertIn("no road", str(ctx.exception))
        for opener in (down, empty, garbage):
            with self.assertRaises(es.PredictionError):
                es.fetch_prediction("http://x", TRIP, opener=opener)


def rows_from(pairs):
    """pairs of (actual, predicted) minutes -> rows with errors filled in."""
    rows = []
    for i, (actual, pred) in enumerate(pairs):
        r = {"trip_id": str(i), "actual_minutes": float(actual), "depart_time": "", "notes": "", "vehicle": ""}
        es.add_errors(r, float(pred), "eta")
        es.add_errors(r, float(pred), "free_flow")
        rows.append(r)
    return rows


class StatsTests(unittest.TestCase):
    def test_error_sign_and_summary(self):
        rows = rows_from([(10, 12), (20, 16), (10, 10)])        # +20 %, -20 %, 0 %
        self.assertAlmostEqual(rows[0]["eta_err_pct"], 20.0)
        self.assertAlmostEqual(rows[1]["eta_err_pct"], -20.0)
        s = es.summarise(rows, "eta")
        self.assertEqual(s["n"], 3)
        self.assertAlmostEqual(s["mape_pct"], 40.0 / 3)
        self.assertAlmostEqual(s["bias_pct"], 0.0)
        self.assertAlmostEqual(s["mae_min"], (2 + 4 + 0) / 3)
        self.assertAlmostEqual(s["median_abs_pct"], 20.0)
        self.assertEqual(s["within_20pct"], 1.0)                # 20 % counts as within
        self.assertIsNone(es.summarise([], "eta"))

    def test_consistent_over_prediction_is_fixed_by_the_factor(self):
        rows = rows_from([(10, 15), (20, 30), (30, 45), (40, 60)])      # always 1.5x too long
        self.assertAlmostEqual(es.scale_factor(rows, "eta"), 2.0 / 3.0)
        loo = es.leave_one_out(rows, "eta")
        self.assertAlmostEqual(loo["mape_pct"], 0.0, places=9)
        self.assertAlmostEqual(loo["factor_min"], 2.0 / 3.0)

    def test_noise_is_not_fixed_by_the_factor(self):
        rows = rows_from([(10, 10), (10, 20), (10, 5), (10, 10)])       # no consistent bias
        loo = es.leave_one_out(rows, "eta")
        # Errors are 0, +100, -50, 0 %: mean absolute 37.5 %. A factor learned from the other
        # trips makes it WORSE (57.8 %), which is what the leave-one-out check exists to show.
        self.assertAlmostEqual(es.summarise(rows, "eta")["mape_pct"], 37.5)
        self.assertAlmostEqual(loo["mape_pct"], 57.767857142857146)
        self.assertGreater(loo["mape_pct"], es.summarise(rows, "eta")["mape_pct"])

    def test_leave_one_out_never_uses_the_trip_itself(self):
        rows = rows_from([(10, 10), (10, 10), (10, 100)])
        f_without_outlier = es.scale_factor(rows, "eta", exclude=2)
        self.assertAlmostEqual(f_without_outlier, 1.0)
        loo = es.leave_one_out(rows, "eta")
        self.assertGreater(loo["mape_pct"], 100.0 / 3)          # the outlier stays badly predicted

    def test_too_few_trips_for_loo(self):
        self.assertIsNone(es.leave_one_out(rows_from([(10, 12), (10, 13)]), "eta"))
        self.assertIsNone(es.scale_factor([], "eta"))


class RunTests(unittest.TestCase):
    def test_run_collects_rows_and_failures(self):
        trips = [dict(TRIP, trip_id="ok", actual_minutes=10.0, depart_time="09:00", vehicle="car", notes=""),
                 dict(TRIP, trip_id="bad", actual_minutes=10.0, depart_time="", vehicle="", notes="")]

        def fake(server, trip, scenario):
            if trip["trip_id"] == "bad":
                raise es.PredictionError("HTTP 422 no road")
            return {"eta_s": 720.0, "free_flow_s": 600.0, "routes": 2, "distance_km": 3.0}

        rows, failed = es.run(trips, "http://x", "normal", fetch=fake)
        self.assertEqual(len(rows), 1)
        self.assertAlmostEqual(rows[0]["eta_min"], 12.0)
        self.assertAlmostEqual(rows[0]["eta_err_pct"], 20.0)
        self.assertAlmostEqual(rows[0]["free_flow_err_pct"], 0.0)
        self.assertEqual(failed, [("bad", "HTTP 422 no road")])

    def test_report_mentions_small_sample_and_failures(self):
        rows = rows_from([(10, 12), (20, 18), (15, 15)])
        text = es.format_report(rows, [("x", "HTTP 422")], "normal")
        self.assertIn("small sample", text)
        self.assertIn("FAILED: HTTP 422", text)
        self.assertIn("leave-one-out", text)
        self.assertIn("positive = predicted too long", text)

    def test_csv_output(self):
        rows = rows_from([(10, 12)])
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "out.csv")
            es.write_csv(path, rows)
            with open(path, encoding="utf-8") as f:
                text = f.read()
        self.assertIn("trip_id,depart_time,vehicle,actual_minutes,eta_min,eta_err_pct", text)
        self.assertIn("20.00", text)


class MainTests(unittest.TestCase):
    def call(self, args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = es.main(args)
        return code, out.getvalue(), err.getvalue()

    def test_bad_file_and_empty_selection(self):
        code, _, err = self.call(["--trips", "/nonexistent/trips.csv"])
        self.assertEqual(code, 2)
        d, path = write(HEADER + "a,30.3,78.0,30.32,78.04,10,,car,\n")
        with d:
            code, _, err = self.call(["--trips", path, "--vehicle", "bike"])
        self.assertEqual(code, 2)
        self.assertIn("no trips", err)

    def test_server_down_is_reported(self):
        d, path = write(HEADER + "a,30.3,78.0,30.32,78.04,10,,,\n")
        with d:
            code, _, err = self.call(["--trips", path, "--server", "http://127.0.0.1:9"])
        self.assertEqual(code, 1)
        self.assertIn("no trip got a prediction", err)


if __name__ == "__main__":
    unittest.main(verbosity=1)
