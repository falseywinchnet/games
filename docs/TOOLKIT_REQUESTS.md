# GUI.Forms requests from the PlaySuite rework

AGENTS.md asks for reusable capabilities to become GUI.Forms enhancements, coordinated with the toolkit owner. The PlaySuite shell needed several things the pinned toolkit (`7b260cf`) does not offer. Games carries small local versions so the application works today. Each entry below names the local stand-in that a toolkit feature would replace. None of these changes were made to the shared toolkit checkout.

## Proposed toolkit features

1. **Application display faces in `Painter` text.** `FontRole` has only `control`, `content` and `monospace`, mapped by each host to bundled faces. Games paints its sign and box titles in Barlow Condensed and Libre Baskerville through `src/text_sprites.*`. That code shapes masks with the text-mask service, converts the coverage to premultiplied images, and draws placeholder text in the host face until the masks are ready. What would replace it: a way to register an application font family, or a `display` role, usable from `draw_text_utf8` and `measure_text_utf8`.

2. **Floating command bar control.** `src/capsule.*` is a generic overlay pill. It shows primary commands when folded, opens on hover with an eased width and height, wraps commands onto extra rows when narrow, can be pinned open, and leaves a shadow margin that is not hit-tested. A command model (id, label, enabled, checked, primary) drives it. Other applications could use it for full-window canvases.

3. **Device scale on `Painter`.** Polygon filling (`paint_polygon` in `src/presentation.cpp`) steps one device row at a time, so translucent fills never overlap. The shell has to publish the window scale through `set_polygon_scale` because `Painter` does not expose it.

4. **Anti-aliased polygon and path fill on `Painter`.** Only `gui_drawing::GraphicsRecorder` has paths, and it is not drawable through `Painter`. Games fills polygons with device-row spans and blends the partly covered pixel at each span end. Tiled shapes need a hard-edged mode, or seams appear between them.

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

1. **Retained repaints hide directly presented live views until their next frame.** A full-window repaint (an expose, or `PrintWindow` during automated captures) redraws a direct-presented view from its retained `on_paint`, which is the game's fallback fill. The live pixels return only when the game publishes a new generation, so a game that publishes rarely can look blank after an expose. Automated screenshots must therefore copy from the screen. A fix would be for the retained repaint to composite each registered surface's latest published frame. In the transactional DIB mode, `present_live_surface_updates` also skips presenting while other damage is pending, which makes needless invalidation costly. The PlaySuite shell avoids it: for example, button setters return early when nothing changed.

2. **Ordinary controls over a live view.** Only `PaintPlane::overlay` controls are subtracted from direct presentation. The repaint of an ordinary control that overlaps a live view overwrites live pixels until the next frame. The PlaySuite capsule therefore stays within the rail above the four live-surface games.

3. **Letter spacing drops characters.** With `FontSpec::letter_spacing` above zero, the Win32 DIB text path dropped or overlapped spaces, periods and digits. Examples: "Welcome back. Your…" rendered as "Welcome backYour", and "MISTAKES 0" overlapped. Games now uses letter spacing only on static uppercase captions.
