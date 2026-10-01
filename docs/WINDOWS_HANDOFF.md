# Windows copy and port handoff

The authoritative Games source is `/Users/joshuahkuttenkuler/solitaire`. There is no Git repository in this directory or its parents. No game implementation edits or builds are active in this thread. After writing this handoff and its fingerprints, this thread is holding source edits for the coordinator's copy. Windows edits belong to the Shadow sibling.

## Copy boundary

Stream the complete project tree except `build/` and OS metadata. Include root dotfiles (`.clang-format`, `.gitignore`), `CMakeLists.txt`, `README.md`, and all of `src`, `tests`, `tools`, `vendor`, `assets`, `incoming`, `docs`, and `evidence`. Preserve incoming originals and provenance even where runtime assets duplicate them. The editable Eggy integration is `vendor/eggy`, not the pristine `incoming/game-eggy-01/source`.

Preserve `build/games.app` separately as the last macOS reference artifact (approximately 113 MiB). Do not transplant the macOS build cache, object files, or libraries into a Windows build directory. The source+asset+incoming/evidence boundary is approximately 186 MiB; no additional full archive was made on the M4. `evidence/windows-handoff-20261001/source-sha256.json` records the boundary, excluding only itself, build outputs, `.git`, and `.DS_Store`.

## Roster and owner boundaries

Twelve playable games / nine collection entries: Solitaire, Spider, FreeCell, Hearts; Sudoku; Gems; Nature Cube; Untangle; Atom Probe; Four Pegs; Puzzle Solve; Eggy. Switchbox is reserved for the user's replacement: do not restore its removed logic or offer its old view. Its enum kind 5 and collection slot 7 remain reserved. Sticks & Stones is retired: enum kind 7 and original assets/saves remain, while its former collection slot 9 now hosts Eggy. Catching Thieves remains planned; Rendezvous Riders is excluded.

Preserve Puzzle Solve's diagonal two-color pieces, the cube's three visible/playable faces, first-press card drag behavior, shared master preferences, independent per-game saves, and the full File Manager house composition. Do not substitute simpler games or silently disable a game to obtain a green build.

## External dependency boundary

The current macOS build's `CMAKE_PREFIX_PATH` and `GUIForms_DIR` resolve to `/Users/joshuahkuttenkuler/Developer/Projects/gui-forms-investigation/paint-port-sdk`. Preserve this installed SDK's headers, CMake exports, fonts, notices, and macOS binaries as provenance. The Windows build requires a Windows-built `GUIForms::Application` implementation; copying the dylib is insufficient. The installed-SDK fingerprint is `installed-sdk-sha256.json` beside the source manifest.

The corresponding available toolkit checkout is `/Users/joshuahkuttenkuler/Developer/Projects/gui-forms`, currently clean at `9a4b156d62469fb8b90d77c967f061868c447590`. This identifies the checkout inspected now; it is NOT a claim that every installed SDK byte was built from exactly that commit. This Games task made no GUI.Forms source changes. The coordinator should capture the producer's actual build/install provenance separately.

SDK features exercised include `Window::queue_live_surface_presentation`, LiveSurface create/reconfigure/write/publish, timer intervals, visibility events, pointer capture and window/local coordinate conversion, BGRA image loading, rich Painter materials and clipping, and the native application host. Ship the provided font and third-party licenses. Relevant design references remain outside the project under `gui-forms-investigation/source/file_manager/frontend/planning/visual/` (`STYLE_FAMILY_ATLAS_004.md`, `DESIGN_DNA_VERDICTS_007.md`). They are authoring references, not runtime dependencies.

## Windows adapter seams

