# Web port: the puzzle games

Gems, Puzzle Solve, Untangle, Sudoku, Atom Probe and Four Pegs in the browser build,
checked on 2026-10-23 against a native 800 × 600 capture of the same revision on the
M4. The page was always opened silent (no `?sound`).

## Status

| Game | Opens, plays, ends | Looks native | Threads | Saves reload | Node tests |
|---|---|---|---|---|---|
| Gems | New game, swaps, cascades, Top scores, Settings, back to shelf. No Undo by design; the end (no swaps left) was not reached by hand | Yes | none | Yes | `puzzle_tests` pass |
| Puzzle Solve | Pieces drag, snap, turn (R), flip (F); Top scores. No Undo by design; the finished design was not reached by hand | Yes (plaster texture statistics match) | none | Yes | `puzzle_tests` pass |
| Untangle | Points drag, kitten walks; the untangled finish was not reached by hand | Yes | none | Yes | `puzzle_tests` pass |
| Sudoku | Digits, notes, mistakes, Undo, a full solve into Top scores and a saved name, New game generated off-thread through QuickJS, Night board | Yes | `std::async` generation finishes on the pool | Yes | `sudoku_tests` (needs the Web.cmake patch below), `sudoku_music_tests` pass |
| Atom Probe | Probes, markers, lever, a scored loss, New box, lab log | Yes | 8 `std::thread`s for the deducibility table finish on the pool | Yes | `atomprobe_rules_tests`, `atomprobe_storage_tests`, frame tests pass |
| Four Pegs | Pegs, Check, a win, signing the Book of Guests | Yes | none | Yes | `fourpegs_rules_tests`, `fourpegs_storage_tests`, frame tests pass |

No game raised a console error. The only warnings are the toolkit's ScriptProcessor
deprecation and one "Blocking on the main thread" notice.

## Assets each game opens

Every game also needs the shelf's fonts (`fonts/*.ttf`). Observed by tracing
`FS.open` while playing, completed from the sound names in the source.

- **Gems:** `audio/music_gems_loop.ogg`, `audio/gems_*.ogg`,
  `audio/stinger_win_gems.ogg`, `audio/stinger_topscore_gems.ogg`, `audio/ui_new_game.ogg`.
- **Puzzle Solve:** `audio/music_puzzle_solve_loop.ogg`, `audio/puzzle_solve_*.ogg`,
  `audio/stinger_{win,topscore}_puzzle_solve.ogg`, `audio/ui_new_game.ogg`,
  `audio/ui_name_confirm.ogg`.
- **Untangle:** `audio/music_untangle_loop.ogg`, `audio/untangle_*.ogg`,
  `audio/stinger_{win,topscore}_untangle.ogg`, `audio/ui_new_game.ogg`.
- **Sudoku:** `sudoku/engine.js`, `sudoku/calibration.json` (read for every new
  puzzle), `audio/sudoku_*.ogg`, `audio/stinger_win_sudoku.ogg`. Its music is
  synthesized live; `audio/music_sudoku_{day,night}_loop.ogg` are only the fallback
  when the build has no audio generator.
- **Atom Probe:** `audio/ap_*.ogg` (loaded lazily, first probe onwards).
- **Four Pegs:** `audio/fp_*.ogg` (music `fp_music_t1..t3`, voices `fp_voice_*`).

`audio/atom_probe_*.ogg` and `audio/four_pegs_*.ogg` belong to the shared puzzle
engine's older atom and pegs kinds, not to the hosted Atom Probe and Four Pegs.

## Frame cost

Measured in the page as main-thread time inside each animation frame (release build,
canvas 2048 × 1536 at devicePixelRatio 2). The browser pane was hidden and shared
with five other PlaySuite pages on a six-core laptop at load 10–12, so the absolute
numbers are inflated and noisy; repeated runs varied up to threefold.

| Game, action | Frames | Cost per frame |
|---|---|---|
| Gems at rest (gem glints) | 15 a second | median 9.6 ms |
| Untangle, kitten walking | 36 a second | median 4.7 ms |
| Untangle, dropping a point | one | up to 400 ms (static scene redraw) |
| Four Pegs, placing a peg | 27 a second | median 16 ms |
| Atom Probe, probe flight | 24 a second | median 35 ms |
| Sudoku, one move (whole window, before this branch) | one | 181 ms |
| Sudoku, one move (sheet only, this branch) | one | 273–350 ms under heavier load; about half the whole-window cost when measured back to back |
| Puzzle Solve, select / turn / flip | one each | 100–330 ms |

A settled Sudoku, Puzzle Solve, Atom Probe or Four Pegs schedules no frame.

Profiles (JS self-profiling with function names) put almost all of the slow frames
in Skia's raster pipeline: full-window radial and linear gradients, the box shadow
and the plaster `fill_image_pattern`. On wasm, `SkRasterPipeline_opts.h` selects
`SKRP_CPU_SCALAR`: one pixel per stage and no lowp pipeline, although Skia is built
with `-msimd128`. See "Skia on wasm" below.

