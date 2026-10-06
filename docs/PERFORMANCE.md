# Performance and resource lifecycle

A settled view should reuse its last output. Animation, input, layout changes,
asynchronous results and timed game actions are reasons to do work; the passage
of another display interval alone is not.

## Current implementation

- The Windows toolkit reuses sampled gradients and shadow coverage while
  compositing against the current background and applying the current clip.
  Gradient pixels remain bounded to 16 MiB per painter; exact shadow samples
  add at most 4 MiB. The gradient entry limit accommodates Untangle's repeated
  brush set without cyclic eviction. Native tests compare cached and uncached
  pixels, including alpha, fractional translation, clipping and scale changes.
  This optimization is specific to the Windows DIB renderer.
- Gems and Untangle publish opaque, device-sized `LiveSurface` frames through
  direct presentation. A GUI.Forms `PaintFramebuffer` reuses the native Windows
  DIB or Skia painter, including its fonts, filtering, blending and shadows.
  Stationary scenery is cached as rendered pixels. Animation restores its old
  and new bounds, draws the current objects and foreground, and publishes a
  complete immutable frame. Untangle also retains crossing geometry until a
  peg moves. Resize and DPI changes rebuild the buffers. Expose and help closure
  draw the latest surface; hidden games stop publishing. Renderer-free tests
  retain the ordinary command path as an explicit fallback.
- Gems updates its private image registry in place while animating; only a
  resize reallocates that raster. Transparent gems are composed over the cached
  board before the opaque direct publication. Effects outside the board and
  their final cleanup preserve broader damage. Nature Cube still uses ordinary
  image drawing and coalesces tracing into its next animation frame.
- Sudoku polls its generation worker without repainting the static waiting
  screen. Completion still publishes the new puzzle and status; celebrations
  retain their animation and final cleanup.
- Hearts schedules the next computer play at its existing deadline when cards
  are stationary, instead of checking it every eight milliseconds. Activity
  wakes animation promptly; the authored pauses between plays are unchanged.
- The collection creates a game on its first visit. The shelf does not construct
  all games, decode their artwork or allocate their framebuffers. Visited games
  retain their state for an exact return to the current position.
- Layout visits the visible game. Hidden vendor game timers stop, and resume
  with a fresh time origin. Eggy's intentional background climbing remains a
  low-frequency simulation with no hidden rendering.
- Shelf animation stops when box lifts and the launch transition finish. The
  shell stops polling on the shelf once pending text and audio loads complete.
  A newly requested text sprite wakes preparation again. Open games still check
  their command model at 5 Hz; this does not invalidate unchanged controls.
  Capsule animation and pending preparations use the faster cadence they need.
- Koi-Koi publishes only while its pixels change. Cards settle at their exact
  target after the remaining displacement falls below one hundredth of a device
  pixel. Hints, matching-card glows, banners, computer turns, deferred saves and
  music fades retain their timing. Once these finish, its timer stops. Input,
  commands, preferences, resize and display-scale changes wake it again.
- Music playback is owned by the audio engine. Steady music does not require
  Koi-Koi's UI timer. Loading and gain transitions do.
- Eggy's asynchronous text preparation freezes HUD values and projected speech
  coordinates, not a copy of its simulation and mountain caches. Retained
  repaints draw the last published surface so exposure does not require waiting
  for another animation frame.
- Runtime card data contains one prepared representation. Original hanafuda PNGs
  remain in the repository; installers omit these duplicate source images. The
  card manifest records both source and prepared fingerprints. No pixel size,
  artwork quality, audio quality or game content is reduced by this change.

## Reproducing measurements

The native executable accepts `--profile-idle -1` for the shelf or
`--profile-idle N` for persisted game entry `N` (0 through 17). It waits five
seconds, emits `PROFILE_BEGIN`, resets window activity counters, waits ten more
seconds, emits `PROFILE_END`, then closes normally one second later with native host
metrics. The delay keeps teardown outside the external process-counter sample.
Always set `GAMES_STATE_DIR` to an isolated directory for profiling.

