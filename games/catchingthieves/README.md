# Catching Thieves

Portable PlaySuite module, permanent entry ID 18. Its factory, cover, help,
build rules and assets live in this folder. See [HANDOFF.md](HANDOFF.md) for current
integration evidence and limitations.

A gang of raccoon bandits is raiding the bear's vegetable garden from their
burrows. They pop up to taunt him: blowing raspberries, juggling stolen
carrots, waving them about and crunching them, dancing, pointing and laughing.
They duck the moment he comes near. The only way to stop them is to roll a
pumpkin onto every burrow. A trapped raccoon mumbles underneath ("Mmmph!",
"Who turned off the sun?"), shoves the pumpkin about, and waves a paw out from
under the rim. Push a pumpkin back off and its raccoon bursts out cheering.

The bear walks one square at a time and pushes a pumpkin ahead of him when
there's room; he can never pull. That makes it a Sokoban, after the
Uriminzokkiri Flash game 도적잡기 ("Catching Thieves"), but with an original cast
and no caricatures. If a pumpkin gets wedged for good, every free raccoon
comes up laughing ("Now you've done it!"), the bear facepalms, a sad trombone
plays and a red mark goes on the pumpkin. Undo is always there. When the last
burrow is covered, the bear jumps for joy with his hat in the air, the
pumpkins hop, and the raccoons wave white flags from underneath ("We give up!").

## The voices

