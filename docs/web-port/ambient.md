# Web port: the ambient group

Stillwater, Nature Cube, Koi-Koi, The Parrot's Table and Liar's Dice in the browser
build, checked on 2026-10-23 against `claude/web-port-ambient`.

## Status

| Game | Opens, plays, ends | Looks native | Threads | Saves across reload | Node tests |
|---|---|---|---|---|---|
| Stillwater | three tanks, tap, Next tank, Pause, help, back to the shelf | yes (planted, reef, pool) | load, layer and picture workers (at most 4 at once) | the chosen tank | `ambient_tests` 62324 checks, `stillwater_rules_tests` 5824, `stillwater_view_contract_tests` |
| Nature Cube | trace, solve (dev `action finish`), Top scores name entry, Settings level, New game on Hard | yes (traces colour their squares; `screens/readme.jpg` predates that) | one dealing worker | `cube-v3.txt` | `cube_rules_tests` rules, generator, saves, tiers; `cube_view_contract_tests` |
| Koi-Koi | play, a set, Stop and score, next round, menu | yes | up to seven PNG decoders at start (`std::thread`, joined; one fewer than the cores, at most 8) | score and hand | `koikoi_rules_tests`, `vendor_game_frame_tests koikoi` |
| The Parrot's Table | mark, name the culprit, win, Records, next table | yes | none | table and record | `parrots_rules_tests`, `vendor_game_frame_tests parrots` |
| Liar's Dice | wager, bid, call, reveal, lose the wager | yes | none | years owed | `liarsdice_rules_tests`, `vendor_game_frame_tests liarsdice` |

No console errors in any game. None of the five has an Undo. The view contract
tests need `new-games/kit`, which `tools/web/m4web` does not copy (rsync it once into
the mirror; an excluded path survives later syncs).

## Assets each game opens

Every game also opens the shared fonts (`fonts/`).

- **Stillwater**: `stillwater/scene/riverscape.ambient`, `reef.ambient`, `pool.ambient`
  (the shown tank; the next one when chosen), `stillwater/audio/stillwater_audio_manifest.json`,
  `stillwater/licenses/*` (shipped, not opened); audio `sw_ambience.ogg`, `sw_island.ogg`,
  `sw_tap.ogg` (about 2.8 MB; the tank voice is synthesized, the clips are its fallback).
  `stillwater/` is 12 MB.
- **Nature Cube**: `nature-lake.gpix` (2.0 MB; `nature-lake.png` only if the gpix is
  missing); audio `music_nature_cube_loop.ogg`, `nature_cube_*.ogg`,
  `stinger_win_nature_cube.ogg`, `stinger_topscore_nature_cube.ogg` (about 2.7 MB).
- **Koi-Koi**: `cards/hana_00.png` to `hana_47.png`, `hana_back.png`,
  `hana_back_red.png`, `card_shadow.png` (14.7 MB); audio `kk_*.ogg` (11 files, 2.1 MB).
- **The Parrot's Table**: audio `pt_*.ogg` (20 files, 1.1 MB).
- **Liar's Dice**: audio `ld_*.ogg` (46 files, 1.7 MB).

## The shared asset pack is stale

`~/Developer/PlaysuiteDependencies/runtime-modular`, which every worker's build links
to, was prepared from an older tree: it has no module asset folders (`stillwater/`,
`mowingman/`) and holds Koi-Koi's cards as `.bgpix`, which nothing reads since #9. In
it Stillwater shows "The scene could not be opened" and Koi-Koi draws no cards. This
branch was checked with a pack prepared from the worktree itself, on the Mini (whose
system `python3` has Pillow):

```sh
rsync -az assets/audio/ <mini>:<mirror>/assets/audio/   # m4web leaves it out
/usr/bin/python3 tools/prepare_portable_assets.py --output .build/runtime-web --ffmpeg /opt/homebrew/bin/ffmpeg
GAMES_RUNTIME_ASSET_DIR=$PWD/.build/runtime-web GAMES_WEB_CMAKE_ARGS=-DGAMES_WEB_SHARED_ASSETS= sh tools/web/mini-build.sh games
```