`tools/profile_windows.py` collects Windows process CPU time, working set and
private memory with standard-library process counters. It creates isolated save
directories, mutes master audio, and can copy the same saved game position into
each run. For example, with paths appropriate to the build:

```text
python tools/profile_windows.py <games.exe> --seed <cabinet-v1.txt> \
  --assets <prepared-assets> --game-save <koikoi-v1.txt> \
  --entries -1 0 13 12 --output astra/profile.json
```

Keep the window uncovered and leave the pointer still. Do not compile, capture
the desktop or run another benchmark during the sampling interval. Compare the
same window dimensions, display scale, preferences and saved position. CPU is
reported as a percentage of one logical core. Timed game events and decorative
animation are active work, so a new deal is not necessarily an idle benchmark.
Repeat measurements on a shared or virtual host; counters for unchanged paints,
layouts, callbacks and surface publications explain more than one CPU reading.

`tools/profile_presentation.py` runs the same native sampling interval on every
platform and saves frame counts, total/average presentation cost and the slowest
presentation. It uses the metrics snapshot taken at `PROFILE_END`, before the
one-second process-counter grace period and teardown. These are presentation
measurements, not process CPU percentages or end-to-end input latency. The macOS
application workflow records Solitaire, Gems and Untangle in its check artifact;
timing values are diagnostic and have no machine-speed pass threshold. Snapshot
the isolated saves before comparing builds, since timed game events can change
them. Keep motion preferences and audio settings identical between comparisons.

Machine-specific reports and screenshots belong in ignored `astra/` directories.
Native `vendor_game_frame_tests` verify that hidden games have no timer callbacks
or publications, that they resume, and that static Koi-Koi stops scheduling.
The collection test verifies lazy creation and still visits the complete roster.
Audio policy tests cover steady playback without UI polling.

## Remaining work

### Direct puzzle framebuffers

The native `puzzle_native_framebuffers` test compares Gems and Untangle's
published pixels against a full redraw using the same native painter. It covers
100%, 125% and 200% scale, narrow/wide layout, partial hover restoration, changed
yarn geometry, immutable read leases across pool rotation, and hidden lifecycle.
This test runs under the actual native application host on each build platform.
Windows comparisons are exact. Skia comparisons permit only one level of
8-bit channel rounding at no more than 0.01% of pixels per frame. Clipped blend
spans produced one or two such differences on native Linux x64 and arm64, and
21 on macOS. Larger channel errors or a larger affected area still fail;
every comparison reports its rounding count, resolution, scale and hover state.

Direct presentation removes control-tree raster replay from ordinary animation.
It does not eliminate gem geometry generation, filtered composition, animated
object drawing, or the final surface copy. Measure process CPU and direct host
counters as well as retained paint counters. A zero retained-paint count is
expected during direct animation and must not be described as zero CPU.

### Rendering and presentation audit

The following paths describe the current application. The native offscreen
adapter is pinned at GUI.Forms revision `2ac5dcd84b8f5b0e241691e31880e36684e0cfcf`.
Direct presentation means a registered `LiveSurface` submitted through
`Window::queue_live_surface_presentation`; merely owning a CPU pixel buffer
does not establish that path. On the pinned Windows host, a surface matching
its destination's physical dimensions uses clipped row copies. This is CPU
presentation, not GPU rendering.

