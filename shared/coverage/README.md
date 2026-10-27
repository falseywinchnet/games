# The coverage engine

A small autonomous brain for machines that have to cover ground: a mower or a vacuum.
It is PlaySuite's path-planning and path-fitting AI. Mowing (`games/mowingman`) drives
its mower with it, in passive mode and when the player lets go of the wheel; any game
with a machine that must sweep a field, find its edges and get itself out of corners can
declare `"engines": ["coverage"]` and link `coverage_core`.

## What it does

It is given nothing but the size of its field. Everything else it learns:

| Stage | What happens |
|---|---|
| Sensing | A fan of rays ahead of the machine, reaching furthest straight ahead like headlights, marches through the true obstacle grid; free and solid cells are written to a belief map. What the scanner cannot see, the bumper teaches. |
| Learning | A support field (distance to anything known solid, and how much structure or doubt is nearby) seeds *germs*: sparse over open lawn, dense where the ground is intricate. Each germ owns the ground geodesically nearest to it, and mown ground keeps its owner. |
| The menu | Laps round the fence and round every bed (contours of the support field), stripes across each owner's ground along its sweep axis, and, when those are done, the tufts left standing. Each entry is a line with grass still under it. |
| Managing | The manager holds one *tape*: the planned way to a line, then the line. It drives that to its end and only then chooses again, looking a couple of jobs ahead. |
| Driving | Tractor steering with a turning circle. Every move between lines is drawn before it is driven, one forward curve if one fits, else a planned turn with one or two changes of gear, and checked against everything known. Driving is nothing but following. |

Unknown space is assumed free and replanned when seen otherwise. Everything the planner
wants is recomputed from (belief, cut, pose), so the machine carries on sensibly from
wherever it is put down, including after the player has driven it somewhere.

## Interface (`src/coverage.hpp`)

| Piece | Role |
|---|---|
| `FieldSetup` | The field in metres and the resolution of the obstacle grid handed in. |
| `Machine` | Clearance, deck width and length, lane pitch, speed, scanner reach and field of view, turning radius, body radius. |
| `Mower(field, machine, obstacles, start)` | The true field as a row-major grid (1 where blocked, no fence needed) and a clear starting pose. |
| `update(seconds)` | Senses, plans and drives for a step (10 to 60 Hz). Returns a `Step`: speed and turning achieved, a bump, a goal reached, cells newly cut. |
| `drive_toward(x, y, seconds)` | Someone has the controls: roll toward a point as tightly as the turning circle allows; collisions are still prevented and coverage recorded. `set_solid` names what stops it even then (a flower bed the player may cross, a wall nobody may). `resume` hands control back. |
| `set_goal(x, y)`, `clear_goal` | Put the deck over this point next (a found object), then carry on. |
| `pose`, `phase`, `finished`, `progress` | Where it is, what it is doing (sweeping, cleaning, goal, manual, finished), and how much of the expected grass is cut. |
| `belief_at`, `cut_at`, `sees`, `path`, `cut` | What the machine knows, for drawing: the belief map, the cut grid (always 0.05 m cells), the scanner's current reach and the tape still to be driven. |
| `gear_changes`, `bumps`, `elections` | Counters for benchmarks. |

The planner works on a 0.1 m grid and records the cut on a 0.05 m grid. Everything is
deterministic: positions are doubles, time is passed in, there is no randomness, so the
same field and the same inputs give the same drive on every platform.

## Cost

A step is a few hundred microseconds on an M-series laptop at 20 Hz; a choice (when a
tape ends) is a few milliseconds, and happens a few times a minute. Mowing runs it on
its simulation tick, never per frame. `tests/coverage_tests.cpp` prints the time each
garden took to mow.

## Tests (`tests/coverage_tests.cpp`, CTest `coverage_engine`)

Thirteen random gardens are mowed completely, safely, deterministically, without
standing about, and with only what the scanner could have seen; someone takes the
controls and hands them back; a goal is driven to. About 20 s optimised. Mowing's own
`mowingman_complete_*` tests (label `design`) mow its whole gardens through the engine.

## Limits

- One machine per field; there is no coordination between several.
- Steering is tractor-like (a turning circle, forward and reverse); a machine that turns
  on the spot would plan simpler moves than it is given.
- The scanner is a forward fan; there is no memory of what a side sensor would see.
