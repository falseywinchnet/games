# Canopy Falls handoff

A living waterfall on the ambient engine's `SceneView`. The look, the sound and the music were chosen by the owner over many rounds of candidates. Those decisions are recorded in README.md and in the commit history of the `falls` branch.

## How it is made

- **Still renderer:** `src/still.cpp`. It ray-traces the scene: the sky (`sky.cpp`), the cliff and the single curtain, the jungle, the banks and plants, the pool, the walkway and the near leaves, through a general-perspective lens (bend .55, 100° vertical). It is the reference for the candidate stills (`tools/candidates.cpp`).
- **Live scene:** `LiveFalls` in the same file.
  - **Baking:** everything that does not move is baked once per picture size, in 16-row bands over the cores, with edges smoothed adaptively (four more rays where a pixel differs from its neighbours). For each pixel the falling sheet covers, the bake keeps where it lies in the water's own frame, and what lies behind and in front of it.
  - **Each frame:**
    - the sheet, from a loop of its noise made once (8 s of the water's time, 384 rows by about 760 columns);
    - the sun's glitter on the pool's ripples;
    - the particles (`shared/particles`): clumps, broad veils, drops, glints, mist;
    - one tone-curve table.
- **View:** `src/falls_view.cpp`.
  - The scene raster is one pixel per two points at Balanced, one per point at Fine, one per three at Light, capped near 680,000 pixels.
  - Baking runs on its own threads (`std::async`, as Mowing does) while the last picture stays up.
  - Particle density is 0.4, 0.7 or 1.0 by detail.
- **Sound:** `src/falls_voice.cpp`, a live voice under Sound.
- **Music:** the suite's `heavenly_harp` style (`src/music/styles`), under Music.

## Verified

On the M4 Mac mini, 2026-10-15, full application build (LLVM 22.1.8, toolkit 6fd0071):
- **Tests:** every target built. `ctest -LE design` passes, 49 of 49, including:
  - `canopyfalls`: the same picture whatever order the bands are baked in; blue sky above and warm wood below; the falls move; particles stay inside their pool; the forest's sound is finite, inside full scale and repeatable;
  - `canopyfalls_view_contract`: hosted frames, help, resizes, nothing while hidden;
  - `particles_engine`, `music` (the new style among them) and `game_catalog`.
- **Native window, standalone, by script:**
  - the backdrop shows until the first bake, then the scene;
  - the picture stays up while a resize to 760 × 520 bakes;
  - the help card opens over the moving falls; the script quits cleanly;
  - captures are in `screens/`.
- **Cost:**
  - Headless, 550 × 380 at density 0.7: baking 18 s on one thread (about 2.5 s over the cores); preparing once 1.1 s; a frame about 4.5 ms (particles 2.65 ms, glitter 0.6, tone 0.4, sheet 0.3, the water's step 0.3).
  - In the window at 1100 × 760, settled:
    - a 10 s profile puts the real work at about a fifth of a core: particles about 10%, the rest of the frame 4%, the forest's sound 2.5%, the harp 1.5%;
    - the process clock reads about a third of a core over the same 10 s, the cores running slowly between short bursts (see the ambient README);
    - the governor holds it near 16 to 20 frames a second.
- **Music:** the port renders the same as the approved stems: 260 of 7.2 million samples differ, by one step. Dropping dead partials made it 55 times faster than real time, with the same loudness and peak.

## Not verified yet

- **Hosted:** from the shelf, the capsule, and the Sound and Music masters with real playback.
- **The sound as heard in the game** (the stems were approved as files).
- **Windows and Linux:** left to CI.
- **Retina (scale 2) cost.**

## Next

- **Cost:** draw each picture in one burst on a worker, as Stillwater does, to escape the slow-clock penalty, and make the streaks cheaper. That would bring the process to Stillwater's level.
- **Frogs and lilies that answer a touch;** the moving light on the rock.

## Decisions

- Entry id 25: 24 is left free because the lamp checkout's Pocket Library has 23, which collides with TriPeaks.
- Without a live audio generator in the toolkit the scene is silent; there is no clip fallback.
- With Reduced Motion the water falls at half speed rather than stopping: the falls are the scene.
