# GUI.Forms requests from the PlaySuite rework

AGENTS.md asks for reusable capabilities to become GUI.Forms enhancements, coordinated with the toolkit owner. The PlaySuite shell needed several things the pinned toolkit (`7b260cf`) does not offer. Games carries small local versions so the application works today. Each entry below names the local stand-in that a toolkit feature would replace. None of these changes were made to the shared toolkit checkout.

## Proposed toolkit features

1. **Application display faces in `Painter` text.** `FontRole` has only `control`, `content` and `monospace`, mapped by each host to bundled faces. Games paints its sign and box titles in Barlow Condensed and Libre Baskerville through `src/text_sprites.*`. That code shapes masks with the text-mask service, converts the coverage to premultiplied images, and draws placeholder text in the host face until the masks are ready. What would replace it: a way to register an application font family, or a `display` role, usable from `draw_text_utf8` and `measure_text_utf8`.

2. **Floating command bar control.** `src/capsule.*` is a generic overlay pill. It shows primary commands when folded, opens on hover with an eased width and height, wraps commands onto extra rows when narrow, can be pinned open, and leaves a shadow margin that is not hit-tested. A command model (id, label, enabled, checked, primary) drives it. Other applications could use it for full-window canvases.

3. **Device scale on `Painter`.** Polygon filling (`paint_polygon` in `src/presentation.cpp`) steps one device row at a time, so translucent fills never overlap. The shell has to publish the window scale through `set_polygon_scale` because `Painter` does not expose it.

4. **Anti-aliased polygon and path fill on `Painter`.** Only `gui_drawing::GraphicsRecorder` has paths, and it is not drawable through `Painter`. Games fills polygons with device-row spans and blends the partly covered pixel at each span end. Tiled shapes need a hard-edged mode, or seams appear between them.

5. **Scaled presentation of a small live surface, and no clear under an opaque one.**
   Measured with Stillwater (`shared/ambient`) on the macOS Skia CPU host at
   1060 x 680 points, scale 1, 20 frames a second: of 18 % of one core, about 6 %
   is host work per presented frame (Skia clearing the window under the opaque
   surface with `rect_memset32`, retained paint of the surface, Core Animation
   submission), and the game spends a further share enlarging its 530 x 340 scene
   to device pixels (`ambient::present_nearest`) only for the host to copy it again.
   Both grow with device pixels, so a Retina window costs about four times as much
   while the scene's own work stays the same. Two features would remove most of
   it: (a) a live surface that may be smaller than its destination, presented
   with nearest-pixel (or optionally linear) scaling by the host or compositor,
   with a way to blend crisp device-resolution overlay pixels (or a second,
   sparse overlay surface) for text; (b) skipping the background clear beneath a
   control that declares an opaque live surface covering its bounds. The kit's
   view contract currently requires device-sized frames, so (a) also needs a
   contract revision.

6. **Decode a PNG into CPU pixels.** `Window::load_png` validates a PNG and hands
   it to the host renderer, but nothing public returns its pixels, and the
   Windows host decodes through WIC while Skia hosts decode internally. Koi-Koi
   keeps its own CPU raster (area-averaged card reduction, card finish), so it
   decodes its compressed deck itself with the ambient engine's zlib decoder
   (`games/koikoi/src/platform/image.cpp`, 8-bit RGBA only). What would replace
   it: a portable `decode_png(bytes) -> premultiplied BGRA` in GUI.Forms, with
   the registry's limits, usable off the UI thread.

## Host behavior found on Windows

**Idle live-surface clock and registration lifecycle.** In pinned `7b260cf`,
   the Windows host arms the periodic presentation clock
   whenever `Window::has_live_surface_presentations()` is true. Registrations
   retain the surface until the control is detached; the public API offers no
   suspend/unregister token. Consequently a settled or hidden producer can stop
   publishing but still leave host wakeups and framebuffer storage behind.
   A publication-triggered wake, plus an explicit lifetime/suspension mechanism
   that preserves the latest frame for exposure, would let Games retire idle
   presentation work and hidden buffers without detaching game state. Games
   currently stops producer timers and avoids repeated publication; it does not
   modify this shared host policy.

1. **Retained repaints hide directly presented live views until their next frame.** A full-window repaint (an expose, or `PrintWindow` during automated captures) redraws a direct-presented view from its retained `on_paint`, which is the game's fallback fill. The live pixels return only when the game publishes a new generation, so a game that publishes rarely can look blank after an expose. Automated screenshots must therefore copy from the screen. A fix would be for the retained repaint to composite each registered surface's latest published frame. In the transactional DIB mode, `present_live_surface_updates` also skips presenting while other damage is pending, which makes needless invalidation costly. The PlaySuite shell avoids it: for example, button setters return early when nothing changed. Each integrated vendor view now also records `draw_live_surface` during ordinary painting so an expose or the help document can restore the latest complete frame without waiting for another publication.

2. **Ordinary controls over a live view.** Only `PaintPlane::overlay` controls are subtracted from direct presentation. The repaint of an ordinary control that overlaps a live view overwrites live pixels until the next frame. The PlaySuite capsule therefore stays within the rail above the four live-surface games.

3. **Letter spacing drops characters.** With `FontSpec::letter_spacing` above zero, the Win32 DIB text path dropped or overlapped spaces, periods and digits. Examples: "Welcome back. Your…" rendered as "Welcome backYour", and "MISTAKES 0" overlapped. Games now uses letter spacing only on static uppercase captions.

## Animated rendering boundaries in the current pin