Remove the `assets.data`/`assets.js` links in `.build/web/web` first, or the packer
writes through them into the shared pack.

## Frame cost

### Same machine, wasm against native (M4 Mini, Node 25 against LLVM 22 `-O3`)

The host was shared with four other builds (load 10 to 40), so runs were interleaved
and repeated; the ratio is the reliable figure.

| Work | Native | Wasm | Ratio |
|---|---|---|---|
| Stillwater compose, reef / pool, 512 x 384 | 1.64 / 1.72 ms | 1.91 / 2.07 ms | 1.17-1.20 |
| Stillwater sway, reef / pool, 2 x 1 samples | 1.08 / 2.50 ms | 1.34 / 3.33 ms | 1.24-1.33 |
| Stillwater sway, planted, 2 x 1 (best runs) | 10.3 ms | 12.8 ms | about 1.25 |
| Cube turning, 2200 x 1440 | 4.55-5.02 ms | 5.55-6.15 ms | 1.15-1.25 |
| Cube hover or trace at rest | 1.22-1.63 ms | 1.48-2.01 ms | 1.2-1.3 |

SIMD128 is on for every object (`-msimd128`); the engines have no intrinsics, so both
builds autovectorize the same loops. Nothing points at a wasm-specific slowdown
beyond the usual 1.2-1.3: integer scan and float shading both sit there.

Pixels: `sw_preview` frames differ from native in 99 of 163,770 pixels (79 by one
level, a handful of edge pixels by up to 53), with `-ffp-contract=off` on both. The
remaining source is the C library's transcendental functions (musl against Apple's
libm) flipping a few coverage decisions. Not visible; the checksums are not portable.

### In the page (Chrome headless on the M4, 1024 x 768 at scale 2, muted)

| Game, activity | Main thread per drawn frame | Frames a second | Window damage | Worker |
|---|---|---|---|---|
| Stillwater planted, 24 pictures/s | 7.0 ms (scene's own draw and enlarge 1.2) | 24 | 3.15 Mpx (all) | picture 14.3 ms (sway 12.3, frame 2.0) |
| Nature Cube tilting | 10.9 ms | 24 | 2.07 Mpx | — |
| Koi-Koi a card in motion | 24.2 ms | 41 (timer asks 60) | 3.15 Mpx | — |
| Parrot's Table intro speech | 8.5 ms | 30 | 1.50 Mpx | — |
| Liar's Dice intro | 9.8 ms | 30 | 2.28 Mpx | — |

`putImageData` itself is 0.2-0.5 ms; the rest of each frame beyond the game's own
drawing is the host composing the window and staging the copy, roughly 2 ms per
device megapixel. Koi-Koi composes and publishes its whole window on every animated
frame (the same as native), so at scale 2 it cannot hold its 60 a second. Every game
settles to no drawn frames.

On the laptop (A18 Pro, two performance cores, five browser tabs from parallel
workers, pane often hidden and throttled) a Stillwater picture took 20-31 ms and the
main thread 9-12 ms per frame; the governor holds its budget there.

### Fixed in this branch

- Pictures were started only on every other tick in a browser (13 a second instead of
  24): the 24 a second timer lands on 60 Hz frames 33 and 50 ms apart, and the exact
  period test missed the short ones. Half a frame interval of slack restores 24.
- `GAMES_SCENE_TRACE` now reports as a scene goes behind the shelf (a page never
  closes its window), and `GAMES_SWAY_TRACE` prints each picture's start delay
  (0.1 ms: `std::async` thread starts are not a cost under the pool).

### Known web differences

- A worker's thread clock is the wall clock (Emscripten maps `CLOCK_THREAD_CPUTIME_ID`
  to `performance.now`), so the governor charges preemption too; and a worker cannot
  ask for a performance core. Both err toward fewer pictures.
- Ticks are quantized to the display: the scene's 42 ms interval arrives as 33/50 ms
  (sd 8 ms).
