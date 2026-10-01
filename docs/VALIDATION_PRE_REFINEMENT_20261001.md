# Validation record — 2026-10-01

## Automated checks

The final Release build passed **7/7 CTest targets**, 8.14 seconds on the M4. This is test execution time, not a gameplay frame-rate claim.

| Target | Evidence |
|---|---|
| `rules` | 320 seeded card deals; conservation and undo; 80 complete Hearts hands; first-trick/follow-suit restrictions, ace-high resolution, moon distribution, FreeCell space capacity, Spider stock restrictions, waste boundaries. |
| `ui_routing` | Real GUI.Forms Window dispatch, card drag/capture/release, stock, keyboard hint/placement, and completed-game score entry/New game flow. |
| `storage` | Cabinet state/preferences/top-score roundtrips; legacy version migration; corruption and unknown-version rejection without replacing the destination; one-time result submission. |
| `audio` | All 187 packaged AAC files decoded; finite samples; all 17 producer loop lengths; offline rendered repetition compared with the expected PCM stream at joins. |
| `sudoku` | Three canonical difficulty fixtures through native JavaScriptCore; deterministic generation; notes, mistake count outside undo, save/resume, and score submission latch. |
| `puzzles` | 24 seeds for each of eight games; invariants/save roundtrips; actual cube-path and tiling/filling witness replay; Untangle planar embedding; worst-case Switchbox deduction within 15 attempts; Gems match/power/cascade fixtures and sequences. All 1,679,616 Four Pegs secret/guess pairs checked against an independent evaluator. Atom tracer compared with a separately written oracle for all 560 three-atom fields × 16 ports. |
| `collection_ui` | Offset nested controls routed through a real Window: FreeCell dragging, Sudoku notes/wrong entry/undo, Gems invalid swap, Four Pegs draft persistence/submission, Atom ports/marking, Puzzle Solve placement, Sticks & Stones placement, Switchbox completion/name entry/New game. |

The native Sudoku fixture export was additionally checked by `tools/check_native_sudoku.mjs`: exact native/Node puzzle equality, independent Algorithm X uniqueness, difficulty band, and logical proof replay for easy/medium/hard (41/55/58 deductions). The supplied engine's broader Node benchmark report remains vendor evidence, not a native benchmark.

## Native macOS interaction and visual inspection

The application was inspected and operated through the native UI, including:

- All four card tables, legal FreeCell/Spider dragging, reveal, Hearts passing/computer play, keyboard H/Enter, options/help, backs, and exact table restoration after quit/reopen.
- Custom collection tiles, outlined material controls, game titles/emblems, recessed name fields, and coordinated game palettes.
- Sudoku light/dark themes, right-click note, wrong digit marking, undo removing the wrong digit while retaining the mistake, matching-number/row/column highlight, and puzzle restoration.
- Gems accepted swaps/cascades and complete invalid swap/return.
- Nature Cube reflective mesh, colored square endpoints, mouse-driven orbit, stable surface tracing, and a path drag.
- Untangle node dragging with attached edges following the moved node.
- Atom Probe boundary absorption feedback and marking a hypothesized atom.
- Four Pegs keyboard entry and aggregate feedback.
- Switchbox visible trial-and-exclusion play through a complete game, automatic terminal Top Scores dialog, native text entry of “Native test,” successful score recording, and New game.
- Puzzle Solve exact cell placement, rotation, joined shape contours, target image and piece tray; Sticks & Stones palette selection and exact center-cell placement.
- Minimum 1000×740 client size (1000×772 including the native title bar): collection tiles and Puzzle Solve board/target/piece tray remain separated and usable.

The nested-coordinate error found during this pass was corrected in all three board views: pointer positions are converted from window coordinates before hit testing. Offset-host tests now exercise that case directly.

The app is `build/games.app`, with a verified local ad-hoc signature. It includes runtime libraries, fonts, assets, the offline generator, and notices. No external publication, installation, distribution signature, notarization, Windows test, or Linux test is claimed.

## Performance and verification limits

Cards retain artwork/shadows and use bounded drag damage. Idle card/puzzle timers stop; cube animation stops after easing settles. Gems maintains a rendering timer for glisten/hover animation when full motion is enabled. Reduced motion suppresses those continuous effects. Sudoku generation runs outside the UI thread.

A CPU-only raster microbenchmark at 822×822 measured approximately 3.56 ms median / 3.61 ms p95 for the cube and 4.89 ms median / 5.00 ms p95 for Gems over 180 frames with the first 20 discarded. These earlier-pass figures exclude texture upload, native compositor, display, and input latency; they are not a sustained 60/120 Hz certification or final end-to-end benchmark.

Audio files, frame counts, and loop scheduling pass automated checks. A subjective listening review and comparison of the first decoded sample with every original WAV master have not been performed. Individual sound balance therefore remains an audition item.

Solvability witnesses establish the generated cube/tiling/hex/graph boards have valid solutions. They do not establish every such puzzle has a unique solution; Sticks & Stones deliberately accepts all valid fillings. Atom fields are selected for a unique full-probe signature. Card deals are not guaranteed winnable; hints suggest legal actions rather than proving a win. Microsoft deal-number compatibility is not claimed. The thirteen-game implementation does not include the separately planned Catching Thieves module.
