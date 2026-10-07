# Maze 95

Portable PlaySuite module, permanent entry ID 19. Its factory, cover, help,
build rules and assets live in this folder. See [HANDOFF.md](HANDOFF.md) for current
integration evidence and limitations.

The 3D maze from 1995, as a game, by way of a dead shopping mall's vaporwave
and Encarta Mind Maze's castle full of chance meetings.

- **The maze:** walk it in first person, a square at a time, turning in quarter turns. Big red brick walls, hotel and arcade carpets, acoustic ceiling tiles or a starry night overhead, and vaporwave posters on the walls.
- **The posters:** sunset grids, a marble bust, palms and a dolphin, AESTHETIC, a floppy disk, "please wait", the Rainstar 95 logo, Tux, a gnu, a smiley, a contented blue screen, the lady with the smile, KILROY WAS HERE.
- **The goal:** every maze opens with a briefing naming what you seek. The first is the cheese; after that it's something else from 46 iconic odds and ends:
  - a striped bucket of fried chicken, a floppy disk, a lava lamp, a boombox, a VHS tape
  - a rotary phone, a goldfish in a bowl, a disco ball, a puzzle cube, a pet rock
  - a pocket game machine, a pot of honey, a tin of spinach, and the rest.
- **The reward room:** it waits at the far end, and it is full of the thing.
- **The trophy shelf:** fills up as you go.

## What's in the mazes

They are dynamically generated: new tricks arrive one at a time, then mix.

- **Doors and pads:** a locked door opens when you step on the pad of its colour. Every door is necessary on its own: even with all the others open it still bars the way.
- **Portals:** the only clue is the carpet, which is ever so slightly off. Some levels are sealed, with a portal as the only way across.
- **The giant marble:** a stone ball rolls the corridors on its own, bouncing off walls.
  - Sometimes you meet it coming straight at you. It stops, waits, and rolls away. It never runs you over and never pens you in.
  - It keeps to the maze's loops, never its dead ends, so it only ever turns back when it meets you.
  - If a portal or the elevator drops you right on it, it rolls aside.
- **Light bulbs:** touching one kills the lights except a little lamp on your head. Find another bulb, or finish, and they snap back on. The music sounds as if it's coming through a wall meanwhile.
- **The paint snail:** it wanders the maze with a hand on the right-hand wall, repainting the brick behind it. The repaints include zebra, plasma, eyes, polka dots, television static, tie-dye and ERROR, some of them glitching. A long level ends as a patchwork.
- **The elevator tile:** the floor lifts you up through the ceiling into another maze that was running directly above all along, and the camera rides up with it.
- **The flip stone:** the original screensaver's spinning grey polyhedron. Touch it and the whole maze rolls over: you walk on the ceiling and left and right swap. Floor pads, portals and elevators go out of reach, and ceiling pads go live. The music plays backwards.
- **Encounters:** someone steps into the corridor ahead, says something, and vanishes with a poof. About thirty characters, around six lines each, plus the occasional good turn:
  - pointing the way to the reward, as the crow flies
  - producing a key for the next locked door
  - distracting the marble

Every maze is proven solvable by a search over the whole state (floor, square, flipped or not, pads pressed). That search also gives the shortest route, which the win dialog compares with yours.

## Who you meet

Everyone is in the public domain in the United States, drawn afresh after their original illustrators. They have lines of their own, and here and there their own words from the books.

- **Alice:** Alice, the Cheshire Cat, the Hatter, the White Rabbit, the Queen of Hearts, Humpty Dumpty.
- **Oz:** Dorothy (silver shoes, as in the book), the Scarecrow, the Tin Woodman, the Cowardly Lion.
- **Pooh:** Winnie-the-Pooh, Piglet, Eeyore, Tigger.
- **The funnies:** Popeye and Olive Oyl (Thimble Theatre), Krazy Kat and Ignatz, Gertie the Dinosaur.
- **Everyone else:** Count Orlok, Sherlock Holmes, Pinocchio, Peter Rabbit, Mr. Toad, the Gingerbread Man, Sun Wukong, Robin Hood, the Frog Prince, and a knight errant.
- **Tux:** Larry Ewing's, drawn with credit.

The in-game "Who's who" lists every source. No trademarks are used: the rewards carry no brand marks, and the only OS logo on the walls is the made-up Rainstar 95.