| Games | Presentation | Work while animated | Existing reuse and next boundary |
| --- | --- | --- | --- |
| Gems | Direct live surface | Draws the animated gem raster and effects into an opaque native framebuffer | Cached board pixels are restored within damage; sampling and alpha composition happen offscreen. Gem raster generation and filtering remain significant costs. |
| Nature Cube | Registered BGRA image, sampled by `draw_image` | Renders when its orientation or other raster state changes | Reuses the settled raster and coalesces pointer motion. Rotation still incurs image sampling. |
| Untangle | Direct live surface | Restores damaged scenery pixels and draws cat, knots, pegs and effects | Stationary yarn is cached as pixels; moving a peg rebuilds the scenery and crossing geometry. |
| Solitaire, Spider, FreeCell, Hearts | Drawing commands and card images | Moving cards invalidate their old and new bounds; celebrations are broader | Settled animation timers stop; Hearts wakes for the next computer play. A damaged table still records its combined drawing commands and samples overlapping card images. |
| Sudoku, Solve | Drawing commands | Input feedback and celebrations | Event-driven while settled. These do not need continuous framebuffer publication. |
| Eggy | Direct live surface | Renders the moving scene and publishes a complete surface | Existing sky reuse, small scene raster and duplicate-row expansion; hidden rendering is suppressed. Camera motion changes much of the view. |
| Switchbox | Direct live surface | Redraws room, box, switches, actor and effects; publishes a complete surface | Static room/box rendering is a candidate for color/depth reuse, with state and camera invalidation. |
| Four Pegs | Direct live surface | Redraws room, actor, desk and effects; publishes a complete surface | Room reuse must account for camera shake and changing doom lighting. |
| Atom Probe | Direct live surface | Redraws chamber, console, atoms, fog, glass and bloom; publishes a complete surface | Transparent passes and bloom spread changes beyond object bounds; cache only stages with explicit dependencies. |
| Koi-Koi | Direct live surface | Composes and publishes the complete surface while cards, highlights or banners change | Stops its timer after visual, save and audio-transition work settles. Active animation could restore and repaint old/new card bounds. |
| Parrots | Direct live surface | Redraws room, table, props and birds; publishes a complete surface | Room/table reuse must account for lamp state, bird placement and evidence props. |
| Liar's Dice | Direct live surface | Redraws room, crew, table and effects; publishes a complete surface | Room reuse must account for tension, lighting and other animated room details. |
| Pen the Sheep | Direct live surface | Restores cached land, draws animated land, patches and sheep; publishes a complete surface | Static land already has color/depth reuse. Remaining work includes patch geometry, animated scenery, composition and publication. |
| Rock Stack | Direct live surface | Restores cached scenery, redraws shadows, rocks, crane and brook; publishes a complete surface | Restores only the moving casters' previous shadow-map bounds. The deferred shadow pass composes camera/light transforms per band and uses a direct interior 2×2 filtered lookup. Quiet rendering remains paced at about 10 Hz; objects and brook animation still cost CPU. |

Every production vendor publisher currently submits the complete surface.
The pinned toolkit's direct presentation drain also derives its clip from the
visible control, rather than the published frame's damage rectangle. Passing
a smaller rectangle to `publish` alone therefore does not establish partial
native presentation. Toolkit support must preserve correctness when generations
are skipped, buffers rotate, the window is exposed, or overlays move.

The Windows live-surface copy path also does not provide the same alpha blending
and filtering as ordinary image drawing. Gems now uses a fully composed opaque
board produced by the same native offscreen painter. Nature Cube remains on the
ordinary image path. Any future migration must preserve its composition.

Use native presentation counters together with process CPU. A low direct-copy
time can coexist with expensive software scene rendering before publication.
Conversely, a settled-game sample cannot establish the cost of dragging, dealing,
rotating or celebrating; measure those interactions separately. Platform source
inspection does not replace a native measurement on that platform.

Visited games currently retain their render caches as well as their game state.
Reducing those retained caches needs explicit lifecycle handling so returning to
a game preserves transient state, speech, positions and input behavior. Animated
3D scenes still need per-game profiling and static-background reuse where their
renderers permit it. The shared command model could expose change notifications
instead of polling.

The pinned Windows GUI.Forms host also keeps its presentation clock armed while
a live surface is registered, even when it has no new generation. That residual
wake policy belongs in the toolkit; see `TOOLKIT_REQUESTS.md`. These changes do
not claim that the collection has reached its minimum possible CPU or memory use.

Rock Stack's shadow regression compares partial restoration with a complete
map rebuild as a caster moves, crosses map edges and disappears. It requires
identical output. Run `rockstack_shadow_tests --bench` for the restore, cast and
deferred-filter timing at 960×600; this is a shadow-stage benchmark, not a whole
game CPU measurement. The optimization retains the 1024×1024 map and 2×2 filter,
adds no per-pixel cache and does not reduce animation cadence.
