# ETA accuracy study

> If no timed trips are available, see `docs/ETA_CROSS_CHECK.md`: a plausibility check against
> another routing engine and published speeds. It is not a substitute for this study.

**Question.** How close is RouteLens's predicted travel time to the time a real trip takes?
The road-class speeds (20 km/h on residential roads and so on) and the 30 s wait at each
junction are provisional assumptions; this study measures how wrong they are.

**Method.** Time real trips, ask the running server for the same trips, compare.
`evaluation/eta_study.py` does the comparison; the trips come from you.

## 1. Collect the trips

Ground truth has to be a real journey. The prediction cannot check itself.

Where the numbers can come from:

| Source | What you get | Watch out for |
|---|---|---|
| Your own journeys, timed with a phone stopwatch | Door-to-door minutes | Start the clock when you move, stop when you arrive; do not count parking or waiting for a friend |
| Phone location history (Google Maps Timeline, Strava, or a GPS tracker app) | Start time, end time and the route taken | Check the end time is arrival, not when you left the building |
| Ride-app trip history (Ola, Uber, Rapido) | Pickup, drop and ride duration | Use the ride duration, not the waiting time before pickup |
| Classmates' or family's regular commutes | More trips, more roads | Ask for the time of day and the vehicle |

Not suitable as ground truth: times shown by a navigation app before you travel. They are
another model's prediction. They can be used as a separate comparison, but do not scrape them
(the services' terms forbid it); read a few by hand and say so in the report.

**How many.** At least 10 to start; 20 to 30 if you can. Spread them over the day: morning rush
(07-10), midday (10-16), evening rush (16-20). Include short and long trips, main roads and side
streets. Repeating the same trip at different hours is very useful for the rush-hour factors.

**One vehicle type per file.** Car and two-wheeler times differ a lot (a two-wheeler filters
through traffic). Use the `vehicle` column and run the study per vehicle with `--vehicle`.

## 2. Fill in the file

Copy `evaluation/trips_template.csv` to `evaluation/trips.csv` and add one line per trip:

| Column | Meaning |
|---|---|
| `trip_id` | any unique name, e.g. `t01` |
| `from_lat`, `from_lon` | start point (see below) |
| `to_lat`, `to_lon` | end point |
| `actual_minutes` | how long the trip really took, in minutes (a decimal is fine) |
| `depart_time` | optional: `2026-11-24 09:15` or `09:15` (used for the time-of-day breakdown) |
| `vehicle` | optional: `car` or `bike` |
| `notes` | optional: anything unusual (rain, accident, long red light) |

Illustrative line (an example of the format, **not a measurement**):

```
t01,30.3243,78.0418,30.3207,78.0507,6.5,2026-11-24 09:15,car,
```

To get coordinates, right-click the point on Google Maps (or OpenStreetMap) and copy the numbers.
Pick points on or next to a road: the server snaps a click to the nearest road within 500 m and
refuses points farther away.

## 3. Run it

Start the server, then run the study (repository root):

```bash
./build/routelens_server &                          # needs data/osm/ and, ideally, data/traffic/traffic.csv
python3 evaluation/eta_study.py --trips evaluation/trips.csv --out evaluation/results/eta_study.csv
python3 evaluation/eta_study.py --trips evaluation/trips.csv --scenario rush_hour --vehicle car
```

For each trip it takes the **fastest** route the server offers (the one a driver in a hurry would
choose) and compares its ETA with `actual_minutes`. It prints, for the RouteLens ETA and for the
free-flow ETA (no traffic data): the mean absolute error in minutes and percent, the bias (too
long or too short on average), the share of trips within 20 %, and a time-of-day breakdown.

## 4. Reading the result

- **Error sign.** `(predicted - actual) / actual`; positive means RouteLens predicted too long.
- **Bias vs noise.** A bias far from 0 with small scatter means the speeds are consistently off
  and a correction would help. Scatter around 0 means no single factor will fix it.
- **Correction factor.** The script reports `sum(actual) / sum(predicted)` and, more
  importantly, the error you would get if each trip were corrected by a factor learned from the
  *other* trips (leave-one-out). Only trust a correction if that number is clearly lower than the
  uncorrected error. If it is, scale the road-class speeds in `src/data/osm_loader.cpp`
  (`defaultSpeedKph`) by the inverse factor, then **re-run on new trips** that were not used to
  fit it.
- **Traffic ETA vs free-flow ETA.** If the traffic ETA is not closer to reality than the
  free-flow one, say so: it is a finding about how much the snapshot helps.

## 5. Limits to state in the report

- A handful of trips is a pilot, not a statistically reliable accuracy. List the trips.
- The server uses one saved snapshot. Its traffic data is from the snapshot time, so a trip
  taken at another time of day is compared with traffic from a different moment. Prefer trips close
  to the snapshot time for the traffic comparison, or take a snapshot at the time you drive.
- A person's route may differ from the fastest route the server finds.
- Local speeds (two-wheelers, driving style, parking, red lights beyond the 30 s average) are
  not modelled.
- Traffic snapshot coverage is partial: only main roads have measured data, so side-street
  parts of a trip always use the free-flow estimates.
