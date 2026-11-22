# ETA cross-check: reference routing services and published speeds

**This is not an accuracy study.** No timed real trips were collected (see `docs/ETA_STUDY.md`
for how that study would be done). This document checks whether RouteLens's route times and
distances are *plausible*, by comparing them with an independent routing engine and with
published travel speeds for Indian cities. Agreement with another prediction is not accuracy.

## 1. Cross-check against OSRM (run on 2026-11-22)

- Tool: `evaluation/eta_reference.py`; raw per-pair results are written to
  `evaluation/results/` (not committed).
- Points: 30 random pairs of the documented major junctions, 1.5 to 8 km apart in a straight
  line, fixed seed 42.
- RouteLens: the running server, scenario `normal`, with the traffic snapshot of
  2026-11-22 10:08 (2,020 edges with data, a quiet Sunday-morning capture). For each pair, the
  fastest route offered.
- Reference: the OSRM public demo server, car profile (generic speeds per road class, no
  traffic, no waiting at junctions).

| Comparison (30 pairs) | median ratio RouteLens / OSRM | mean absolute difference | correlation r |
|---|---:|---:|---:|
| Route distance | 0.99 | 7.0 % | 0.94 |
| ETA with snapshot vs OSRM time | 2.13 | 105 % | 0.84 |
| Free-flow ETA vs OSRM time | 1.72 | 78 % | 0.86 |

Median average speed over the pairs (own distance / own time):

| | km/h |
|---|---:|
| RouteLens ETA (with snapshot) | 24.8 |
| RouteLens free-flow ETA | 27.8 |
| OSRM | 53.7 |

The `rush_hour` scenario on the same pairs gave a median ratio of 2.20 (correlation 0.89).

## 2. Published speeds for Indian cities

Akbar, Couture, Duranton and Storeygard, *Mobility and Congestion in Urban India*, NBER Working
Paper 25218 (November 2018, <http://www.nber.org/papers/w25218>). They collected Google Maps
predicted travel times for 22.7 million trips (longer than 1 km) in 154 Indian cities. Figures
below are copied from their Tables 1 and 3:

| Quantity | km/h |
|---|---:|
| Trip speed, mean (all trips, Table 1) | 22.1 |
| Trip speed, median / 75th / 90th / 99th percentile (Table 1) | 20.6 / 25.4 / 31.6 / 45.8 |
| Mean city speed, cross-city average (Table 3) | 24.4 (slowest city 16.2, fastest 34.9) |
| Mean city speed in the absence of traffic (Table 3) | 26.8 (range 16.3 to 38.1) |

They also report that the average trip under actual traffic lasts about 13 % longer than its
no-traffic counterpart (mean duration 20.0 against 17.2 minutes, Table 1).

Limits of this source: the numbers are Google's *predicted* travel times, not measured trips;
the paper is a working paper that has not been peer-reviewed (it says so on its first page);
Dehradun is not reported separately (the text does not mention it); the trip mix differs
(mean trip length 8.2 km, against 1.5 to 8 km straight-line here). I searched for a published
Dehradun average speed and found none; the Dehradun mobility-plan annexure
(`ukmrc.org`, the source of the junction list) gave no speed text I could extract.

## 3. What the results support

1. **The road network and the routing are sound.** RouteLens and OSRM choose routes of almost
   the same length (median ratio 0.99, correlation 0.94), and trips that are longer for one are
   longer for the other (time correlation 0.84 to 0.86).
2. **RouteLens's implied speeds are in the range of Indian cities.** 24.8 km/h with the
   snapshot (27.8 free-flow) lies between the median (20.6) and the 75th percentile (25.4) of the
   published trip speeds, and close to the published city means (24.4, and 26.8 with no traffic).
3. **OSRM's times are unrealistically short for this city.** Its 53.7 km/h median is above the
   published 99th percentile (45.8 km/h), because it uses generic speeds and no junction waits.
   The 2x time ratio therefore says more about OSRM than about RouteLens.

## 4. What the results do not support

- That any RouteLens ETA is correct for a real trip. No trip was timed.
- That RouteLens's speeds are *calibrated*: the road-class speeds (for example 20 km/h on
  residential roads) and the 30 s wait per junction are still provisional assumptions.
- Anything about rush hour or rain: the snapshot is one quiet Sunday-morning capture, and the
  rush-hour and rain factors are literature or provisional values, not measured here.
- Plausibility is also not evidence of accuracy for Dehradun specifically, since the published
  speeds are for Indian cities in general.

## 5. Suggested wording for the report

> RouteLens's ETA could not be validated against measured trips within the project time. As a
> plausibility check, routes between 30 pairs of major junctions were compared with the OSRM
> routing engine: route lengths agree (median ratio 0.99, r = 0.94). RouteLens's average speed on
> these routes (24.8 km/h, 27.8 km/h free-flow) is consistent with published figures for Indian
> cities (mean 24.4 km/h; Akbar et al., 2018), whereas OSRM's (53.7 km/h) is not. This shows
> plausibility, not accuracy; the speeds per road class and the junction wait remain provisional,
> and a timed-trip study (`docs/ETA_STUDY.md`) is left as future work.

## 6. Still to add: TomTom Routing comparison

TomTom's Routing API returns a travel time with current traffic and one with no traffic, using
local probe data, so it is a stronger reference than OSRM. It needs your API key (read from the
environment only, never printed or saved) and about 30 requests:

```bash
export TOMTOM_API_KEY=...        # type it in the terminal; do not paste it into files or chat
./build/routelens_server &       # in another terminal, or in the background
python3 evaluation/eta_reference.py --refs osrm,tomtom --pairs 30 --out evaluation/results/eta_reference.csv
```

The TomTom times describe the moment the script runs, while the RouteLens snapshot is older, so
state both times in the report. The request format follows TomTom's documentation but has not been
run against the live API from this repository; if it reports an error, send me the message.
