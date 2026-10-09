# Maze 95 integration

## VERIFIED

- Built the native twenty-game application on the M4 Mini using the reviewed GUI.Forms pin, LLVM 22.1.8 and the Skia CPU renderer.
- `maze_rules` passes its procedural-generation, route-solving and session checks, including 120 generated maze proofs and 36 simulated sessions.
- `maze_view_contract` passes first-frame, command/help, 150% scale, 600 × 370 and 600 × 320 layout, hidden callback silence, immediate level persistence and production-script refusal checks. Level 14 exercises the later procedural feature set.
- Real AppKit standalone and hosted scripts completed with zero frame callback and native callback faults. The standalone script selects later levels and runs the original route-following development actions; the hosted script switches games and shared help.
- Inspected native renderer frames for the briefing, play and help at large and minimum sizes, plus level 14 and the paginated trophy shelf at 600 × 320. PNGs are in `screens/`, including the hosted capsule and game at 600 × 420.
- Portable text and audio compile against the pinned headers. Dynamic source/runtime inventory verification and native loop decoding pass with quality-6 libvorbis assets.
- Only byte-identical original files are listed as `borrowed`; the gate checks all edited C++ files. `SOURCE_PROVENANCE.json` records the original package fingerprints.
- Demand rendering reuses the last frame when the camera and visible world are stationary. Conservative projected bounds and the existing depth buffer retain animation for visible rewards, actors, bulbs, flip stones and animated paint. Actors still simulate; quiet sessions sleep to their next encounter or speech deadline. Input shortens that wait, and panel time does not advance a resumed game.
- `maze_idle` compares 2,700 frames across levels 1, 5 and 14 against unconditional rendering, pixel for pixel, and checks event deadlines and fresh visitor speech. `maze_view_contract` also verifies a live corridor has no callbacks or publications while quiet and wakes on input.

Run `games --game maze --standalone --dev --script games/maze/tests/native.script`. The root `AGENTS.md` contains the supported native development build recipe.

## NOT VERIFIED

The automated native scripts do not certify human listening, subjective controls or long play sessions. The local development platform is macOS arm64; Windows x64, Linux x64/arm64 and release macOS packaging are covered by the separate publication workflow. Local success alone does not establish those platform results.

## DECISIONS

Preserved the full user-supplied `maze` game: software 3D renderer, textures, deterministic generated mazes, solver, pads, portals, flips, elevators, rolling marble, paint snail, encounters, cast, all 46 rewards and audio. Replaced the AppKit-only presenter/text/audio bridge with GUI.Forms LiveSurface, TextMask and SceneAudio. Permanent module ID is 19; its cover, help, factory and build registration are owned by this folder.

The original save format records the current maze seed/level, trophies and totals; reopening starts that maze at its entrance. It does not save a mid-corridor camera position. The port retains those semantics and explains them in player help. Within a session, visiting panels after a win cannot award its statistics twice.

The original actors continue while actively playing. Hidden views stop callbacks and audio; static panels stop once audio has settled. Reduced motion snaps the camera instead of smoothing movement, turns, ceiling rolls or portal flashes while retaining the actors and rules. Minimum-size rendering uses a logical canvas of at least 320 × 200. The trophy shelf pages when needed. Hosted music/sound controls belong to the suite.

`mz_step_0` is a dynamically completed filename prefix; files `mz_step_01` through `mz_step_03` exist. Unused generic original UI cues were renamed with `mz_ui_` to avoid collisions. No maze feature or reward was removed.
