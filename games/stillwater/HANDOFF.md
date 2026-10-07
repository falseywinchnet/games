# Stillwater handoff

Stillwater is the native Stillwater aquarium (Metal, macOS) ported to PlaySuite as
a processor-only scene, and the first game on the shared ambient engine
(`shared/ambient`). The engine tier itself (`shared/`, discovery, gate and kit
rules) arrived in the same change; see `new-games/guide/11-shared-engines.md`.

## Verified

On the M4 Mac mini (macOS, LLVM 22, toolkit `d58f530`, Skia CPU), 2026-10-04:

- Game's own headless build (`cmake -S games/stillwater`): no warnings;
  `ambient_engine` (38,342 checks) and `stillwater_rules` (the shipped archive:
  loads, validates, look ranges, grass, fish, light, sway, startle, reproducible
  frames) pass.
- Full native application (`.build/modular-app`): builds with no warnings in the
  engine or this game; the complete suite passes, 51 of 51, including
  `stillwater_view_contract` (frames at 1180 x 800, F1 help and Escape, 150 %
  scale at 1770 x 1200, 600 x 370 and 600 x 320, no timer callbacks or frames
  while hidden, resume), `catalog_ui`, `shelf_help` and `game_catalog` (17 tests,
  two for engines).
- The native window, standalone and hosted, by script: first frame is the backdrop
  until the scene is ready, then the tank; a tap scatters the nearby fish (with the
  knock); help card and Escape; resizes to 600 x 370 and 600 x 320 rebuild the
  fixed layer while the old picture stays up; Pause holds a byte-identical frame;
  Detail cycles; from the shelf, the box, the shared help, the capsule (Pause is
  primary) and returning to the game all work. Captures are in `screens/`.
- Cost, measured as process CPU over 15 s with `ps` at 1060 x 680 points, scale 1,
  no script: animated Balanced 18.3 % of one core (217 MiB resident); paused
  0.5 %, the same as an idle Maze (0.6 %), so a paused tank adds nothing
  measurable. With the script harness running: Fine 25 %, Light 16 %. A sample of
  the animated process puts about 11.6 % in this game's work and about 6 % in the
  host's per-frame painting (see `docs/TOOLKIT_REQUESTS.md`, item 5).
- Headless timings (`sw_preview`, 590 x 380): archive load 61 to 77 ms, shadow
  map 20 ms, foliage preparation 16 ms, fixed layer 55 ms (first frame about
  170 ms after the archive opens, off the UI thread); a composed frame 1.5 ms; a
  foliage update 9 ms.

## Not verified

- Sound quality: the ambience and the tap were synthesized and level-checked
  (ambience peak 0.3), not listened to.
- Windows, Linux and the release packages: left to CI. The local runtime assets
  were prepared with a scratch ffmpeg stand-in that encodes Vorbis through
  libsndfile, because this machine's Homebrew ffmpeg lacks libvorbis; CI prepares
  its own with ffmpeg.
- A Retina (scale 2) window's cost and look, and the inactive-window halving,
  were not measured; the display here is scale 1. Host painting grows with device
  pixels, so expect a higher total at scale 2.
- Machines without a GPU or with older processors: not run. The governor is built
  for them (it lowers rates to 12 frames and 3 foliage updates a second before
  exceeding its budget), and Light detail halves the budget.
- Real hand-driven input and the Reduced Motion master in the hosted shell.

## Decisions

- **Same scene, made for the processor.** The owner chose the faithful scene over a
  procedural rebuild. `scene_src/build_scene.py` reads Stillwater's archive
  (commit 4a80ee3, SHA-256 in `scene_src/build_report.json`) and writes
  `assets/scene/riverscape.ambient` (5.6 MB): fixed meshes quadric-decimated with
  borders kept (330,504 triangles drawn once per size); foliage ribbons thinned
  exactly along their row/column lattices (502,066 to 126,426 triangles); fish
  decimated to 800 body and 400 fin triangles, chosen by comparing captures at
  scene resolution with the full meshes; material maps at 256 x 256. Authoring
  needs NumPy, Pillow and `fast_simplification`; nothing new at run time (the
  engine has its own zlib decoder).