1. **CMake:** make Objective-C++/Apple frameworks/macOS bundle/signing-copy steps conditional. The portable language is C++20; CMake currently requires 3.25. Adapt GCC/Clang-only flags for the chosen Windows compiler, retaining assertions in tests. Build an executable-side assets/fonts/notices layout and preserve executable-relative lookup.
2. **Cabinet audio:** retain `music_play` and `sound_play` from `src/audio.hpp`. Implement the current AVFoundation behavior from `src/audio.mm` and the PCM/manifest loader in `src/audio_pcm.hpp` with a Windows backend. Preserve exact producer loop bounds, crossfades, stingers, and aliases. Windows must decode packaged AAC and WAV. `tests/audio_tests.mm` is itself an Apple adapter and requires an equivalent backend test; omitting it is not audio validation.
3. **Sudoku:** replace only the JavaScriptCore adapter in `src/sudoku_bridge.mm`. Keep `SudokuJob::operator()()`, background generation, the canonical bundled engine and calibration, deterministic seeds, and result validation. `assets/sudoku/engine.js` is the runtime adapter; `vendor/sudoku/src/engine.mjs` and its independent oracle are the authoring/test reference. An embedded engine can serve this seam without a mandatory external Node install. Node is used by `tools/check_native_sudoku.mjs` for independent verification.
4. **Environment image decode:** port `PuzzleRaster::load_environment` in `src/puzzle_image.mm` from AppKit/CoreGraphics to a Windows image decoder while preserving pixel format and sampling orientation. PNG artwork is already in assets.
5. **Eggy:** `world/sim/save/lines/raster/r3d/mesh/textures/ground/flora/critters/duck3d/scene` are portable core. Replace `vendor/eggy/src/text.mm` (the `text.hpp` mask/cache interface), `audio.mm` (the `audio.hpp` interface including `audio_cabinet` gating, clocks, asset path, backing scale), and `present.mm` (`NativePresenter`). If the Windows presenter initially returns false from `attach`, the existing LiveSurface fallback in `EggyView` runs; `text` and `audio` still need real implementations. Preserve immediate hide, input release, full audio gating, one-second hidden simulation ticks, and the first-Begin save guard. The macOS Core Animation workaround does not establish Windows performance, and the reported toolkit LiveSurface cost was not fixed here.
6. **Storage/tests:** both normal save roots currently use macOS's `Library/Application Support/Rainstar/Games`; preserve filenames/formats under a suitable Windows user-data root and keep `GAMES_STATE_DIR` for isolation. Replace test uses of `unistd.h`, `getpid`, `setenv`, and literal `/tmp/rainstar-puzzle-test` as appropriate. Keep old cube/solve v1 saves separate from their v2 replacements.

## Authoring resources and user data outside the source tree

- Original Eggy package, synthesis scripts, bundled synthesis engine, line drafts, runtime audio, screenshots, and validation are inside `incoming/game-eggy-01`. Its 217-file manifest was verified. The Neo original Eggy WAV masters are reported at `/Users/ultimussecundai/solitaire_sounds/eggy/audio_src/out`; acquire them from the Neo if 'everything' includes authoring masters. They are not required to run/build the delivered game.
- The original card/puzzle audio source project and remaining masters are on the Neo under `/Users/ultimussecundai/solitaire_sounds`. `incoming/audio-batch-01/work/src` contains delivered synthesis scripts; preserve both incoming batches. The coordinator should obtain the remaining Neo authoring source/masters rather than infer that runtime AAC is the complete authoring archive. Python/NumPy and ffmpeg are used for synthesis/transcoding.
- Card image generation is `tools/make_cards.py`, using Python/Pillow and locally installed Georgia regular/bold font files at macOS-specific paths. Finished card PNGs are present, so a Windows build does not need regeneration. Do not redistribute Apple's font files as a dependency; adapt authoring font lookup to an appropriately licensed local installation if regenerating.
- Sudoku's canonical source, calibration, independent oracle, and reports are in `vendor/sudoku`; no external generator checkout is needed for the shipped source boundary.
- Plan Paint's felt renderer source and MIT license are already vendored in `vendor/paint`; the full Rainstar Paint repo is a design/provenance reference rather than a build dependency.
- Final generated nature/Curator/Switchbox images used by the project are under `assets`. Unintegrated Switchbox experiments are not replacement-module content and must not be wired in by the port.
- Actual personal saves are separate at `/Users/joshuahkuttenkuler/Library/Application Support/Rainstar/Games/`. Preserve/migrate those separately if requested; do not commit them into the source repository or replace them with `evidence/.../native-state`, which contains isolated test play. Copy them consistently when the app is closed. This source freeze does not freeze a running app's save writes.

## Latest validation and reference artifact

No new test run was needed for this read-only inventory. The last full Release suite passed 8/8 targets in 9.26 seconds. Subsequent focused reruns passed card+collection (2/2, 3.59 seconds), then collection+Eggy (2/2, 2.88 seconds) after final fixes. See `evidence/refinement-20261001/test-receipt.json` and `docs/VALIDATION.md`.

All 236 audio files fully decoded; the original 17 cabinet loops passed exact offline PCM join checks. All 361 bundled assets matched source SHA-256. The last macOS app passed strict deep local ad-hoc signature verification. Its recorded executable SHA-256 is `85081a368272fb5a22b34f9ce30b61b61c4fe591d097dd314c40a7e49a39724e`. There is no Windows build, native Windows UI result, or Windows performance claim yet. The Shadow sibling owns those edits and checks.
