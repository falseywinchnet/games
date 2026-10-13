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

The original actors continue while actively playing. Hidden views stop callbacks and audio; static panels stop once audio has settled. Reduced motion snaps the camera instead of smoothing movement, turns, ceiling rolls or portal flashes while retaining the actors and rules. Minimum-size rendering uses a logical canvas of at least 320 × 200. Hosted music/sound controls belong to the suite.

2026-10-12: the renderer moved onto the shared render engine. `soft3d.*` keeps its
interface (every caller in `world.cpp`, `session.cpp`, `idle.cpp` and the preview is
untouched and still borrowed) but its triangles go through `shared/render`'s
camera-space scan (`r3d_scan.hpp`: near-plane clipping, 8-bit subpixel fixed-point
edges with the top-left rule, perspective-correct weights), with the maze's texel, light
and blend as a pixel policy in `soft3d.cpp`. The two files left the `borrowed` list.
Frames compared with the previous renderer (`maze_preview`, levels 1, 5, 9, 14, 22, 30,
with yaw, roll, blackout and sizes to 800 x 500): at most 0.5 % of pixels differ,
all along the seams between walls, floor and ceiling (the exact fill rule decides a
pixel centre on an edge once; the old per-pixel test could cover it from both sides)
and a few far texel boundaries; views at an arbitrary yaw differ in under 0.01 %.
`maze_idle` still matches demand rendering to full rendering pixel for pixel. The
frame cost about halved (366 x 240: 2.0 to 1.0 ms a frame on an A18 Pro; 800 x 500
close to a wall: 7.3 to 4.1 ms). The window end moved from the game's own `LiveSurface`
and enlargement loop onto the engine's `PixelSurface` and `blit_text`: the frame is
written in the window's own byte order, only the rows and texts that changed are
enlarged again, and an unchanged frame publishes nothing.

The frame is drawn at twice the size the game shows and averaged down (2 x 2
supersampling, `MazeView::resolve_frame`): the owner chose this on 2026-10-12 from five
labelled candidates (as before; double resolution shown whole; double resolution averaged
down; bilinear enlargement; both), for clean edges with the game's pixel size kept. It
costs four times the renderer's pixel work, which the shared scan now affords (a 366 x 240
view draws 732 x 480: about 5 to 9 ms a frame on an A18 Pro with the new surfaces), and the
maze still draws only when something changed.

The same day the sibling session's surface work (choosable floors, walls and ceilings in
`art.*`, the snail's moving repaints in `paints.*`, the `kRain` and `kSlide` texture
flags, fresh paint rising, the four-frame snail, Settings for the three surfaces) was
merged in from its uncommitted tree; the two flags live in the pixel policy. `borrowed`
now lists only the files still byte-identical to the delivered package (`cast.*`,
`maze.cpp`, `rewards.*`); the edited ones are the gate's to style.

`mz_step_0` is a dynamically completed filename prefix; files `mz_step_01` through `mz_step_03` exist. Unused generic original UI cues were renamed with `mz_ui_` to avoid collisions. No maze feature or reward was removed.

2026-10-13: endless mazes and a passive player. The briefing, the win dialog and the trophy
shelf are gone: a maze names its reward in the speech box as it begins, the reward room is
admired for five seconds, and the next maze starts. Rewards come in a shuffled order per run.
The save format is unchanged (its trophy bits are still written, no longer shown). Left
alone, the maze walks itself (`wanderer.*`): this is deliberate ambient motion, so
`settles_when_untouched` is false. It walks only while the window is active, not under
reduced motion, and not while hidden; after the player takes over, the only wake is one
twenty seconds after their last input, to hand the maze back. `maze_view_contract` checks
that the opening maze settles under reduced motion, that the passive player walks in an
active window, and that an inactive window stops it. `maze_rules` walks 120 mazes with it:
every reward is found, at about 2.7 times the shortest way.
