# Web port: Mowing, Maze 95, Eggy, Switchbox, Pen the Sheep

Checked in the browser build (silent page, never `?sound`) on 2026-10-23.
Interactive checks used the app's browser pane; frame costs and saves used
headless Chrome (`--mute-audio`, silent page) because a hidden pane throttles
`requestAnimationFrame` to 1 Hz. Window 1200 × 713 CSS px at device pixel ratio
2 (2400 × 1426 device pixels), 6 logical cores.

## Status

| Game | Opens, plays | Looks native | Threads (pool 16) | Save survives reload | Node tests |
|---|---|---|---|---|---|
| Mowing | Yes: mows on its own, arrow-key driving, N new garden, Space pause, Q detail, Settings | Yes (lawn, props, mower) | No worker beyond the 16; lawn crew capped at 8 on the web | Yes (settings: mower, detail; the garden is not saved natively either) | rules, voice, banjo, view contract pass |
| Maze 95 | Yes: walks itself, three mazes cleared in 200 s, Restart maze | Corridors yes; **posters and resident pictures missing** (not in the web asset pack) | 16 | Yes (level, seed) | rules, idle, view contract pass |
| Eggy | Yes: title, climb under autopilot, keys | Yes | 16 | Yes (`eggy-v1.txt`, written every 15 s once climbing) | sim, frames pass |
| Switchbox | Yes: switches, a win, top-score name entry, Top scores, Settings, back to shelf | Yes | 16 | Yes (`switchbox-v2.txt`, scores) | rules, storage, actor, frames pass |
| Pen the Sheep | Yes: fences, Undo, Hint, a loss (escape), A new meadow | Yes | 16 | Yes (seed, moves) | rules, frames pass |

No console errors in any game. The view contract tests need
`GAMES_ASSET_DIR=<runtime assets>`, as their CTest entries set.
`vendor_game_frame_tests` has no fixture for mowingman or maze (also natively).

## Fixes on this branch

- Switchbox top-score names: the name panel marked every key handled, so no
  text input followed (on every host) and the box always signed BOX FRIEND.
  Only Backspace, Enter and Escape are taken now. Atom Probe has the same pattern
  (`atomprobe_view.cpp`, name panel in `on_key`).
- Switchbox cursor on the web: when she holds your switch she hides the system
  cursor and draws her own; when she lifts the pointer away she drops it and
  natively warps the real cursor there. A page can do neither, so the game said
  "The pointer stays with you here." and the mischief was lost. With the
  GUI.Forms patch below (hiding only), the game keeps the real cursor hidden after
  the drop and draws the pointer where she left it, at the height she let go;
  real motion melts the offset (half of each step) until the two meet and the
  real cursor returns. Clicks act at the drawn pointer meanwhile. Releasing
  pointer capture revokes a hidden-cursor lease in GUI.Forms, so the game takes
  a new lease in the same turn. Verified in a build with the patch. Without the
  patch the behaviour is unchanged (refused, said once per attempt).
- Grass: `lawn_threads()` is at most 8 under Emscripten; jobs seed by cell, so
  the picture is unchanged.
- `switchbox_storage_tests`: removes its emptied scratch folder with
  `remove_all`; Emscripten's NODERAWFS on macOS returns EPERM to `remove` on a
  directory. Atom Probe's and Four Pegs' storage tests have the same line.

## Shared patch (GUI.Forms web host, for the lead)

Lets a page hide the pointer over its canvas (`cursor_capabilities` hide = true,
warp = false). Without it every game that hides the cursor is refused on the web.

```diff
--- a/src/host/web/web_host.cpp
+++ b/src/host/web/web_host.cpp
@@ -36,6 +36,7 @@
 void gf_web_js_set_cursor(int kind);
+void gf_web_js_hide_cursor(int hidden);
 void gf_web_js_pointer_capture(int captured, int pointer_id);
@@ -135,6 +136,17 @@
   HostServiceStatus set_cursor_impl(CursorKind cursor) override {
     gf_web_js_set_cursor(static_cast<int>(cursor));
+    return {};
+  }
+  // The pointer can be hidden over the canvas; a page cannot move it.
+  CursorCapabilities cursor_capabilities_impl() const noexcept override { return {true, false}; }
+  CursorStatus cursor_authority_impl(const Window&, bool) const noexcept override { return {}; }
+  CursorStatus hide_cursor_impl() noexcept override {
+    gf_web_js_hide_cursor(1);
+    return {};
+  }
+  CursorStatus restore_cursor_impl() noexcept override {
+    gf_web_js_hide_cursor(0);
     return {};
   }
--- a/src/host/web/web_host.js
+++ b/src/host/web/web_host.js
@@ -192,7 +192,17 @@
   gf_web_js_set_cursor__deps: ['$GFWeb'],
   gf_web_js_set_cursor: (kind) => {
-    if (GFWeb.canvas) GFWeb.canvas.style.cursor = GFWeb.cursors[kind] || 'default';
+    GFWeb.cursorKind = kind;
+    if (GFWeb.canvas && !GFWeb.cursorHidden)
+      GFWeb.canvas.style.cursor = GFWeb.cursors[kind] || 'default';
+  },
+
+  // A page may hide the pointer over its own canvas, though never move it.
+  gf_web_js_hide_cursor__deps: ['$GFWeb'],
+  gf_web_js_hide_cursor: (hidden) => {
+    GFWeb.cursorHidden = !!hidden;
+    if (GFWeb.canvas)
+      GFWeb.canvas.style.cursor = hidden ? 'none' : GFWeb.cursors[GFWeb.cursorKind || 0] || 'default';
   },
```

