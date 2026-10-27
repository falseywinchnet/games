# Eggy and the Very, Very Tall Mountain

A tiny duckling in an acorn-cap helmet climbs a mountain so tall that a whole
climb takes years. You can help. When you stop helping, he keeps climbing by
himself, a little slower. He never stops — not even while the game is closed.

Native C++20 on GUI.Forms (the framework used by the Games cabinet), with a
1990s-style software 3D renderer, procedural world, and an original synthesized
soundtrack.

## Play (dogfood copy)

```sh
open /Users/ultimussecundai/solitaire_sounds/eggy/Eggy.app
```

- **Help Eggy:** arrow keys or W A S D (Up is uphill), Space hops, or hold the
  mouse on the ground where you want him to go. Eggy still finds his own way
  around obstacles.
- **Autopilot:** after 7 seconds without input he climbs on his own. Stars are
  only caught by a helping hand.
- **Keys:** F1 help · T top scores · M music · N sound · +/- zoom · Esc close.
- **Saves:** `~/Library/Application Support/Rainstar/Games/eggy-v1.txt`. He
  autosaves; time away is credited at the autopilot's pace.

Dev flags (use a separate dev save, never your real climb):
`--summit` (warp near the summit ceremony), `--storm` (a storm in a few seconds,
once past base camp), `--warp=<row>`, `--dev`.

A fresh climb starts at a level base camp: Eggy explains why he climbs, salutes,
and sets off (any key skips). Rare sights along the way: a resting deer, a
preening cat, waterfalls down the slope, fish leaping in brooks, a tree that
topples in the woods, frogs on logs. Storms roll in every half hour to hour and
a half: dark clouds, driving rain or blizzard, gusts, lightning and thunder.

## Build

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=$PWD/sdk && cmake --build build -j 8
ctest --test-dir build        # world determinism + autopilot never gets stuck
./build/preview out.ppm 620 .4   # headless frame render (row, time of day)
```

`sdk/` is a copy of the GUI.Forms paint-port SDK from the M4; `ref/` is a
read-only copy of the cabinet for reference.

## How it works

| Part | Files |
|---|---|
| Endless seeded mountain (biomes, slope, brooks, ponds, ice bands with ledge chains, a guaranteed climbable lane, 200-420 stars, 190-260 million rows) | `world.*` |
| Duck physics, hops, ice, wind, leaves, breath, drinking, idle life, A* autopilot, summit ceremony | `sim.*` |
| Software 3D: z-buffer, affine texture mapping, mipmaps, splat blending, Gouraud light, fog, 15-bit ordered dither | `r3d.*` |
| 40 ground textures (128 px, baked noise-bump, mip chains) | `ground.*` |
| 100 flowers, 50 trees, 30 logs, 20 bushes, 40 mushrooms, 40 ferns, 200 rocks, 12 grasses | `flora.*` |
| Eggy and the officer as low-poly textured 3D models | `duck3d.cpp`, `pose.hpp` |
| Scene: sky, terrain, props, weather, particles, camera | `scene.*` |
| Game view: HUD, speech, dialogs, saving, music | `eggy_view.*` |
| Native fast presentation (Core Animation layer, GPU nearest-neighbour scaling) | `present.*` |

**CPU:** the game renders a ~390×260 frame and hands it to a Core Animation
layer. Text is drawn as small cached layers. It paces itself: 20 fps while
you help, 12 fps while you watch, 5 fps in the background, and nothing while
the window is hidden. GUI.Forms' own `LiveSurface` path is the fallback; on
macOS it re-rasterizes the full window on the CPU every frame, so it is much
costlier.

## Lines

17,888 lines in 53 categories (`assets/lines/`). Sibling models (Claude Sonnet
subagents) wrote the drafts, sorted by situation and nearby object.
`tools/build_lines.py` filters them: no other creatures or people, no
franchise names, kid-safe, ASCII, at most 70 characters, deduplicated.

## Music and sound

All audio is synthesized by `audio_src/` using the shared engine in
`../work/src`. No samples or quotations.

| Track | Region | Flavour |
|---|---|---|
| Little Boots March | meadow and title | cute march |
| Mossy Mystery | forest | mystery and fantasy |
| Brook Hoedown | ponds | mountain man |
| Fistful of Pebbles | ravine | spaghetti western |
| Dwarven Halls | highlands | dwarven halls |
| Snow Bell Monastery | snow | Tibetan monastery |
| Summit, With Medal | summit | — |

There are also 30 effects and the summit fanfare, plus seamless wind, brook
and forest beds. Regenerate with `python3 eggy_music.py && python3 eggy_sfx.py`.

## Not yet verified

- **Listening:** no one has listened to the music or effects yet. Only
  loudness and peaks were checked numerically.
- **Long-term behaviour:** autopilot progress is tested over sampled stretches
  of each biome, not over a real multi-year climb. Average rates are about
  0.9 rows/s on autopilot and about 1.6 rows/s when helping, so a summit
  takes about 4.5–6 years of helping.
- **Cabinet integration:** not yet done. `EggyView` is a self-contained
  `gf::Control`, and the cabinet's `Collection` could host it as another tab.
  That is Astra's tree, so it is untouched here.
