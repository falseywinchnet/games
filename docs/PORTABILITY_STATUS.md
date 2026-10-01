# Cross-platform port in progress

This branch is a source checkpoint, not a complete Windows application or a release.
The complete collection remains the target. Platform success must be validated
independently before publishing release artifacts.

Switchbox's replacement source is copied from `incoming/game-switchbox-01` at
commit `de23ab8` into `vendor/switchbox`. The incoming delivery is unchanged.
The collection edits select the new control at slot 7, retain enum identity 5,
and do not restore the removed implementation. Its dialogue has a separate
`assets/lines/switchbox` directory. Production and development saves use
`switchbox-v2.txt` and `switchbox-dev-v2.txt`; neither accesses `switchbox-v1.txt`.

The portable core builds and its rules, save-isolation and pointer-cancellation
tests pass on Windows with GCC. The CPU renderer has produced inspected scene
and holding-sequence images. These are not native application interaction tests.
The existing rules, storage, Sudoku, puzzle and Eggy simulation suites also pass.
Sudoku now uses pinned QuickJS rather than JavaScriptCore, with native/Node
generator parity checks. Environment artwork can be prepared as bounded RGBA
pixels, eliminating runtime Apple image decoding.

The audio adapters under `src` and the two vendor controls compile against the
new, separately developed GUI.Forms Audio component. The PCM implementation
passes complete decoding of all 243 prepared files and exact loop-seam checks
for the 17 cabinet music tracks. Its shared component has not yet been adopted
into the application's SDK or wired into the main application build.

## Remaining integration

- Finish and adopt GUI.Forms owned text masks with word wrapping, newlines,
  metrics and grayscale/monochrome profiles. Replace the remaining text adapters.
- Finish the shared window-scoped cursor visibility and placement capability.
  `vendor/switchbox/src/platform/cursor.hpp` currently declares the required
  seam but has no implementation. No successful no-op is substituted.
- Wire `sbx_ui`, the portable audio adapters and their shared dependencies into
  the main CMake application targets. The application still references legacy
  audio/text build inputs and is not expected to link on Windows yet.
- Complete compact audio decoding in GUI.Forms. The asset preparation tool can
  produce Ogg Vorbis quality 6 candidates as well as PCM fixtures; the current
  runtime loader still expects WAV. Vorbis is lossy and is not yet an admitted
  runtime decoder. Decoder ownership, bounds and licensing must be settled in
  the shared toolkit before adoption.
- Build and exercise the complete native application, including nested input,
  DPI changes, focus/capture loss, audio playback, saves and packaged launch.
  The new collection integration test has not run against a linked application.
- Establish repository release builds for each supported platform, preserving
  the complete test suite. No new platform release has been validated here.

`tools/prepare_portable_assets.py` requires Python with Pillow and a supplied
FFmpeg executable. Use separate output directories for `--audio-format pcm`
and `--audio-format vorbis`. Authoring sources remain unchanged. Generated
runtime assets are build products, not source replacements.
