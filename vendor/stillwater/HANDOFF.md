# Stillwater handoff

Stillwater is the native Stillwater aquarium (Metal, macOS) ported to PlaySuite as
a processor-only scene, and the first game on the shared ambient engine
(`engines/ambient`). The engine tier itself (`engines/`, discovery, gate and kit
rules) arrived in the same change; see `new-games/guide/11-shared-engines.md`.

## Verified

On the M4 Mac mini (macOS, LLVM 22, toolkit `d58f530`, Skia CPU), 2026-10-04:

- Game's own headless build (`cmake -S vendor/stillwater`): no warnings;
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
