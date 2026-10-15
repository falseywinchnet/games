# The particle engine

Many small things that each go their own way under a few forces: water breaking
from a falling sheet, drops flung off it, glints, spray, mist; later leaves, sparks,
bubbles. Canopy Falls is the first scene on it.

## What it does

- **A pool of particles in arrays** (`System`): position, velocity, age, life, size,
  tail, kind, the emitter that made it, and a seed of its own. A fixed capacity;
  births beyond it are skipped and counted (`skipped()`), never allocated.
- **Emitters as data** (`Emitter`): a box to be born in, a rate, a velocity and its
  spread, ranges for life, size and tail, a kind, a seed, and the forces on what it
  makes. `set_rate` drives surges and lulls from the game.
- **Forces** (`Forces`): gravity, lift, drag toward the air's own motion, a swirling
  air (the curl of a smooth stream function, so it neither bunches nor thins the
  particles), growth of size (mist spreading) and a floor where particles end; their
  landings are reported for splashes.
- **Deterministic.** The n-th particle an emitter makes draws every choice from a hash
  of the emitter's seed and n; time advances in fixed steps, and a remainder is
  carried, so the same seconds give the same particles however they are split into
  calls.
- **Drawing** (`Splatter`): through a pinhole `Lens` into a `Layer` the size of the
  picture.
  - Streaks (water, leaves) cover what is behind them. Each is a head with a trailing
    tail: motion blur plus the water trailing behind it. It is soft at its edges,
    tapers toward the tail, and its opacity gathers at the head. It can be frayed
    into beads along its length.
  - Mist puffs and glints add light, keeping their energy however large they are
    drawn.
  - Coverage is accumulated without regard to order (a coverage-weighted colour and
    the product of what passes), so nothing is sorted.
  - A per-pixel depth hides particles behind nearer surfaces.
  - `compose` lays the layer over a linear-light picture.
- **Light is the game's.** Each particle's colour (`Shade`) comes from the game, which
  alone knows its sun, sky and spray.

## Costs

Simulation is a few dozen floating-point operations per particle per step. Drawing
costs per pixel covered: a streak's box, a glow's disc (capped at 400 pixels across).
Scenes keep counts and sizes to their budget. Canopy Falls' stills use about 25,000
particles.

## Limits

- No collisions except the floor; particles do not see each other.
- Streak colour is uniform along its length; the game varies opacity, not colour.
- Order-independent coverage approximates "over": overlapping streaks of very
  different colours average rather than layer.

## Tests

`tests/particles_tests.cpp` (CTest `particles_engine`) checks:
- births follow the rate and the box;
- the same seconds give the same particles in any split;
- free fall, the floor and its landings;
- a full pool skips births and counts them;
- drawing stays in the picture and respects depth;
- composing is bounded;
- a glow keeps its light.
