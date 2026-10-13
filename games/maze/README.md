# Maze 95

Portable PlaySuite module, permanent entry ID 19. Its factory, cover, help,
build rules and assets live in this folder. See [HANDOFF.md](HANDOFF.md) for current
integration evidence and limitations.

The 3D maze from 1995, as a game, by way of a dead shopping mall's vaporwave
and Encarta Mind Maze's castle full of chance meetings.

- **The maze:** walk it in first person, a square at a time, turning in quarter turns. Big red brick walls, hotel and arcade carpets, acoustic ceiling tiles or a starry night overhead, and pictures on the walls.
- **The pictures:** each maze hangs one or two posters and three or four framed pieces at random.
  - The 27 posters are made-up ads in 90s and 2000s box-art style (airbrushed, Bryce-rendered, Windows 95 to ME), plus WANTED posters for Wojak and Chudjak.
  - The 51 framed pieces cover the same eras.
  - All are painted offline in `authoring/maze_posters/` and loaded from `assets/posters/`.
- **The goal:** every maze opens with a briefing naming what you seek. The first is the cheese; after that it's something else from 47 iconic odds and ends:
  - a striped bucket of fried chicken, a floppy disk, a lava lamp, a boombox, a VHS tape
  - a rotary phone, a goldfish in a bowl, a disco ball, a puzzle cube, a pet rock
  - a pocket game machine, a pot of honey, a tin of spinach, a dancing hamster, and the rest.
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
- **The paint snail:** a 3D snail that crawls the floor, walls and ceiling, never in a straight line. It leaves a painted path wider than itself, with a slime trail.
  - There are 18 animated paints, among them a blue-screen debug-log rain, polka-dot spam, houndstooth patterns, and eyes that blink and look around.
  - It ignores you until you come close, then hides in its shell and peeks out once you back off. It squelches, hides and peeks with sounds of its own.
- **The elevator tile:** the floor lifts you up through the ceiling into another maze that was running directly above all along, and the camera rides up with it.
- **The flip stone:** the original screensaver's spinning grey polyhedron. Touch it and the whole maze rolls over: you walk on the ceiling and left and right swap. Floor pads, portals and elevators go out of reach, and ceiling pads go live. The music plays backwards.
- **Encounters:** the Shiba, Apu or the Doomer steps into the corridor ahead, says something, and vanishes with a poof. Now and then they also do you a good turn:
  - pointing the way to the reward, as the crow flies
  - producing a key for the next locked door
  - distracting the marble

Every maze is proven solvable by a search over the whole state (floor, square, flipped or not, pads pressed). That search also gives the shortest route, which the win dialog compares with yours.

## Who you meet

The maze's residents are memes, drawn for PlaySuite (the Shiba, Apu and Monke after the owner's own crayon drawings).

- **Visitors:**
  - **The Shiba:** such maze, a dozen lines.
  - **Apu:** a small one in a blue shirt, holding a spatula and clutching a teddy, asking if you've seen his mommy.
  - **The Doomer:** asks if you've seen the exit.
- **On the ceiling:** Ceiling Cat, who only meows, and the Goose, who honks.
- **Keyboard Cat:** waits just beyond a portal and taunts you to come closer.
- **Philosoraptor:** stands in the reward room staring at the trophy and wonders aloud.
- **Monke:** sometimes rides the elevator with you.
- **The giant ball:** it may be the stone marble, or one of these:
  - a monocled frog on a penny-farthing, who rings his bell and is irritated if you block him
  - the Thinking Face, glowing, with a glam-pop tune you only hear up close
  - Mazeball, in the maze's own brick, with googly eyes, who burps
- **A reward:** one of them is a dancing hamster.

The in-game "Who's who" lists them. No trademarks are used: the rewards carry no brand marks, and the only OS logo on the walls is the made-up Rainstar 95. The old public-domain cast (`cast.*`) remains only as test fixtures.

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
| The renderer: perspective-correct textures, near-plane clipping, z-buffer, roll, fog, the blackout lamp, glitch, sprites. Triangles are scanned by the shared render engine's camera-space scan (`shared/render`, `r3d_scan.hpp`); the texel, light and blend are this game's pixel policy | `soft3d.*` |
| Rules, generation and the solver (state search over floor, square, flip and pads), the route for the dev walker | `maze.*` |
| Drawing the maze and what lives in it | `world.*` |
| A level in motion: steps, turns, doors, elevator ride, flip roll, portal blink, the ball, the snail, visitors and residents | `session.*` |
| The snail: crawling every surface, its painted path and slime, hiding | `snail.*` |
| The snail's animated paints | `paints.*` |
| The residents: their pictures, lines and credits | `residents.*` |
| Hanging the pictures; PNG decoding for the painted assets | `art.*`, `png.*` |
| Painted surfaces (brick, carpets, ceilings, doors, pads, posters, the snail's paints) | `textures.*` |
| The rewards | `rewards.*` |
| The band: its instruments, the three styles' arrangements and themes, the tunes it makes up, the wall and playing backwards | `band.*` |
| Window, chrome, briefing, speech, win, trophy shelf, help, credits, autosave; the frame enlarged into the window's buffers by the render engine's `PixelSurface`, repairing only what changed | `maze_view.*`, `save.*` |
| Portable raster, GUI.Forms text masks and scene audio, namespace `mz` | `platform/` |

All audio is synthesized (`audio_src/`):

- The music is played live by a band that makes up its tunes as it plays (`band.*`), in one of three styles chosen for each level:
  - "Maze Run": synth rock in A major at 98 BPM
  - "Bonus Round": a bouncy romp in C major at 124 BPM
  - "Turbo": a funky groove in B-flat at 112 BPM
- A level's first tune plays the style's own themes; later ones make new melodies over the same chords, in the themes' rhythms, and change key.
- In a blackout the band is heard through a wall; on the ceiling it plays backwards, a bar at a time.
- The styles were chosen from recordings that `score.py` still renders, and the band is matched to them; `maze_preview band STYLE SECONDS out.wav` renders the band itself, and `maze_preview band-levels` re-measures its mix.
- A start-up chime and a win jingle (`mall_music.py`).
- The residents' sounds: the meow, the honk, the bell, the burp, Keyboard Cat's keys, Monke's ooh, the Thinking Face's tune, and the snail's squelch, hide and peek.
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

## Current validation boundaries

The portable module, shared capsule/help, master switches and reduced motion are
implemented. Native automation and inspected captures are documented in
`HANDOFF.md`. Human listening and subjective long-session play remain unverified;
cross-platform release validation is performed by the suite's publication workflow.
