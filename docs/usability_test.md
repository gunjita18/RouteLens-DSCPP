# Usability test plan (Phase III)

Owner: Aayush. Phase III design, section 8: "Usability test with classmates;
phone and colour-blind checks". This file is the plan and the results sheet.
**Status: not run yet.** Fill in the tables below during the sessions; do not
fill them in from memory or with guesses.

## Automated pre-checks (22 Nov 2026; not a substitute for the user test)

Done by script, without people, so the user test can focus on understanding.

**Narrow screen.** The site was loaded in a 390 x 844 px frame (a common phone
size) in headless Edge; a script searched Clock Tower, then ISBT, and pressed
Find. Result: 3 route cards; no horizontal scrolling and no element past the
right edge; no button or search box under 36 px tall. Not checked: a real
phone, its keyboard, or touch.

**Colour-blind simulation.** The palette was run through the Machado et al.
(2009) colour-vision-deficiency matrices (full severity) and compared with
CIE76 Delta E (above about 20: easy to tell apart; under about 10: hard).

| Vision | Closest pair of route colours (Delta E) | Closed vs slow (Delta E) |
|---|---|---|
| normal | 70 (route 1 vs route 3) | 74 |
| protanopia | 52 (route 2 vs route 3) | 72 |
| deuteranopia | 56 (route 1 vs route 3) | 70 |
| tritanopia | 17 (route 1 vs route 3) | 36 |

Red-green deficiencies (the common ones) keep all colours far apart. Under
tritanopia (rare) routes 1 and 3 are fairly close in colour; they still differ
by line style (solid / dashed / dotted), card border style and number. This is
a simulation, not a check with colour-blind users.

## Set-up

- Start the server in normal mode (real routes), not preview mode.
- 5 classmates who have not seen RouteLens, one at a time, about 10 minutes each.
- Laptop (Edge or Chrome) for 3 people, phone for 2 (the phone opens
  `http://<laptop IP>:18080` on the same Wi-Fi; start the server with
  `ROUTELENS_BIND=0.0.0.0`).
- Say only: "This website compares routes in Dehradun. Please do these tasks
  and think aloud." Do not help unless someone is stuck for 2 minutes; note it.

## Tasks

| # | Task given to the participant | Success means |
|---|---|---|
| T1 | Find routes from Clock Tower to Prince Chowk. | 3 route cards shown |
| T2 | Which route is recommended, and why? | Points at the recommended card and mentions the priorities or the explanation |
| T3 | Make distance matter more than time and find routes again. | Moves the Distance slider and the cards update |
| T4 | Show the routes for rainy weather. | Selects Rain |
| T5 | A road near Prince Chowk is closed. Show the routes that avoid it. | Places a closure and reads the impact box |
| T6 | How old is the traffic data? | Reads the "Traffic data" label |
| T7 | Start again with two new points. | Uses "Reset points" |

## Results (one row per participant and task)

| Participant | Device | Task | Done? (yes / with help / no) | Time (s) | Notes (what confused them) |
|---|---|---|---|---|---|
| P1 | | T1 | | | |

## After the tasks: three questions

1. What was the most confusing part?
2. Was anything hard to read or to tap?
3. Would you use this before a trip? Why or why not?

## Phone check

| Check | Phone model / browser | Result |
|---|---|---|
| Panel and map both usable without zooming the page | | |
| Buttons and search results easy to tap (44 px targets) | | |
| Route cards readable without horizontal scrolling | | |
| Search box opens the keyboard and suggestions are visible above it | | |

## Colour-blind check

The website uses the Okabe-Ito palette, and routes also differ by number, dash
pattern and card border style, so colour should never be the only cue. Check it:

- In Chrome or Edge DevTools: Rendering tab, "Emulate vision deficiencies":
  Protanopia, Deuteranopia, Tritanopia, Achromatopsia.
- For each one: can the three routes, the closures ("Closed" / "Slow") and the
  preview banner still be told apart?

| Simulation | Routes distinguishable? | Closures distinguishable? | Notes |
|---|---|---|---|
| Protanopia | | | |
| Deuteranopia | | | |
| Tritanopia | | | |
| Achromatopsia | | | |

## Fixes made after the test

| Problem seen | Change | Commit |
|---|---|---|
| | | |