- **Approximations, named.** Foliage lighting is linearized in the bend; foliage
  shadows are cast at rest and moving things cast none; foliage that cannot move
  half a pixel at the current size is drawn once with the fixed layer;
  see-through grass is blended per plant in depth order (Metal used 4x MSAA
  alpha-to-coverage); the rippling light is the original's labeled wave
  approximation, refreshed every frame from cached positions. The thinnest
  needle-leaved plant reads as fine dotted whorls at Balanced detail (the original
  shows them softened by multisampling); widening sub-pixel strands offline was
  tried and measurably did not help, so it was removed.
- **Cadence.** 20 frames and 6 foliage updates a second preferred, budget 10 % of
  a core for the scene's own work (Light 5 %, Fine 20 %), half while another
  window is in front, nothing while paused, hidden or occluded. The settled-idle
  contract does not apply: the fish and grass move while visible
  (`settles_when_untouched: false`), and stopping is one key away.
- **Reduced Motion** holds the grass and the light still; the fish keep swimming.
- **Sound.** The ambience follows the Music master and stops while paused; the tap
  follows the Sound master. The 12 MB ambience master WAV is not committed:
  `audio_src/make_audio.py` regenerates it and the shipped files deterministically.
- **Saves.** `stillwater-v1.txt` holds paused and detail only; there is no progress.
- **Provenance.** Riverscape geometry and formulas: Desktop Habitats, Chase Lean,
  MIT (`assets/licenses/desktop-habitats-MIT.txt`). Material maps: Poly Haven,
  CC0. ACES fit: Three.js, MIT. Fish, crab and bubble motion, the audio and all
  code here are new or carried from Stillwater (same owner).

## 2026-10-06: chest, shanties, sound routing, buffering and cost

On the M4 Mac mini (`.build/mowing-app`, LLVM 22, Skia CPU), all 56 tests pass.

- **Sound vs music.** The water ambience was played in the shell's music slot, so
  the Music control silenced the tank. It is now sound: a live `TankVoice`
  (`src/tank_voice.cpp`, the same recipe as `make_audio.py`, matched to the loop's
  level: RMS 0.067 vs 0.066) through `SoundDesk::live`, under the Sound master;
  the `sw_ambience` loop is the fallback bed. No saved setting held this, so no
  migration was needed (saves still hold paused and detail only).
- **Music.** `src/island_voice.cpp` (2026-10-06, replacing the 6/8 shanty band at
  the user's request for something like Bikini Bottom's 1950s Hawaiian library
  music; the tunes are its own): lap steel (slides, slow vibrato, sixths), ukulele
  (finger-shaped Karplus-Strong plucks through a two-resonance body, strum brush,
  palm chunks, a hand position per section), upright bass (additive, finger thump),
  vibraphone and marimba answers, shaker, bubbles; swung 4/4 at 84-100 on sixth,
  major-seventh and dominant chains, sections joined by the II7-V7 vamp where the key
  moves (C, F, G, Bb); spring reverb, tape wow and flutter, narrow band, nearly mono.
  `sw_music_render band out.wav seconds seed part` solos a part. Through
  `SoundDesk::live_music`. About 120x real time. Fallback loop
  `assets/audio/sw_island.m4a` (96 s). The user listened and approved it after
  the bass (a buzzy Karplus-Strong) and ukulele were rebuilt.
- **Treasure chest** (`src/treasure.cpp`): planked wood with seams, iron straps,
  barrel lid open 35 degrees, a lumpy gold heap with a ruby, coins on the sand,
  three slow bubbles. Added to the archive on load through the engine's new
  `SceneSetup::dress`; it is fixed scenery (about 1,000 triangles shaded once per
  size), so a frame costs nothing extra. Gold and iron are new materials (15, 16)
  with a highlight; the gold takes the rippling light, which makes it glint.
- **Engine additions** (`shared/ambient`): `SceneSetup::music`, `dress`,
  `live_ambience`, `live_music`; `Diorama` plays the ambience as a bed. Mowing's
  `live_music`, Q and repeat handling are unchanged.

### Buffering study

`GAMES_SCENE_TRACE=1` makes a scene print its pacing when it closes. Measured with
`--profile-idle 20` (10 s sample, 1060 x 618 window, scene 530 x 309, Balanced) and
headless with `sw_preview` (590 x 380).

Found:
1. *Frame hitches from the foliage.* Every sway update (9 ms headless, up to ~25 ms
   in the app) ran inside a frame on the UI thread: frame drawing averaged 12.9 ms,
   max 37-40 ms, against 1.5 ms for a frame without it.
