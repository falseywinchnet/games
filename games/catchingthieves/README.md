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

## The gardens

- **The campaign:** 243 gardens, a book in nine sections.
  - First Steps (three hand-made openers), then eight generated sections through the year: Spring Sprouts, Spring Rows, Summer Patch, Summer Orchard, Autumn Field, Autumn Maze, Winter Frost and Winter Night.
  - They run from 2 pumpkins in a small plot to 6 in a big winter maze, sorted by difficulty within each section.
  - The first two sections are open from the start; each later one opens once half of the one before is cleared.
- **The seasons:** each section shows its season.
  - Spring petals, summer butterflies, falling autumn leaves, snow-capped hedges.
  - A snowy night with lanterns and the bear's own pool of light.
  - Each season has its own arrangement of the music.
- **Endless gardens:** any season's difficulty, generated fresh in the background while you play.
- **Scoring:** every garden shows the fewest pushes it can be done in. Match it for a gold pumpkin; get within a fifth of it (at least two pushes' grace) for silver. The garden book records your best for each.

**Every garden is proven solvable.** The generator follows Taylor and Parberry:
- It carves a garden, sits every pumpkin on its burrow, then searches *backwards* by pulling pumpkins. Every position that search reaches can be solved by pushing them back.
- It picks a far, interesting start (deep, with every pumpkin off its burrow, switching between pumpkins often).
- An independent forward solver confirms it from scratch and must find exactly the same number of pushes.
- The solution is stored and replayed through the real rules.

## Play

```sh
games --game catchingthieves
games --game catchingthieves --standalone --dev --script games/catchingthieves/tests/native.script
```

- **Walk:** arrow keys or WASD. Walking into a pumpkin pushes it.
- **Click:** a square to walk there, or a pumpkin beside the bear to push it.
- **Keys:** Z (or Backspace) undo, R start over, H or F1 help, L the garden book, Enter the next garden. Use the capsule for Hint and the hosted audio masters.
- **Hints:** a hint marks the next push of a best solution with marching chevrons.
  - Instant when your garden still lies on the level's recorded best solution.
  - Otherwise the solver thinks it through on a worker thread while the bear strokes his chin.
  - It says plainly when the position can no longer be solved.

Saves go to `~/Library/Application Support/Rainstar/Games/catching_thieves-v1.txt`
(`GAMES_STATE_DIR` overrides the folder). The save holds the garden in play
with its moves, so a half-finished garden resumes exactly; the bests for every
garden; and the endless garden.

With `--dev` (a separate save), `CT_SCRIPT="0.5:lvl40,1.5:sol"` replays inputs:

- `u` `d` `l` `r` take a step
- `z` undo, `rs` start over, `h` hint, `n` the next garden
- `sol` plays the recorded solution
- `stuckme` finds the shortest way to wedge a pumpkin for good
- `lvl<k>` opens garden k, `end<t>` an endless garden of tier t
- `map`, `help` and `close` open and close the panels

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
| The generator (carve, pull backwards, choose, confirm forwards, replay) | `gen.*`, `tools/levelgen.cpp` |
| The level-book file | `levelset.*`, `assets/levels/campaign.txt` |
| The cast: the bear and the raccoons, rigged from primitives, painted faces | `critters.*` |
| The garden scene: lawn, soil, hedges, burrows, pumpkins, seasons, weather, marks, picking | `garden.*` |
| The performance: steps, pushes, raccoon minds, stuck and win reactions, speech | `show.*` |
| Window, input, queue, hints and the endless garden on worker threads, HUD, book, dialogs, autosave | `thieves_view.*`, `save.*` |
| Shared platform (copied from Atom Probe, namespace `ct`) | `platform/` |

All audio is synthesized (`audio_src/`):

- "Pumpkin Patrol", one cheeky theme written in scale degrees, so it plays in G major or sneaks into E minor. It comes in five seasonal arrangements:
  - Spring: marimba and flute over a pizzicato oom-pah.
  - Summer: slide whistle with claps.
  - Autumn: clarinet, accordion and tuba.
  - Winter: music box and sleigh bells.
  - Night: a vibraphone lullaby.
- Stingers for a garden cleared, a pumpkin stuck (the sad trombone) and the whole book.
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
