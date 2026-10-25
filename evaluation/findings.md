## Findings

1. **All three searches are correct on this data.** 3,000 queries (1,000 pairs
   x 3 weight profiles) agree with NetworkX for Dijkstra, A\* and the
   `std::priority_queue` version, with no mismatch at a relative tolerance of
   1e-9. Agreement with one reference on random pairs is evidence, not proof:
   it does not cover pairs that are unusually close together, or edge cases
   such as zero-length roads.

2. **A\* helps a lot for distance, hardly at all for travel time.** With the
   distance profile A\* expands about 3.5x fewer vertices and is about 2.4x
   faster (median per pair). With the time profile it expands only 1.35x fewer
   vertices and the median run time is not better than Dijkstra's (0.96x),
   because the heuristic must assume the fastest edge speed in the whole graph
   (120 km/h here) and a city's real speeds are far lower, so the bound is loose.
   The balanced profile sits between the two (1.76x fewer vertices, 1.26x
   faster). A tighter speed bound (for example per road class or landmark
   based) would be the next thing to try; it has not been tested.

3. **The hand-written heap is not faster than `std::priority_queue`.** On the
   same Dijkstra, same costs and the same path bookkeeping, the own heap is
   about 3-4% slower (median per-pair ratio 0.96-0.97). It is a correct,
   competitive structure, but this run gives no performance argument for
   writing it by hand. The cause was not investigated, and timings came from a
   single run on one machine under WSL2, so a difference this small should not
   be over-interpreted.

4. **The penalty method gives genuinely different routes; Yen's mostly does
   not.** Asked for 5 routes, Yen's returns near-duplicates (mean overlap 0.94,
   only 1.18 routes survive the RouteLens filter, and only 17% of pairs have 2
   or more usable routes). The penalty method averages 0.20 overlap, 4.38
   usable routes, and 100% of pairs have at least 2. It also runs about 45x
   faster on average (16 ms against 758 ms). The price is stretch: penalty
   routes cost 1.22x the cheapest on average and up to about 1.5x at worst,
   whereas Yen's stay within 1.01x because they are barely different roads.
   This supports using the penalty method as the primary generator and Yen's
   only as a top-up, as `OsmRouteProvider` now does.

## Limitations

- Free-flow travel times only: no live traffic, so nothing here says anything
  about ETA accuracy (a separate study, still pending).
- Origin-destination pairs are uniformly random vertices. Real commutes are
  shorter and concentrated, so absolute times and the amount of overlap may
  differ on realistic trips.
- "Usable" depends on the RouteLens thresholds (stretch <= 1.5, overlap < 0.85)
  and the penalty factor (1.5) was not tuned; no sensitivity analysis was run.
- One run per configuration, no confidence intervals, one machine.