The raccoons are cheeky, theatrical garden bandits who love vegetables and mean
no harm; the bear is gentle and polite. Nobody makes fun of the player. About 500
lines (`src/lines.cpp`) are drawn from shuffled bags, so none repeats until every
line of its kind has been said, and each follows what just happened: a greeting
for a new garden, often about its season ("Brr! Got any soup?"); taunts while
popping up ("We're very polite thieves."); mumbles from under a pumpkin ("It's
very orange in here."); a cheer when freed; nerves when a pumpkin rolls up next
door ("Good pumpkin. Stay.") or only one raccoon is left ("Guys? Guys?!"); lines
for undo, starting over, hints and standing still. Wedging pumpkins escalates
within a garden: the first time they laugh ("Now you've done it!"), the second
they cheer ("Encore!"), and from the third they turn kind and point at Undo
("Psst. Z takes it back."), without the laughing sound. Reactions to things done
over and over (bumping hedges, starting over, hints) are said now and then, not
every time. A simulated 18-minute session says about 110 lines with none repeated.

## The gardens

There is no fixed sequence. **Difficulty** on the shell's Settings screen chooses
Tutorial, Easy, Medium or Hard (`settings()` in the view) and **New garden** deals a
garden at it. A garden in play is never interrupted: a new
difficulty replaces an untouched garden at once, otherwise it waits for the next one.

- **Tutorial:** eleven short, hand-made lessons, each showing one mechanism, dealt at
  random (every lesson once before any comes round again). Pushing onto a burrow;
  walking round to push another way; covering every burrow; no pulling; a pumpkin
  against a bare hedge is stuck for good; sliding along a hedge to a burrow on it;
  filling the far burrow first; two pumpkins in a row won't budge; corners; undo; hint.
  The book's three old openers are among them.
- **Easy, Medium and Hard:** generated gardens, defined by what the solver measures
  along each garden's best solution, not by size alone (`src/tiers.cpp`). The
  difficulty score is `3·log2(1 + solver states) + pushes + 1.5·pumpkin switches +
  40·share of offered pushes that wedge a pumpkin`. Each tier is a band of that score
  with floors, and the bands do not overlap:

  | Tier | Pumpkins | Pushes at least | Solver states at least | Score | Fresh gardens grow in |
  |---|---|---|---|---|---|
  | Easy | 2–3 | 6 | – | below 65 | 5×5 to 6×6 plots |
  | Medium | 2–5 | 14 | 50 | 65 to 100 | 7×6 to 8×7, more hedges |
  | Hard | 3–6 | 26 | 500 | 100 and up | 8×8 to 9×9, most hedges |

  Medians of the verified tables (72 Easy, 78 Medium, 90 Hard), every one higher at
  each step, checked by `catchingthieves_rules`:

  | Tier | Pumpkins | Soil | Pushes | Moves | Switches | Solver states | Trap pushes on the way | Score |
  |---|---|---|---|---|---|---|---|---|
  | Easy | 2 | 26 | 12 | 38 | 5 | 79 | 13 | 46 |
  | Medium | 3 | 38 | 25 | 87 | 9 | 1,654 | 32 | 80 |
  | Hard | 4 | 50 | 37 | 151 | 16 | 29,304 | 71 | 115 |

  The share of offered pushes that wedge a pumpkin stays near a quarter at every tier;
  what grows is how many such traps lie along the way.
- **Dealing never waits.** While a garden is played, the next one for the chosen
  difficulty grows on a worker thread with the usual cancellation. New garden takes
  it if it is ready, and otherwise deals at once from the verified tables
  (`assets/levels/gardens.txt`), preferring gardens not yet dealt. Measured on the M4
  Mini, single-threaded, 40 gardens a tier: Easy median 0 ms (worst 4 ms), Medium 8 ms
  (worst 42 ms), Hard 555 ms (90th percentile 2.1 s, worst 6.5 s). A deal from the
  tables takes about 1 ms in the native view.
- **The seasons:** each garden's season is chosen at random, never the same twice
  running, so a session passes through the whole year and all five arrangements of
  the music; the season says nothing about difficulty, which the card names. Lessons
  are always in spring, the plainest and brightest scene.
  - Spring petals, summer butterflies, falling autumn leaves, snow-capped hedges.
  - A snowy night with lanterns and the bear's own pool of light.
- **Scoring:** every garden shows the fewest pushes it can be done in. Match it for a
  gold pumpkin; get within a fifth of it (at least two pushes' grace) for silver. The
  card counts the gardens cleared at each difficulty.

**Every garden is proven solvable.** The generator follows Taylor and Parberry:
- It carves a garden, sits every pumpkin on its burrow, then searches *backwards* by pulling pumpkins. Every position that search reaches can be solved by pushing them back.
- It picks a far, interesting start (deep, with every pumpkin off its burrow, switching between pumpkins often).
- An independent forward solver confirms it from scratch and must find exactly the same number of pushes.
- The solution is stored and replayed through the real rules; the tests replay every table garden and freshly grown ones.

## Play

```sh
games --game catchingthieves
games --game catchingthieves --standalone --dev --script games/catchingthieves/tests/native.script
```

- **Walk:** arrow keys or WASD. Walking into a pumpkin pushes it.
- **Click:** a square to walk there, or a pumpkin beside the bear to push it.
- **Keys:** Z (or Backspace) undo, R start over, H or F1 help, Enter the next garden. Use the capsule for Hint and Settings for the difficulty.
- **Hints:** a hint marks the next push of a best solution with marching chevrons.
  - Instant when your garden still lies on the level's recorded best solution.
  - Otherwise the solver thinks it through on a worker thread while the bear strokes his chin.
  - It says plainly when the position can no longer be solved.

Saves go to `~/Library/Application Support/Rainstar/Games/catching_thieves-v1.txt`
(`GAMES_STATE_DIR` overrides the folder). The save holds the garden in play
with its moves, so a half-finished garden resumes exactly; the difficulty;
the gardens cleared and perfect at each difficulty; the bests for every table
garden; and which table gardens have been dealt.

The file name and format are permanent. A save from the old sequential book
loads as before and is upgraded once (`migrate` in `src/save.*`): the garden in
play resumes move for move (the tables keep the book's 243 gardens under their
old indices); New garden carries on at that garden's tier; every best stays
recorded and counts as a garden cleared (and perfect, if it matched par) at its
garden's tier; gardens cleared in the old endless mode count at Easy (spring),
Medium (summer, autumn) or Hard (winter, night); each book garden keeps its
season.

With `--dev` (a separate save), `CT_SCRIPT="0.5:lvl40,1.5:sol"` replays inputs:

- `u` `d` `l` `r` take a step
- `z` undo, `rs` start over, `h` hint, `n` the next garden
- `sol` plays the recorded solution
- `stuckme` finds the shortest way to wedge a pumpkin for good
- `lvl<id>` opens table garden id, `tier<d>` chooses difficulty d (0 Tutorial to 3 Hard) and deals, `fresh<d>` grows one there and then
- `menu`, `help` and `close` open and close the panels

The shared native runner also supports keyboard, pointer, resize and full-window
frame capture. Its finite scripts are described in `new-games/AGENTS.md`.

## Build

From the repository root, test the core independently:

```sh
cmake -S games/catchingthieves -B .build/new-games/catchingthieves -DCMAKE_BUILD_TYPE=Release
cmake --build .build/new-games/catchingthieves --parallel 2
ctest --test-dir .build/new-games/catchingthieves --output-on-failure
```

Build the shared native host using the repository's `docs/BUILDING.md` and root
`AGENTS.md`; it supplies both standalone and hosted play. The headless preview
executable is `ct_preview`. Authoring audio sources stay in `audio_src/` and
render into this module's `assets/audio/`. Adding the complete folder to `games/`
is its registration; no shell or release-version edits are required.

## How it works

| Part | Files |
|---|---|
| Rules: the board, moves, pushes, undo, dead squares, stuck detection, the XSB/LURD formats | `level.*` |
| The push-optimal solver (breadth-first over pushes, the bear reduced to his walkable region) | `solver.*` |
| The generator (carve, pull backwards, choose, confirm forwards, replay) and the difficulty metrics | `gen.*`, `tools/levelgen.cpp` |
| The difficulties: tier rules, classification, growing a fresh garden for a tier | `tiers.*` |
| The garden tables: lessons and verified gardens with permanent ids | `levelset.*`, `assets/levels/gardens.txt` |
| What everyone says: 500 lines in shuffled bags | `lines.*` |
| The cast: the bear and the raccoons, rigged from primitives, painted faces | `critters.*` |
| The garden scene: lawn, soil, hedges, burrows, pumpkins, seasons, weather, marks, picking | `garden.*` |
| The performance: steps, pushes, raccoon minds, stuck and win reactions, when to speak | `show.*` |
| Window, input, queue, hints and the next garden on worker threads, dealing, HUD, dialogs, autosave and migration | `thieves_view.*`, `save.*` |
| Shared platform (copied from Atom Probe, namespace `ct`) | `platform/` |

All audio is synthesized (`audio_src/`):

- "Pumpkin Patrol", one cheeky theme written in scale degrees, so it plays in G major or sneaks into E minor. It comes in five seasonal arrangements:
  - Spring: marimba and flute over a pizzicato oom-pah.
  - Summer: slide whistle with claps.
  - Autumn: clarinet, accordion and tuba.
  - Winter: music box and sleigh bells.
  - Night: a vibraphone lullaby.
- Stingers for a garden cleared and a pumpkin stuck (the sad trombone).
- Effects: steps, pushes with a little "hup", the trap thunk, pops and ducks, muffled grumbling, a raspberry, a carrot crunch, the raccoons' laugh, and more.

## Original delivery measurements

These measurements describe the original supplied package. Current port checks
and inspected native captures are recorded in `HANDOFF.md`.

- Tests: 228 stuck positions met in random play, every one confirmed unsolvable by the solver.
  - All 243 campaign solutions replay to a win in exactly their par.
  - 147 pars were re-solved from scratch and confirmed optimal.
- Rendering: about 5 to 10 ms a frame for the largest garden (Release, arm64).
- Solving the hardest night gardens from scratch takes up to about 4.4 s; hints along the recorded solution are instant.
- Music: each loop's AAC decodes to exactly its manifest length; levels are even and nothing clips.

## Current validation boundaries

The portable module, shared capsule/help, master switches and reduced motion are
implemented. Native automation and inspected captures are documented in
`HANDOFF.md`. Human listening and subjective long-session play remain unverified;
cross-platform release validation is performed by the suite's publication workflow.
