# Web port: Rock Stack and Catching Thieves

Checked on 2026-10-23 against this branch's browser build (emsdk 6.0.13, release
and `GAMES_WEB_DEBUG=ON` links), in a 1024 × 768 page at devicePixelRatio 2 on a
6-core laptop (`navigator.hardwareConcurrency` 6), always silent (no `?sound`).
Other workers' pages shared the browser and the Mini during the checks, so the
frame costs below are upper bounds.

## Status

| | Rock Stack (`zenconstruction`) | Catching Thieves (`catchingthieves`) |
|---|---|---|
| Opens from the shelf, back to the shelf | Yes | Only with a current asset pack (below) |
| Play | Fetch a rock, swing, lower, let go, new best; Jiggle; New site; Sites; Settings | Walk, push, Undo, win, next garden, Hint, difficulty, New garden, Settings |
| Saves across a reload | Yes: sites, stack and best | Yes: garden, moves and difficulty |
| Looks native | Matches `screens/readme.jpg` (that capture predates the GUI.Forms HUD) | Matches `screens/` |
| Console errors | None; a stream of `Audio rate: status 2` warnings (below) | None |
| Node tests | `rockstack_physics`, `_rules`, `_cache`, `_worksite`, `_shadow` pass | `ct_rules`, `ct_contract`, `ct_coon_motion`, `catchingthieves_view_contract` pass |

## Findings

**The asset pack is stale for Catching Thieves.** The pack is built from
`~/Developer/PlaysuiteDependencies/runtime-modular`. That directory has no
`catchingthieves/levels/gardens.txt`, so `ThievesView::load_tables` throws. From
the shelf the box does not open. With `?game=catchingthieves` the window closes
with no frame drawn. The directory comes from an older prepared runtime (it lacks
Mowing's `mm_*` sounds as well). The game ran normally on a test pack: the same
directory with `games/catchingthieves/assets/levels/gardens.txt` added at
`catchingthieves/levels/`, left at `/tmp/physics-runtime` and `/tmp/physics-pack`
on the Mini. The lasting fix is to pack a runtime prepared from this checkout with
`tools/prepare_portable_assets.py`. The view-contract test needs the same
directory as `GAMES_ASSET_DIR`.

**The motor's pitch is rejected, natively too.** `PcmPlayer::create_voice` opens
looping voices with pitch disabled (`!slot.loop`). Rock Stack sets the
`zc_motor` bed's rate every tick, so `AudioVoice::set_rate` returns
`invalid_value` each time. Natively that goes unseen on stderr and the motor never
whines up. In the page every rejection is a `console.warn`: about 60 a second
while the crane runs, and thousands over a session. The suggested patch enables
pitch on a looping voice that starts at a rate other than 1.

**Physics determinism.** The wasm `rockstack_physics_tests` passes all 17 cases
(determinism included). Its checksum differs from native: wasm `13c8966f6ec6b6a3`,
native `5ce089fba7002e83` (Homebrew LLVM 22, Release). The difference comes only
from shape cooking. The test shapes and `hull.cpp`'s axis-angle quaternion call
libm `sin`/`cos`, which `PHYSICS_SPEC.md` allows outside the step. With both
compiled against one `+ − × ÷` sine (a diagnostic, not committed), native and wasm
give the identical checksum `a7647e68eb1a75bb` over the 600 frames. So the
simulation step is bit-identical across the two platforms. A seed's rocks can still
differ in their last bits between platforms, because the rock factory also uses
libm. Saves do not travel between them, so this does not show.

## Threads under the 16-worker pool

Rock Stack, on the main thread:

- r3d bands: `min(8, hardwareConcurrency) − 1` workers (5 here, 7 on an 8-thread
  or larger machine). They are created on the first moving frame and kept for the
  game's lifetime, hidden or not.
- Rock factory (`Run::make_rocks`): `min(4, hc) − 1` = 3 threads. The main thread
  joins them when a saved site is loaded (constructor, Sites).
- New site: one `std::async` thread, which starts 3 more from itself.
- Peak: 11. Nothing deadlocked. A main-thread join of a thread that cannot get a
  pooled worker would deadlock, because a new worker loads only when the main thread
  yields. The factory and `run_bands` therefore depend on free workers in the pool.

Catching Thieves:

- One field worker. It runs the grass engine's `in_parallel` with
  `hardwareConcurrency` threads, which is uncapped: 5 here, 15 on a 16-thread
  machine.
- One generator thread, plus any retired generators still winding down.
- One hint thread.
- On a 12-thread or larger machine the field alone exhausts the pool.
  `join_field` and the destructor join the field worker from the main thread. That
  worker in turn joins children that may be waiting for a new worker, which can
  deadlock. The suggested patch caps `lawn_threads` at 4 on the web. The output
  does not depend on the thread count.

## Frame cost

Measured as the time inside each `requestAnimationFrame` callback, in the release
build. Native figures come from `docs/ENGINE_REVIEW.md` and the game READMEs.

| Scene | Web | Native reference |
|---|---|---|
| Rock Stack, settled (brook at 10 frames/s, serial) | 27 ms median per frame, 683 × 512 scene | 9.8 ms serial, 960 × 600 window (640 × 400 scene) |
| Rock Stack, crane and camera moving (threaded) | 29 ms median, 59 ms p90 | 7.0 ms threaded draw at 960 × 600, plus a 3.75 ms mean physics step |
| Rock Stack, Jiggle (50 rocks) | 50–98 ms per frame (about 14 frames/s) | Not recorded |
| Catching Thieves, idle animation (15 frames/s) | 12 ms median, 18 ms p90 | "5 to 10 ms" for the largest garden |
| Catching Thieves, opening the game | Two main-thread tasks of 660 and 730 ms (debug link) | Not recorded |

Physics alone runs about 2.5 times slower than native under Node (17 test cases:
8.9 s against 3.5 s). The shelf costs nothing while either game is hidden.

## Assets opened

Rock Stack: `state/zen_construction-v1.txt`, plus 32 audio files at
`audio/zc_*.ogg` (4.8 MB): bed_1–3, best, bird_1–3, brook, click, clip, clunk,
frog_1–3, knock_1–3, land, motor, music, reeds, relay, reset, rush, splash_1–4,
squeak, tumble, unclip and whoosh.

Catching Thieves: `catchingthieves/levels/gardens.txt` (from
`games/catchingthieves/assets`; missing from the current pack),
`catchingthieves/audio/thieves_audio_manifest.json`,
`state/catching_thieves-v1.txt`, plus 27 audio files at `audio/ct_*.ogg`: bump,
click, crunch, duck, hint, laugh, muffle_01–02, music_{spring, summer, autumn,
winter, night}, pop, push_01–03, raspberry, restart, step_01–03, stinger_{book,
stuck, win}, trap and undo. The `ct_ui_name_*` sounds are in the pack, but no
source opens them. The 8 MB source `assets/` folder is mostly the m4a/wav masters.
The prepared `ct_*.ogg` files come to 5.3 MB.

Both games also use the shelf's shared files: the fonts, `back_*.png` (settings
card back) and `audio/music_menu_loop.ogg`.