2. *Timer phase resets.* Any change of the governed interval re-armed the timer from
   the end of the tick's work, stretching that frame: interval max 93-137 ms at a
   target of 83 ms.
3. *Wasted foliage.* 80,000 of 118,000 redrawn foliage triangles drew no pixel; 34,000
   of them are hidden by fixed scenery wherever they sway, or off the picture.
4. *Actor vertices projected per triangle* (three divisions per triangle, not one per
   vertex).
5. *Audio.* A 64 s loop decodes to 24.6 MB of float PCM (plus a 16 MiB codec arena
   while decoding); the live voices hold nothing and need about 0.25 % of a core
   for both on the audio thread (256-frame device blocks). No lease misses (frames
   drawn but not presented) were seen.
6. *Host presentation on macOS* is a full retained paint per frame (no direct live
   surface; Skia clears then draws the window): `docs/TOOLKIT_REQUESTS.md` item 5.
   Not changed here.
7. The app's own frame costs run 3-6x the headless timings (the window process is
   scheduled at a lower performance level), so the governor holds Balanced at its
   floors (12 frames, 3 sway updates a second) at this size.

Changed:
- Sway updates (after the first) run on a worker into a second sway buffer and are
  shown with the next frame (`Stage::build_sway`/`show_sway`, double-buffered).
- The timer is re-armed only when the interval changes by 2 ms or more, and then
  from the tick's start (`Timer::start_at`).
- `hide_foliage` drops, per fixed layer, foliage that cannot show (exact: pixel
  comparison tests in `stillwater_rules`).
- Scan conversion solves each row's covered span and rejects triangles holding no
  pixel centre before fixed-point setup; actor vertices are projected once.
- `sw_preview` printed the settled count after a move ("of 0"); fixed.

| Measure | Before | After |
|---|---|---|
| Headless sway update | 9.09 ms | 7.9-8.1 ms |
| Headless composed frame | 1.53 ms | 1.31 ms |
| Headless scene work at 24 fps + 8 sway | 109 ms/s | 96 ms/s |
| App frame drawing (UI thread), mean / max | 12.9 / 37-40 ms | 7.8 / 11-12 ms |
| App frame interval max (target 83 ms) | 93-137 ms | 84.7 ms |
| Process CPU, 10 s | 21.6-27.2 % | 28.2 % (same floor rates; noisy) |
| Resident memory | 196 MiB | 201 MiB |

Process CPU did not fall: the governor spends the budget it is given, and rates
stayed at their floors in this window, so the gains appear as smooth frames and
headroom. The original pre-chest build could not be swapped in for comparison;
the HANDOFF's earlier 18.3 % (1060 x 680) is the older reference.

Not yet done: listening to the shanties and the live water; Retina; why in-app
compose is so much slower than headless (QoS); toolkit direct presentation on macOS.

### Why the app costs 3-6x the headless timings (2026-10-06, follow-up)

The cause is not QoS, backing scale or locking. In the app the scene is 530 x 309
at scale 1. The main thread runs at the default priority (31, from `ps -M`), and
forcing user-interactive QoS changes nothing. The sway worker shares no lock with
the frame.

The cause is the idle time between frames. `sw_preview` now takes a ninth
argument, `pace`: the milliseconds to sleep after each timed frame. Timings at
530 x 309:

| Pace | Compose | Sway update |
|---|---|---|
| 0 ms (back to back) | 1.07 ms | 7.7 ms |
| 5 ms | 1.40 ms | 10.2 ms |
| 15 ms | 3.32 ms | 24.3 ms |
| 40 ms | 5.30 ms | 31.6 ms |
| 80 ms (the app's 12 frames a second) | 5.9 ms | 31.5 ms |
| 80 ms busy-waited instead of slept | 1.09 ms | 8.1 ms |

At 12 frames a second the processor falls back to its idle performance state
between frames, so each short burst of work runs at a fraction of full speed. The
app's 7.8 ms frames are 1.1 ms of work done slowly. Process CPU, being a measure
of time, is inflated the same way.

Cutting cycles per frame still helps. Most of the time that the governor and `ps`
report, though, comes from this scheduling effect. Keeping the clock up by
spinning would burn energy just to improve the number, so it was not done.

About 20 % of the active samples are the toolkit's full-window repaint of the live
surface on macOS (a clear, a blend and a byte-order swap). That is recorded in
`docs/TOOLKIT_REQUESTS.md`.
