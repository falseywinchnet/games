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
- **Saves.** `stillwater-v1.txt` holds paused, detail and (since the tanks) scene; there is no progress.
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

## 2026-10-06: caustics and a choice of tanks

On the M4 Mac mini (`.build/opaque-app`, LLVM 22, toolkit `toolkit-opaque`, Skia CPU),
all 60 tests pass, including `ambient_engine` (46,353 checks), `stillwater_rules`
(5,816) and `stillwater_view_contract`; Mowing's six tests pass unchanged. The game's
own headless build (`cmake -S games/stillwater`) has no warnings.

- **Caustics** replace the labelled wave approximation. `ambient::CausticField`
  (`shared/ambient/src/caustics.*`) traces light through a tileable, looping
  rippled surface once per look (about 20 ms on the loading worker, 1 MB) and keeps
  32 steps, sharp and soft. Surfaces are reduced to taps per layer; a frame reads two
  bytes per lit pixel and blends with packed integer arithmetic. The light falls on
  sand, stones, wood, coral and the chest (sharp near the surface, soft and dimmer on
  the floor), on the swaying leaves (soft only: sharp lines turned into dashes on
  edge-on blades) and on the fish and crabs. In the look, the share of sunlight the
  ripples gather (`TankStyle::caustic_share`) leaves the even light and returns in
  the lines, so the tank is not brighter overall. `SW_CAUSTIC_TILE=file.png` and
  `SW_CAUSTIC_STATS=1` make `sw_preview` show the pattern and its strength by height;
  `SW_NO_CAUSTICS=1` draws a tank without them.
- **Tanks.** `Scene` (S, and the capsule) cycles the planted tank, a coral reef and a
  river pool. `scene_src/build_tanks.py` writes `assets/scene/reef.ambient` and
  `pool.ambient` (deterministic; `--check` compares) from new procedural content
  (staghorn and brain corals, sea fans, soft corals, anemones with swaying tentacles,
  sea grass and sea whips; tree roots, sunken branches, leaf litter, gravel and
  eelgrass) and from the planted archive's shared parts (camera, sand floor, material
  maps, boulders, fish meshes, crab). Each tank has a `TankStyle` (`src/tanks.cpp`):
  water, light, fog, sand and rock tints, caustics and three fish colourings, chosen
  per fish by its archive tint (tangs, clownfish with bands, yellow tangs; minnows,
  red-finned rudd, banded darters). The chest dresses the planted tank and the reef.
- **Engine** (`shared/ambient`): `DioramaScene` and `SceneSetup::scenes`; the Diorama
  loads and builds the next scene on workers while the shown one dims (0.45 s), then
  swaps and brightens it (0.6 s), on the wall clock so it completes while paused
  (`SceneContext::redraw` keeps the view drawing); a second press redirects a change
  in flight. `FixedLayer::light_position` now keeps x, y, z. Looks without a field
  keep `animated_light` unchanged (Mowing does not use the stage).
- **Saves.** The choice is the `scene` line of `stillwater-v1.txt`; a file without
  one opens the planted tank (tested in `stillwater_view_contract` with a pre-scene
  file, then cycling while paused, saving, a double press and reopening).

Cost, planted tank (the default), measured before and after on the same machine:

| Measure | Before | After |
|---|---|---|
| Headless compose (`sw_preview`, 590 x 380, 240 frames) | 1.39 ms | 1.56 ms |
| Headless sway update | 8.81 ms | 9.71 ms |
| Headless scene work at 24 fps + 8 sway | 104 ms/s | 115 ms/s |
| App draw per frame (`--profile-idle 20`, 530 x 309 scene) | 7.81 ms | 7.82-7.91 ms |
| App process CPU over 10 s (rates at their floors, 12 + 3) | 27.2 % | 27.7-28.7 % |
| Resident memory | 200 MiB | 208 MiB |

The extra work is the light on leaves and fish, which the old pattern did not reach;
the budget (10 % of a core for the scene's own work) and the governor are unchanged,
so the frame and sway rates absorb it. Reef: compose 1.90 ms, sway 0.52 ms, app
23.0 %, 168 MiB. River pool: compose 1.65 ms, sway 1.89 ms, app 23.4 %, 166 MiB.
Opening a tank costs about 130-180 ms on workers (archive, look and caustics, shadow
map, fixed layer).

Not verified: a Retina window; the look of the new tanks on other displays; the
caustic pattern's repetition (one tile is 3.2 units, turned 27 degrees) at Fine on a
very large window.

## 2026-10-07: the native Stillwater's sound restored; the reef rebuilt

On the M4 Mac mini (`.build/app`, LLVM 22, toolkit `toolkit-opaque`, Skia CPU), all 60
tests pass; `scripts/check-style.py` is clean; `build_tanks.py --check` matches.

### Sound

The port's water and tap came from the native app at 4a80ee3, which still had its
first sound. Hours later the native app replaced it (3f6830f, 424bea9, its
`docs/ATMOSPHERE.md`): the owner had heard the tonal loop, the rising bubble sweeps
and the ringing 730 Hz tap and found them too prominent, and the refined version is the
one they kept. The port never received it. `src/tank_voice.cpp` now carries that
refined recipe, measured against the original's own code (a harness compiled from its
`src/sound.cpp`):

