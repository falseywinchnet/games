# Current cross-platform validation

The [complete application run](https://github.com/falseywinchnet/games/actions/runs/36953930003) passes all 22 tests independently on Windows x64, macOS arm64, Linux x64 and Linux arm64. Windows verifies the portable application, per-user installer, installed application and uninstaller. macOS verifies the bundled application and the PKG-installed copy at `/Applications/Games.app`, including required bundled fonts. Both Linux platforms install the generated DEB and launch its real X11 window under Xvfb. Every native smoke requires a presented collection frame and scheduled clean shutdown.

The released dependency closure includes the required Windows and macOS runtimes; Linux's DEB declares its system dependencies. Package manifests record the Games commit and toolkit revision. Runtime data is verified against the complete source inventory before packaging. The touched startup and collection-layout C++ passes the house spelling checker; this does not certify unrelated legacy source style.

The complete Windows Release application passes all 22 CTest cases, including replacement game rules and saves, cabinet routing, offline QuickJS Sudoku, all 298 compact audio files, Four Pegs bar transitions, and complete frames from all four vendor views. The packaged application launches and presents its collection with isolated saves and only Windows system directories in PATH. Its scheduled native close returns success.

The [native text run](https://github.com/falseywinchnet/games/actions/runs/36950462158) passes all seven consumer and complete-frame tests on Windows x64, macOS arm64, Linux x64 and Linux arm64. Actual game/help frames at 100% and 150% have been inspected. Software-frame checks do not measure native presentation latency.

The [application workflow](https://github.com/falseywinchnet/games/actions/workflows/applications.yml) builds, tests, packages and launches the full application independently on those platforms. Tagged releases require all four application jobs to pass. Launch smoke establishes resource loading, a presented collection frame and clean shutdown; it is not a full manual gameplay review or listening test. A Windows Four Pegs device-audio exercise was heard clearly by the tester.

The following record describes the earlier macOS implementation. Its JavaScriptCore, Core Animation, audio inventory and roster claims are historical and do not certify the replacement application.

---

# Validation record — October 1 refinement

The combined Release build passed **8/8 CTest targets in 9.26 seconds**. Focused card/collection tests were rerun after the final category-selection adjustment; collection and Eggy simulation checks were rerun after gating the initial Eggy save on Begin. The earlier 13-game validation is preserved separately in `VALIDATION_PRE_REFINEMENT_20261001.md`; it describes the prior implementation, including modules no longer offered.

| Target | Current evidence |
|---|---|
| `rules` | Seeded card deals, conservation, undo, Hearts play, legal run capacity, stock and waste restrictions. |
| `ui_routing` | Actual GUI.Forms pointer dispatch and capture; a newly pressed card drags immediately, including when it is a legal destination for the previous selection; keyboard hint/placement; stock; terminal scores/New game. |
| `storage` | Cabinet preferences, scores, legacy migration, corruption rejection, and submission latch. |
| `audio` | Full finite-sample native decoding of **236 files** (231 AAC + 5 WAV), including all 49 Eggy files. Existing 17 cabinet loop lengths and offline-rendered loop joins remain exact. Eggy loop-join equivalence is not added to that 17-loop claim. |
| `sudoku` | Three canonical native JavaScriptCore fixtures, deterministic generation, notes, mistakes, undo, persistence, score latch. |
| `puzzles` | Seeded save roundtrips and construction witnesses; cube traces restricted to three faces; seven-piece diagonal reconstruction; Gems powers/cascades; independent Four Pegs evaluation for all 1,679,616 code/guess pairs; Atom oracle across 560 fields × 16 entries. |
| `collection_ui` | Offset-host card/Sudoku/puzzle input; peg draft persistence; reconstruction placement. Cube raster picking contains exactly all 48 cells on faces 0, 3, 4 at nine tilt combinations. Selection/open are separate; Eggy opens, hides, saves into the isolated directory; category filters hide/show the right entries. |
| `eggy_sim` | Delivered world-determinism and autopilot-progress fixtures, linked against the same core as the cabinet. |

## Native verification

The app was exercised with an isolated `GAMES_STATE_DIR` under `evidence/refinement-20261001/native-state/`. Original user saves were not used for test moves. The incoming Eggy package was independently rechecked: **217/217 hashes match**, with the receipt in the evidence directory.

Native inspection covered the complete pearl/white collection composition, single selection and double-click opening, Four Pegs palette/check/history, diagonal piece dragging and target rendering, the cube's three-face tilt range, and Eggy title/start/base camp/Help/return to collection. Native first-press FreeCell dragging, a staggered new deal, a Solitaire tableau reveal, Eggy saved-climb resume, and the corrected Eggy Help panel were also checked. The collection and Puzzle Solve fit the minimum 1000×740 client size. Final screenshots are in the refinement evidence directory.

Nature Cube and Puzzle Solve use new version-two paths; prior version-one files are preserved, and player names/scores are copied when starting the replacement board. Switchbox's previous logic is removed and its slot remains reserved for the owner. Sticks & Stones is not constructed or shown.

The final bundle passed strict deep ad-hoc signature verification; **361/361 bundled asset files** match the source assets by SHA-256. The final binary hash and bundle receipt are in `evidence/refinement-20261001/final-bundle-verification.json`.

## Limits

Card movement uses quintic easing, staggered new deals, and a 340 ms face turn with cosine-width projection. Native inspection and routing tests do not certify sustained 60/120 Hz presentation. Earlier raster microbenchmarks exclude upload, composition, display, and input latency and are not measurements of this revised build.

Eggy retains its authored Core Animation workaround; no GUI.Forms LiveSurface performance fix is claimed. No real multi-year run, complete listening review, public distribution, notarization, or Windows/Linux verification was performed. Generated-puzzle witnesses establish solvability, not uniqueness; card deals are not guaranteed winnable. Catching Thieves remains a planned module.