## Frame cost (headless Chrome, release build)

Per tick: main-thread update. Per frame: live composition, retained paint,
presentation, device pixels drawn.

| Game, state | fps shown | update / tick | live / paint / present per frame | kpx / frame |
|---|---|---|---|---|
| Switchbox, girl in the box | 5.3 (paced 33 ms, repairs only change) | 5.1 ms | 0.4 / 0 / 0.9 ms | 252 |
| Eggy, autopilot climb | 9.5 (paced 83 ms) | 4.1 ms | 2.0 / 7.5 / 4.7 ms | 3422 |
| Mowing, mowing | 27 | 2.9 to 4.8 ms | 0.2 / 0 / 0.5 ms | 643 |
| Mowing, growing a new garden | 28.5 | 9.0 ms (worst rAF gap 126 ms) | 0.4 / 0 / 0.7 ms | 513 |
| Mowing, paused | 0 | 0.02 ms | none | 0 |
| Maze 95, walking | 26 | **16.5 ms** (rAF p50 28 ms) | 0.6 / 4.7 / 0.7 ms | 1747 |
| Pen the Sheep, idle sheep | 30 | 3.1 to 3.4 ms | 0.7 / 0 / 0.4 ms | 1250 to 1440 |

Native references: Maze README gives 1 to 3 ms a frame for its renderer at
366 × 253; the other games record no per-frame native figure. Maze is the one
game whose main thread is saturated on the web (frames every ~28 ms).

## Assets each game opens

Fonts (Barlow Condensed, Carlito, Cousine, Libre Baskerville) are opened by the
shell for every game; Switchbox adds ComicNeue-Regular/Bold.

- Mowing: nothing else in the current web pack. Its sample cues (`mm_*.ogg`)
  and `mowingman/audio/` are absent from the web pack (present in the native
  runtime).
- Maze 95: `maze/posters/*.png` (78, 4.3 MB) and `maze/residents/*.png` (16,
  0.7 MB) are opened but **absent from the web pack**; `maze/audio/maze_audio_manifest.json`;
  `audio/mz_*.ogg` (29, 3.0 MB).
- Eggy: `lines/*.txt` except `lines/switchbox/` (54 files, 1.1 MB);
  `audio/eggy_*.ogg` (49, 17 MB).
- Switchbox: `lines/switchbox/*.txt` (30); `audio/switchbox_*.ogg` (17),
  `music_switchbox_loop.ogg`, `stinger_win_switchbox.ogg`,
  `stinger_topscore_switchbox.ogg`, `ui_name_*.ogg`, `ui_new_game.ogg`;
  `back_*.png` for the Settings sheet. `switchbox-girl.png` is not opened.
- Pen the Sheep: `audio/sh_*.ogg` (17, 2.0 MB).

## Sound: live voices and sample files

| Game | Live `AudioGenerator` voices | Sample files | Samples made by |
|---|---|---|---|
| Mowing | `LiveMower` (engine, blades, chipper, grill knock, the old lady's lines) and `LiveBand` (banjo music), when `GUI_FORMS_AUDIO_GENERATOR` | `mm_bump`, `mm_freeze`, `mm_gnome_*`, `mm_mushroom`, `mm_puff`, `mm_scream`, `mm_shatter`, `mm_granny_bonk`; without generators also `mm_mower_run`, `mm_music_mowing` | code: `audio_src/make_audio.py` (NumPy, "no recordings") |
| Maze 95 | the band (`LiveBand` through `audio_live_music`) | `mz_*` effects and name-entry clicks, `mz_music*` files | code: `audio_src/make_sfx.py`, `mall_music.py`, `score.py` ("synthesized, no samples") |
| Eggy | none | all 49 `eggy_*` (music, effects, ambience beds) | code: `incoming/game-eggy-01/source/audio_src/eggy_music.py`, `eggy_sfx.py`, `eggy_synth.py` |
| Switchbox | none | `switchbox_*`, music loop, stingers, `ui_*` | code: babble and clap from `incoming/game-switchbox-01/source/audio_src/make_sfx.py` ("no samples"); music, stingers and switch sounds from `authoring/audio/work/src/songs2.py`, `sfx2.py` |
| Pen the Sheep | none | all 17 `sh_*` | code: `audio_src/make_sfx.py`, `meadow_music.py`, `long_grass.py`, `angel_music.py` |

## What remains

- The web asset pack must carry `maze/posters/` and `maze/residents/` (Maze is
  not at native fidelity without them) and `mowingman/`.
- Apply the GUI.Forms cursor-hide patch; then Switchbox's hold and drop work as
  described.
- Maze's main thread: 16.5 ms a tick at this size against 1 to 3 ms natively;
  worth a profile with `GAMES_WEB_DEBUG=ON`.
- Pen the Sheep: a pen (win) was not reached by hand; the loss dialog, Undo and
  new meadow were. Eggy's summit and Mowing's finished garden were not played to
  the end in the browser (their rules tests run them).
- Loading the page into a zero-size viewport (a hidden pane) gave "Web host
  attach rejected" and five uncaught exceptions; the page stayed dead until
  reloaded at a real size.
