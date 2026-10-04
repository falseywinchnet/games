# Performance and resource lifecycle

A settled view should reuse its last output. Animation, input, layout changes,
asynchronous results and timed game actions are reasons to do work; the passage
of another display interval alone is not.

## Current implementation

- Gems and Nature Cube update their registered board image with bounded damage.
  Ordinary animation does not invalidate the surrounding legend and command
  capsule. Gem effects that extend outside the board retain full repainting,
  including the final cleanup frame. Unchanged layout reuses the board raster;
  Cube tracing coalesces pointer bursts into the next animation frame.
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
