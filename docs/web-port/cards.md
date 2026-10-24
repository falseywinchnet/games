# Card games in the browser

Solitaire, Spider, FreeCell and Hearts share one table (`shared/cards/table.cpp`),
one press (`card_press.cpp`), one dealer (`dealer.cpp`) and the felt
(`shared/felt`). What holds for one holds for all four unless stated.

## Status

| Game | Opens | Play, undo, new game | Win / loss | Settings, Top scores | Saves across reload |
|---|---|---|---|---|---|
| Solitaire | yes | yes | won with hints; score offered for a name | yes | yes |
| Spider | yes | yes (moves, stock rows) | not played to the end | yes | yes |
| FreeCell | yes | yes | won with hints in the browser | yes | yes |
| Hearts | yes | yes (pass, tricks, computer players) | a hand played to the end | yes | yes (mid-hand) |

The browser pane was hidden during testing, so the page's `requestAnimationFrame`
was throttled; checks drove `_gf_web_tick` by hand. Wins were played by pressing
Hint and Enter until the deal came out. A fresh page showed no console errors through
new games, level changes, shelf switches, a Hearts hand and the FreeCell win; six
"Uncaught" reports seen once in an earlier long session could not be reproduced.
A settled table ran no ticks in ten seconds.

In every PlaySuite build the shell takes H for help before the table sees it, so the
table's "H gives a hint" never fires; that is not a web difference.

## Asset pack

The Mini's shared runtime directory
(`~/Developer/PlaysuiteDependencies/runtime-modular`) is older than the code: it
has the retired `card_00..51.png` faces and `.bgpix` hanafuda but not
`classic_00..51.png` or `back_1899.png`. With it the table throws
"Missing card artwork" while the window attaches, the host reports
"Web host attach rejected", and the whole page stays blank; a saved entry of a
card game then blanks the shelf on every later load too. Re-preparing the
runtime with `tools/prepare_portable_assets.py` fixes it. For testing, a private
copy of that directory with the 53 missing PNGs added was packed separately.

## Files the four games open

All under `/assets`:

- Faces: `classic_00.png` .. `classic_51.png` (2.8 MB).
- Backs: `back_0.png` .. `back_3.png` and the Classic back `back_1899.png`; the other
  backs are drawn (`card_backs.cpp`). Shadow: `card_shadow.png` (0.3 MB together).
- Fonts through the shell, as every game.
- Audio (`audio/`, 7.6 MB together):
  - music `music_klondike_loop.ogg`, `music_spider_loop.ogg`, `music_freecell_loop.ogg`,
    `music_hearts_loop.ogg`;
  - wins `stinger_win_klondike.ogg`, `stinger_win_spider.ogg`, `stinger_win_freecell.ogg`,
    `stinger_win_hearts.ogg`;
  - effects `card_shuffle_01..02`, `card_flip_01..04`, `card_pickup_01..04`,
    `card_place_01..04`, `card_foundation_01..04`, `ui_undo_01..02`, `ui_hint_01..02`,
    `ui_invalid_01..02` (`.ogg`).

Not used by these games: the old `card_00..51.png` (unused by any current code) and
`cards/` (Koi-Koi's hanafuda, 35 MB).

Saves under `/state`: `cabinet-v1.txt` (the table), `card-deals.txt` (the dealer's
ready deals), scores and settings through the shell. The dealer's write-then-rename
works on IDBFS; its file appeared with a deal for every game, option and level, and
survived reloads.

## Threads

- The dealer runs one `gui_forms::Worker` at a time, only while a table is shown.
- `table_art.cpp` lends the felt and the press a crew of
  `min(hardware_concurrency, 8)` threads: the caller plus up to seven Workers made for
  each batch and joined before it returns. At most eight of the pool's sixteen at once
  (nine with the dealer), all joined on the UI thread by spinning, as the host allows.
- No deadlock was seen: felt, backs, printing and background deals all completed.

## Node tests

| Test | Result |
|---|---|
| `rules_tests` | passes |
| `felt_tests` | passes |
| `solver_tests` | fails with OOM as built; passes (70 s) once it gets the Node link options |

`cmake/Web.cmake` gives Node link options to the executables that exist when it is
included, but `CMakeLists.txt` declares `rules_tests`, `solver_tests` and later tests
after including it. `solver_tests` therefore links with a fixed 16 MB heap (natively it
peaks at 30 MB). The fix defers the loop to the end of the top-level directory; with it
applied in the Mini mirror only, `solver_tests` linked with the Node options and passed:

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

## Frame cost

The window was 1024 x 768 at device pixel ratio 2 (3.1 M device pixels), Release
build, pane hidden, laptop shared with other workers. All figures are noisy.

- Full repaint of a settled card table: 0.5-1.1 s on a quieter laptop, 2.6-6 s when the
  other workers were busy. Four Pegs on the same page: 7-9 ms; the shelf: about 450 ms.
- Moves and deals: 0.3-1 s a frame, so a deal shows only two or three frames.
- A temporary build that skipped parts of `Table::on_paint` (not committed) put the
  cost in Skia's raster pipeline: the felt (a bilinear repeating image shader, 256 px
  tile drawn at 2x, as natively) costs most, then the printed cards and their shadows
  (alpha blits), then the lamp (a radial gradient, about 0.5 s). With all four skipped a
  full repaint took 5-19 ms.
- The Mini's Skia wasm archive was built with `SKVX_DISABLE_SIMD`, which makes Skia's
  raster pipeline run one pixel at a time. GUI.Forms' working tree already adds
  `-USKVX_DISABLE_SIMD`; the archive has to be rebuilt with it before these figures
  mean anything.
- Natively the table has no per-frame budget in `docs/PERFORMANCE.md`; a settled table
  schedules no frames, and that holds in the browser (the timer stops when nothing
  moves and the dealer is idle).