| Layer | Native Stillwater (424bea9) | Port before (PR #8) | Port now |
|---|---|---|---|
| Filter motor | 60 Hz series, five modes weighted to 120 Hz, amplitude stirred ±8 % by 89 Hz-filtered mechanical noise, a little of that noise heard; right ear 0.94 | two steady sines, 55 and 110 Hz (the first draft) | the original, by frequency at 48 kHz |
| Return water | continuous noise band 42-612 Hz, independent per ear, very quiet | low-passed noise (about 270 Hz) swelling on 16 and 64 s sines: a rumble, the loudest layer | the original; reef ×1.6; pool ×2.1 (the current); ±12 % with the flow |
| Air stone | about 24 bubbles/s at exponential waits; radii 0.7-3.2 mm (biased small) ringing at their Minnaert pitch (1-4.7 kHz), fixed pitch, damped 75-255/s; 24 voices; levels skewed quiet; softened by an 872 Hz low-pass; panned 0.25-0.65 | about 3/s in clusters, 440-1340 Hz sines rising 1,600 Hz/s (the sweeps the owner rejected), 8 voices, unfiltered, loud | the original, but the flow that paces them really wanders (15-33/s over 10-40 s; the original's wander was ±0.002), panned toward the column on the right; pool: about 1.3/s larger silt bubbles anywhere |
| Glass tap | 200 ms fingertip: 220 Hz damped body, faint 920/1770 Hz glass, contact noise; peak -42.7 dBFS, beside the ambience; synthesized per tap, equal-power pan | the rejected 600 ms 730/1931/3173 Hz ring and 157 Hz knock, about 34 dB louder than the kept tap; one clip | the original, live in the tank voice, placed at the click; each tap ±6 % pitch, ±10 % level and new noise |
| Scatter, feeding, surface | none (a surface-splash layer was removed at the owner's request as "mechanical clatter"); no feeding | none | none, deliberately |
| Level | RMS 0.0036 (-48.8 dBFS), alone | RMS 0.066 (-23.6 dBFS), louder than the band (0.047) | RMS 0.0116 (-38.7 dBFS): the original +10 dB (`TankVoice::suite_level`), about 12 dB under the band |
| Rate | 22,050 Hz | 48 kHz | 48 kHz; one-pole filters carried by frequency, noise power matched |

Octave-band spectra of the original and of the port at the original's level agree
within 0.5 dB from 20 Hz to 5.6 kHz (the 180 Hz band within 1.8 dB) and their RMS
within 0.3 %. `sw_music_render original|tank|taps|tapped|tankloop|tap` renders them.

Engine: `ambient::SceneVoice` (`shared/ambient/ui/diorama.hpp`) is a live bed that
hears the shown scene and may play the taps itself; `SceneSetup::live_ambience` is now
a `SceneVoiceFactory`. Taps pass through a four-slot lock-free queue; one the device
did not take within a third of a second (sound off) is dropped. The `sw_tap` and
`sw_ambience` clips (no-generator fallback) are rendered by the same voice through
`audio_src/make_audio.py --render <sw_music_render>`; the manifest is unchanged.

### Reef and pool

`build_tanks.py`: the reef is rebuilt. Live rock forms a back wall rising toward the
sides, two bommies and a sand channel; on it, seven table corals (dished plates on
stalks, tipped toward the glass so their tops show), ten staghorn and bushy thickets
of thick forking branches, finger corals, brain corals with meandering valleys,
Montipora plate whorls, leather toadstools, mushroom discs, zoanthid mats, tube
sponges, three lace sea fans with open holes, and an encrusting pass of 520 small
heads, mats and shelves on every upward face and face toward the glass (`RockMap`,
`encrust`, `encrust_faces`); rubble along the channel. Fish are reshaped from the
tetra's meshes to real outlines (`reshaped_fish`: body depth to an outline, dorsal and
anal fins stretched and re-seated, tail forked or rounded): 13 green chromis and 10
lyretail anthias in schools (shared loop, offsets, near phases), 3 yellow tangs
(discs), 2 blue tangs, 4 clownfish, the hermit crab. `TankStyle` gained six fish
colourings, `film`/`turf` (pink coralline crust on the reef's rock), `surface_glow`
and `shafts` (static sunbeams in the open water). The pool gained a far bank of
stones out of the murk, a minnow shoal that keeps together, and warm sunbeams.

| Measure (headless, 590 x 380) | Reef before | Reef now | Pool now | Planted |
|---|---|---|---|---|
| Static triangles | 327,241 | 535,262 | 319,059 | 330,504 |
| Fixed layer | 65 ms | 79 ms | 46 ms | 57 ms |
| Compose / frame | 1.74 ms | 1.47 ms | 1.51 ms | 1.48 ms |
| Scene work at 24 fps + 8 sway | 45.7 ms/s | 39.3 ms/s | 50.8 ms/s | 111 ms/s |

The reef costs less per frame than before (smaller fish), more once per size.

Process CPU in the app (1060 x 680 window, scene 530 x 309, Balanced; `ps` CPU time
from 15 s to 35 s; four runs each, alternating builds, on a host shared with other
work): planted 17.9 % before and 20.3 % after (runs 13.6-23.4 and 14.4-23.3), reef
15.4 % and 15.8 % (14.3-16.8 and 13.2-19.8), pool 13.4 % and 17.8 % (8.8-16.4 and
14.4-22.0). The spread within a build is wider than the difference between them; the
governor spends the budget it is given and the scene's own per-frame work is unchanged
(planted, pool) or lower (reef). The tank voice itself renders at about 1,500 times
real time, 0.066 % of a core (the PR #8 voice: 0.15 %). Resident memory: reef 225-246
MiB (was 193-228; more static geometry), others unchanged.

Captures from the real window (1180 x 800, by script) of all three tanks before and
after are with the review material, not committed.

Not verified: listening (the samples are rendered for the owner to judge: the
original's own, the PR #8 port's and the new voice's, per tank, with taps); Retina;
the reef on other displays; Windows and Linux (CI).