The Windows adapter backport at `c3b9d10d0ed708da2527fcb2697b29b8b2cd9007`
resolves input starvation during continuous rendering. Both native host loops
alternate bounded input-first and ordinary message turns before presentation.
The input turn includes posted keyboard/mouse messages and translated characters,
which are not covered by `PM_QS_INPUT` alone. A native queue fixture checks their
ordering through a posted render backlog, ordinary FIFO work and quit handling.

The same backport includes the complete `BeginPaint` device-pixel update bounds
when logical damage already exists. This prevents fractional damage edges from
leaving stale lines on an opaque help document over animated content, and keeps
native exposure damage from being omitted. The public toolkit interface is
unchanged. These fixes do not implement retained pixels or faster filtered image
composition; those requests below remain open.

The following were verified in pinned revision
`3f75e379213de972f78729a591594e70fe65a585` while auditing frequent updates
across the eighteen-game collection. The per-game paths are recorded in
`PERFORMANCE.md`.

1. **Damage propagation for direct live surfaces.** Producers can publish a
   damage rectangle, but `Window::take_live_surface_presentations` uses the
   control's visible bounds for changed generations. Carrying conservative
   damage through to native presentation needs to cover skipped generations,
   rotating buffer contents, scale changes, exposure and overlay removal.
   Simply applying the newest frame's rectangle would lose updates when a
   consumer skips an intermediate frame.

2. **Fast premultiplied image/surface composition.** Gems and Nature Cube use
   transparent, supersampled CPU rasters with ordinary image drawing. The
   Windows direct surface path uses copy semantics and cannot preserve that
   composition by substitution. A shared fast sampling/blending path, or an
   offscreen `Painter` that can produce an opaque composed board, would avoid
   application copies of toolkit rendering code. Preserve filtering, alpha,
   clipping, fractional translation and display scale.

3. **Retained pixels or command bounds during replay.** Separating static and
   animated controls retains recording, but intersecting chunks still replay
   every command through the painter. Untangle's stationary yarn contains many
   short line segments; even a smaller cat damage rectangle incurs their replay
   overhead. Reusable pixel retention or conservative command culling must
   preserve painter save/restore, transforms, clips, stroke extents and shadows.

4. **Comparable presentation metrics.** Window `frames_presented` and its
   duration counter cover retained painting; direct surface copies have separate
   host `live_presentations` counters. The current application sampling boundary
   resets only the former. Exposing/resetting both at the same boundary would
   permit comparable native measurements without counting startup and teardown
   in one path. Zero retained paints does not mean a live-surface game is idle.

## Native offscreen adapter

Games now pins `d58f530ad5ceaa8f4ab1e5afdc12027aff90d16d`, adding
`Window::create_framebuffer(Size, double)`, the native painter factory, and
`PaintFramebuffer`. Hosts explicitly register a borrowed framebuffer painter and
clear it before teardown; consumers do not discover it through cross-library RTTI.
Native DIB and
Skia implementations preserve their renderer's drawing and registered fonts.
The UI-thread-owned target retains writable premultiplied pixels, reports its
BGRA/RGBA order and stride explicitly, and clips each begin/end pass. Unsupported
recording/headless painters return no target. Requested dimensions are bounded.

Gems and Untangle use this facility to cache scenery pixels and publish complete
opaque direct surfaces. This resolves their need for offscreen composition and
static pixel retention without copying platform rendering code into Games.
Faster image filtering and propagation of bounded damage through the native
direct drain remain independent improvements. The adapter was developed in an
isolated toolkit branch and raised with the toolkit owner for upstream adoption.

## Live audio sources

Done: GUI.Forms `63e7128` adds `AudioGenerator`, `AudioEngine::generator` and the
feature macro `GUI_FORMS_AUDIO_GENERATOR` (falseywinchnet/file_manager#54). Games
uses it behind that macro: `PcmPlayer::generate`, `ambient::SoundDesk::live` and
`live_music`, Mowings mower, garden and band, and Stillwaters tank and shanty.
Builds against an older toolkit fall back to rendered clips.

## macOS live-surface painting (found with Stillwater, 2026-10-06)

Largely done: GUI.Forms #52 added `LiveSurfaceDescription::opaque` (no retained paint
under an opaque surface) and #55 (`2ac5dcd`) native-order surfaces
(`native_live_surface_pixel_format()`, RGBA on macOS and Linux, BGRA on Windows) with
pixel-exact copies at 1:1. The scene view and the puzzle views write native order and
declare themselves opaque. Stillwater, 1060 x 618, per frame on the M4: retained paint
1.84 -> 0.56 ms, live draw 1.55 -> 1.35 ms. The full-window CoreGraphics hand-off
remains. The original finding follows.

On the M4 (pinned Skia CPU adapter), a `LiveSurface` drawn through
`Painter::draw_live_surface` is not directly presented: every published frame
becomes a retained paint pass over the control's whole area. A 10-second
`sample` of Stillwater at 1060 x 618 (scale 1, 12 frames a second) puts about 20 %
of the process's active samples in that paint: `neon::rect_memset32` (the
destination is cleared before an opaque surface covers it), `srcover` blending
of a surface the producer marks opaque, `swap_rb` (a BGRA/RGBA conversion of every
pixel), loads and stores, and the IOKit flush. Requests: present opaque live
surfaces directly on macOS as on Windows, or at least skip the clear, use `src`
for opaque surfaces, and accept the surface in the destination's byte order. A
scene-sized surface enlarged by the presenter would also remove the producer's
enlarging copy.
