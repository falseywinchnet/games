# Nature Cube handoff

## Verified

- Full application suite on the M4 Mini (Homebrew LLVM 22, toolkit 2ac5dcd): 69 of 69,
  including `cube_rules`, `cube_generator`, `cube_saves`, `cube_tiers`,
  `cube_view_contract` and `cube_audio`.
- Native window, hosted and `--dev`, at 1100 × 760 and 600 × 420, scale 1, driven by
  `tests/native.script`: every level, portals, arrival, finale, departure, help and the
  name card. Captures were inspected.
- Idle (`--profile-idle 6`, 10 s): 0 paint passes, 0 frames; the 60 callbacks are the
  capsule's command polling, as for every game.
- Animating (`tests/animate.script`: finale, departure and arrival back to back, 1100 × 760,
  scale 1, window in front, launched with `open -n`): 3.8 s and 3.8 s of process CPU over
  16 s, 23 to 24 percent of one core, music playing. The cube's own motion runs at 25
  frames a second (20 while the lines light), drawn at draft quality and sharpened once it
  settles. `cube_bench` on the M4: 2.0 ms to draw a draft frame, 0.5 ms to lay it over
  the lake; 8.3 ms for a full-quality settled frame.
- Old saves: `tests/fixtures/` were written by the previous engine (Hard, fill rule): a game
  in progress resumes and is won by joining the last pair with squares left open; a board
  waiting to be filled loads as won and offers its result once.

Tier measures (`cube_rules_tests tiers`, fixed seeds; Hard 5 boards, others 10):

| Level | Solutions | Wrong turns | Witness cells | Folds and hops | Forced starts | Detour |
|---|---|---|---|---|---|---|
| Easy | 162.1 | 44.3 | 31.6 | 3.6 | 1.40 | 3.2 |
| Medium | 22.1 | 67.3 | 47.1 | 5.8 | 1.30 | 7.1 |
| Hard | 1.0 | 330.4 | 68.2 | 6.4 | 0.80 | 11.0 |

Solutions are counted without detours (a line never runs beside an earlier part of
itself), capped at 200. Wrong turns are the median, over five pair orders, of the steps a
depth-first solver with connectivity lookahead takes before its first solution beyond the
steps that solution needs. Hard generation: 1.7 s mean, 2.9 s slowest on the M4; it runs on
a worker, and the next board is dealt while the current one is played.

## Not verified

- Display scale 2 on a real high-density screen (the M4's display is scale 1); the contract
  test covers 1.5 headlessly. Animation cost scales with pixels, so expect more there.
- Windows and Linux builds of the view (the core is portable C++20 and builds with Apple
  clang and LLVM 22).
- The portal sound was rendered only through `cube_audio_tests`; it has not been listened to.

## Decisions

- Nature Cube left the shared puzzle engine for its own folder: bigger boards (up to seven a
  side), portals and animation did not fit the engine's 96-cell state. `shared/puzzles`
  still contains the old cube code and its tests; it is no longer used by the shelf.
- New save `cube-v3.txt`. `cube-v2.txt` (and `cube-v1.txt` for names) are read once and
  left untouched, so an older build still finds its own file.
- Portal rules: entering either cell hops to its partner; the line leaves in any direction;
  a portal is never an ordinary cell and never a line's end; the first line through claims
  the pair until cut back past it. Tracing back onto the entry cell removes the hop. After a
  hop, steps count again once the pointer reaches the far side.
- Hard boards are carved with extra stone until the solver finds one solution; Medium until
  at most 40; portals are kept only where no solution exists without them.
- The view draws into its own canvas (the lake PNG is decoded with the ambient engine's
  inflate) and uses the shell's rail: a capsule floating over a live surface made the host
  repaint every display frame.
- Provenance: everything is original. The idea comes from 3D Logic; board sizes (three to
  five a side in 3D Logic 2) come from published descriptions. The game's SWF was opened
  only to look for its level data, which is obfuscated; nothing was taken from it.