## Skia on wasm (for the lead)

An experiment built Skia with `-msse4.1 -DSKRP_CPU_SSE41 -DSKCMS_PORTABLE` (SSE
intrinsics are lowered to SIMD128 by Emscripten), which needs `skcms.cc`'s
`#elif defined(__SSE__)` include block guarded with `&& !defined(__EMSCRIPTEN__)`.
It linked and rendered correctly, and a Sudoku whole-window repaint fell from 181 ms
to 77 ms. It then aborted (`unreachable` from `abort()`) on Sudoku's first partial
repaint, so it is not ready; a debug build is needed to see which check fails. The
archive is in an APFS clone of the Skia tree at
`~/Developer/PlaysuiteDependencies/web/web-puzzles-skia/out/sse41`.

## Shared-file fix needed: Node-linked tests added after Web.cmake

`cmake/Web.cmake` links test programs for Node by walking the directory's targets
when it is included, but the top-level `CMakeLists.txt` adds `sudoku_tests`,
`puzzle_tests`, `rules_tests` and others afterwards. Those link for the browser
(MEMFS, no `NODERAWFS`), so `sudoku_tests` cannot read `assets/sudoku/engine.js`.
Deferring the walk fixes it; with this patch `sudoku_tests` passes under Node
(generation 176 ms Easy, 2.3 s Medium, 1.0 s Hard) and `puzzle_tests` still passes:

```diff
diff --git a/cmake/Web.cmake b/cmake/Web.cmake
index 0943837..525aea9 100644
--- a/cmake/Web.cmake
+++ b/cmake/Web.cmake
@@ -45,17 +45,22 @@ add_custom_command(TARGET games POST_BUILD
     VERBATIM)
 
 # Test programs run under Node (node <test>.js), reading files straight from
-# the disk; main runs on a worker so it may block like a native test.
-get_property(games_web_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
-foreach(test_target IN LISTS games_web_targets)
-    get_target_property(test_type ${test_target} TYPE)
-    if(test_type STREQUAL "EXECUTABLE" AND NOT test_target STREQUAL "games")
-        set_target_properties(${test_target} PROPERTIES SUFFIX ".js")
-        target_link_options(${test_target} PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
-            -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB -sDEFAULT_PTHREAD_STACK_SIZE=4MB
-            -sPROXY_TO_PTHREAD=1 -sEXIT_RUNTIME=1 -sPTHREAD_POOL_SIZE=8 -sASSERTIONS=1 --profiling-funcs)
-    endif()
-endforeach()
+# the disk; main runs on a worker so it may block like a native test. Deferred
+# to the end of the directory, so the tests the top-level CMakeLists.txt adds
+# after including this file (sudoku_tests, puzzle_tests, ...) are linked so too.
+function(games_web_link_tests_for_node)
+    get_property(games_web_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
+    foreach(test_target IN LISTS games_web_targets)
+        get_target_property(test_type ${test_target} TYPE)
+        if(test_type STREQUAL "EXECUTABLE" AND NOT test_target STREQUAL "games")
+            set_target_properties(${test_target} PROPERTIES SUFFIX ".js")
+            target_link_options(${test_target} PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
+                -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB -sDEFAULT_PTHREAD_STACK_SIZE=4MB
+                -sPROXY_TO_PTHREAD=1 -sEXIT_RUNTIME=1 -sPTHREAD_POOL_SIZE=8 -sASSERTIONS=1 --profiling-funcs)
+        endif()
+    endforeach()
+endfunction()
+cmake_language(DEFER CALL games_web_link_tests_for_node)
 
 set(GAMES_WEB_SHARED_ASSETS "" CACHE PATH "Directory of an already packed assets.data + assets.js to link to instead")
 if(GAMES_WEB_SHARED_ASSETS)
```

## Changes on this branch

- Sudoku invalidates only the sheet and the status line above it for a move
  (pointer, keyboard, digit), not the whole window; the paper, halo and shadow are
  unchanged by a move. Pixel output is unchanged.
- `sudoku_tests` reports an escaping exception instead of dying as an unhandled
  promise rejection under Node.
- The Atom Probe and Four Pegs storage tests tolerate a scratch folder that will
  not be removed: Emscripten's Node file system on macOS reports a directory unlink
  as EPERM, not EISDIR, so `remove()` never falls back to `rmdir`.

## What remains

- Skia's scalar raster pipeline on wasm (above). Puzzle Solve redraws its full
  plaster wall and lamp for every selection, turn and drag frame, and Untangle
  redraws its static board when a point is dropped; both are far over a frame
  budget in the browser until the pipeline is vectorized or those scenes keep their
  static layers as pixels.
- Not reached by hand in the browser: the Gems, Puzzle Solve and Untangle finishes.
  Their end paths share the scoring and save code exercised by Sudoku's win.
- A Medium Sudoku takes about 2.3 s to generate on wasm (on its worker; the board
  shows "Making a new puzzle…").
