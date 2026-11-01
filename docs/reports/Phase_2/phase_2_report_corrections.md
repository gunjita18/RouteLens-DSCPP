# Phase II report: corrections to make

The submitted PDF and `phase_2_report.tex` were **not changed**. This file lists
statements that no longer match the repository and gives replacement text, so
you can decide what to amend (for example in a Phase III report). Commit hashes
below are real and were checked with `git log`.

## Codebase Information

| Report says | Repository shows | Suggested text |
|---|---|---|
| `main` is "protected", merged only via reviewed pull requests | Merges are pull requests, but branch protection cannot be seen from the repo | Drop "protected" unless it is enabled in GitHub settings |
| A `dev` integration branch | No `dev` branch exists | "`main` and `feature/*` branches merged through pull requests" |
| `src/{graph, ds, routing, traffic, api}` | Actual folders are `src/{algorithms, core, data, spatial, traffic, server, app}` | Use the actual names; add `evaluation/` and `docs/` |
| `.env.example` committed, `.env` ignored | No `.env.example`; `.env` and `config.txt` are git-ignored; the server reads `TOMTOM_API_KEY` from the environment | "API keys are read from the `TOMTOM_API_KEY` environment variable; `.env` and `config.txt` are git-ignored and no key is committed" |
| Blank commit hashes | | Table below |

Important commits:

| Hash | Author | What it is |
|---|---|---|
| `26fcad4` | Aayush Rawat | Crow API endpoints, website and request validation |
| `16b5e0b` | Aayush Rawat | GitHub Actions CI and Docker deployment |
| `918aa1f`, `32c4cb5` | debugging-life08 | OSMnx export script and C++ OSM loader (the loader contains the iterative Tarjan SCC cleaning) |
| `513efc3`, `ab7bb98` | Aayush Rawat | KD-tree, then snapping clicks to the nearest road |
| `8cc0b73`, `b4e4a64` | Gunjita Upreti | Binary min-heap; Dijkstra |
| `c26bb62` | Gunjita Upreti | A\*, Yen's algorithm, route ranking with tests |
| `e75d03b` | Gunjita Upreti | Penalty method, scenario mode, real A\* heuristic |
| `6e96288` | Sneha Rawat | TomTom traffic module (client, matching, ETA) |
| `fbfc39f` | Aayush Rawat | LRU cache and rate limiter |
| `421e36d` | Gunjita Upreti | Evaluation harness and results |

The loader and export commits are authored under the `debugging-life08` GitHub
account. Check that this is the account you want credited for that work.

## Tasks Completed / Pending

- **Penalty method** was listed as completed; it was not in the repository until
  `e75d03b`. It is now implemented and tested.
- **Scenario backend** (pending in the report): done in `e75d03b`; the
  scenario user interface remains open (Aayush).
- **Algorithm evaluation** (pending): the algorithm part (1,000 pairs vs
  NetworkX, Dijkstra vs A\*, own heap vs `std::priority_queue`, penalty vs Yen's)
  is done, see `docs/evaluation_phase2.md`. The ETA accuracy study is still open.

## Testing and Validation Status

Verified on 2026-10-25 (Linux/WSL2, `ctest`, 15 of 15 passing):

| Test type | Status | Notes |
|---|---|---|
| Unit tests: data structures and algorithms | Pass | Heap, KD-tree, LRU cache, rate limiter, Dijkstra, A\* (including a randomised equal-cost test against Dijkstra), Yen, penalty, scenario, ranking |
| Route correctness | Pass | 3,000 queries (1,000 pairs x 3 weight profiles) agree with NetworkX for Dijkstra, A\* and a `std::priority_queue` Dijkstra, relative tolerance 1e-9 |
| Route provider and scenario | Pass | Blocked and slowed roads change the routes as expected on a hand-built network; `/api/scenario` also exercised by hand against the Dehradun network |
| Traffic API integration, website manual testing | As reported by their owners | Not re-verified in this check |
| Performance evaluation | Done for the algorithms | `docs/evaluation_phase2.md`; free-flow times only |
| ETA accuracy against TomTom and timed trips | Pending | |

Two statements to reconsider given what the evaluation found: the own heap is
about 3-4% slower than `std::priority_queue`, and A\* helps for distance
weighting but not for travel time, so avoid presenting either as a clear
performance win.
