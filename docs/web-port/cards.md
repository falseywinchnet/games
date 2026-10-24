# Card games in the browser

Solitaire, Spider, FreeCell and Hearts share one table (`shared/cards/table.cpp`),
one press (`card_press.cpp`), one dealer (`dealer.cpp`) and the felt
(`shared/felt`). What holds for one holds for all four unless stated.

## Status

| Game | Opens | Play, undo, new game | Win / loss | Settings, Top scores | Saves across reload |
|---|---|---|---|---|---|
| Solitaire | yes | yes | won with hints in the browser | yes | yes |
| Spider | yes | yes (moves, stock rows) | not played to the end | yes | yes |
| FreeCell | yes | yes | not played to the end | yes | yes |
| Hearts | yes | yes (pass, tricks, computer players) | a hand played to the end | yes | yes (mid-hand) |

The browser pane was hidden during testing, so the page's `requestAnimationFrame`
was throttled; checks drove `_gf_web_tick` by hand. Nothing failed on the console
apart from the asset problem below.

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
after including it. `solver_tests` therefore links with a fixed 16 MB heap. The fix
(defer the loop to the end of the top-level directory) belongs to the lead.

## Frame cost

The window was 1024 x 768 at device pixel ratio 2 (3.1 M device pixels), Release
build, pane hidden, laptop shared with other workers. All figures are noisy.

- Full repaint of a settled card table: 520-1100 ms. Four Pegs on the same page: 7-9 ms;
  the shelf: about 450 ms.
- Moves and deals: 300-1000 ms a frame, so a deal shows only two or three frames.
- With the felt pattern, the lamp gradient, the slots and the cards skipped one at a
  time, almost all of the cost is the two full-window Skia shaders: the felt
  (a bilinear repeating image shader) and the lamp (a radial gradient). Blitting the
  printed cards is cheap.
- The Mini's Skia wasm archive was built with `SKVX_DISABLE_SIMD`, which makes Skia's
  raster pipeline run one pixel at a time. GUI.Forms' working tree already adds
  `-USKVX_DISABLE_SIMD`; the archive has to be rebuilt with it before these figures
  mean anything.
- Natively the table has no per-frame budget in `docs/PERFORMANCE.md`; a settled table
  schedules no frames, and that holds in the browser (the timer stops when nothing
  moves and the dealer is idle).