## Play

```sh
games --game maze
games --game maze --standalone --dev --script games/maze/tests/native.script
```

- **Keys:** Up / W walks forward, Down / S steps back, Left and Right turn.
- **Mouse:** the left and right thirds of the view turn, the middle walks, and the bottom middle steps back.
- **Panels:** T trophy shelf, H or F1 help. The capsule provides Restart and Who's who. Hosted audio uses the suite masters; standalone help retains its audio switches.

Saves go to `~/Library/Application Support/Rainstar/Games/maze-v1.txt`
(`GAMES_STATE_DIR` overrides the folder). The save holds the maze you're on (each is rebuilt from its seed), the shelf, and tallies.

With `--dev` (a separate save), `MZ_SCRIPT="0.3:lvl12,0.8:ok,1:auto"` replays inputs:

- `f` `b` `l` `r` walk and turn
- `ok` dismisses a dialog
- `lvl<n>` starts maze n
- `auto` walks the best route, re-planned each step around the marble and visitors; `auto<k>` walks only k steps of it
- `shelf`, `help` and `credits` open those panels

The shared native runner also supports keyboard, pointer, resize and full-window
frame capture. Its finite scripts are described in `new-games/AGENTS.md`.

## Build

From the repository root, test the core independently:

```sh
cmake -S games/maze -B .build/new-games/maze -DCMAKE_BUILD_TYPE=Release
cmake --build .build/new-games/maze --parallel 2
ctest --test-dir .build/new-games/maze --output-on-failure
```

Build the shared native host using the repository's `docs/BUILDING.md` and root
`AGENTS.md`; it supplies both standalone and hosted play. The headless preview
executable is `maze_preview`. Authoring audio sources stay in `audio_src/` and
render into this module's `assets/audio/`. Adding the complete folder to `games/`
is its registration; no shell or release-version edits are required.

## How it works

| Part | Files |
|---|---|
| The renderer: perspective-correct textures, near-plane clipping, z-buffer, roll, fog, the blackout lamp, glitch, sprites | `soft3d.*` |
| Rules, generation and the solver (state search over floor, square, flip and pads), the route for the dev walker | `maze.*` |
| Drawing the maze and what lives in it | `world.*` |
| A level in motion: steps, turns, doors, elevator ride, flip roll, portal blink, the marble, the snail, visitors | `session.*` |
| Painted surfaces (brick, carpets, ceilings, doors, pads, posters, the snail's paints) | `textures.*` |
| The 46 rewards, and the cast with their lines and sources | `rewards.*`, `cast.*` |
| Window, chrome, briefing, speech, win, trophy shelf, help, credits, autosave | `maze_view.*`, `save.*` |
| Portable raster, GUI.Forms text masks and scene audio, namespace `mz` | `platform/` |

All audio is synthesized (`audio_src/`):

- "Atrium", a mallsoft loop at 78 BPM in D-flat: electric piano ninths, a vibraphone tune, synth bass, a soft swung drum machine, a big wet hall and tape wobble. It comes in three versions:
  - as it is
  - through a wall, for the blackout
  - backwards, for the ceiling
- A start-up chime and a win jingle.
- Effects: steps on carpet, turns, bumps, locks, pads, grinding doors, the flip, bulbs, the elevator and its ding, portals, the marble's rumble, knock and thud, appearances and poofs.

## Original delivery measurements

These measurements describe the original supplied package. Current port checks
and inspected native captures are recorded in `HANDOFF.md`.

- Tests:
  - 120 mazes over three runs are generated and proven, each one's best route replays to the reward, and the same seed builds the same maze.
  - Every door is necessary on its own, ceiling pads need the flip, and sealed portals are the only way across.
  - Two-floor mazes have exactly one elevator joining them.
  - 36 sessions walked to the reward with the marble rolling and the snail painting; the marble never shared a square with the player.
- A fuzz run generated all 3,000 mazes from levels 1–60 across 50 runs; the slowest took 50 ms.
- Rendering takes about 1–3 ms a frame at the game's 366×253.
- The music's AAC decodes to exactly its loop length.

## Current validation boundaries

The portable module, shared capsule/help, master switches and reduced motion are
implemented. Native automation and inspected captures are documented in
`HANDOFF.md`. Human listening and subjective long-session play remain unverified;
cross-platform release validation is performed by the suite's publication workflow.
