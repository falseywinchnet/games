# Primitives

What a game can draw, write and play with, and where each piece lives. The
collection has no shared game library by design: a game *copies* what it borrows
into its own folder and namespace (see [anatomy](03-anatomy.md)). This page tells
you what exists so you do not write it again.

Headers are the reference. Each is short and commented; read the one you use.

## In every kit game

### Canvas: 2D vector drawing (`src/platform/raster.hpp`)

An anti-aliased software rasteriser writing BGRA premultiplied pixels, the format
a `LiveSurface` takes as-is.

```cpp
Canvas canvas;
canvas.resize(width_px, height_px);
canvas.clear(hex(0x2B2118));

canvas.save();
canvas.scale(scale, scale);            // draw in points from here on
canvas.begin();
canvas.rrect(x, y, w, h, radius);      // also move/line/quad/cubic/close, rect, ellipse, circle
canvas.fill(Paint::lin(0, y, 0, y + h, {{0, top}, {1, bottom}}));
canvas.begin();
canvas.circle(cx, cy, r);
canvas.stroke(alpha(ink, .5f), 1.5);   // round joins and caps
canvas.restore();
```

- Colours: `rgb(r, g, b, a)`, `hex(0xRRGGBB, a)`, and `mix`, `alpha`, `shade`.
  Straight alpha in, premultiplied in the buffer.
- Paints: solid, `Paint::lin` and `Paint::rad` with any number of stops.
- Transforms: `save`, `restore`, `translate`, `scale`, `rotate`, `set_transform`.
- Coverage accumulates by signed area: sub-paths with the same winding union, a
  reversed sub-path cuts a hole (`ellipse(..., reverse = true)`).
- `global_alpha` fades everything drawn while it is set; `additive` adds light.
- `draw_mask(mask, x, y, colour, scale)` blends an alpha mask in device pixels
  (this is how text is drawn; an integer `scale` gives chunky pixel text).
- `draw_canvas(source, x, y, opacity)` blends another canvas over this one, pixel
  by pixel. Use it for a layer with transparency. For an opaque full-frame
  backdrop of the same size, assign the pixel vector instead
  (`frame.px = backdrop.px`), which is a plain copy.

`begin()` starts a new path; a path is filled or stroked once. Paths are in user
space; masks and canvas copies are in device pixels and ignore the transform.

### Text (`src/platform/text.hpp`)

```cpp
const Mask& mask = text_mask("Presses 7", Font::speech, size_px, wrap_px);
canvas.draw_mask(mask, x_px, y_px, cream);
// or, with an optional shadow:
draw_text(canvas, "Solved", Font::title, size_px, x_px, y_px, cream, 1, shadow);
```

| `Font` | Face | Use |
|---|---|---|
| `speech`, `ui` | Libre Baskerville | Body text, a character's lines, labels |
| `speech_bold`, `title` | Libre Baskerville Bold | Headings, emphasis |
| `pixel`, `pixel_bold` | Cousine, drawn without anti-aliasing | Chunky pixel lettering; request it small and draw it with `scale` 2 or 3 |

- Sizes and wrap widths are device pixels. Multiply points by the scale.
- `mask.w` and `mask.h` are the text's size; measure with them. A wrap width
  breaks lines and `mask.h` grows.
- Masks are cached by content and stay valid until `text_cache_trim()`, which the
  scene calls once per frame.
- In the application the first use of a string waits briefly for the text
  service; later uses are free. Avoid text that is different on every frame.
- The headless harness draws the same font files with a simpler line breaker, so
  a preview's text can be a pixel or two wider or narrower than the application's.
  Leave slack; never fit text to the exact pixel.

The shelf and capsule use Barlow Condensed for their own lettering. Games use the
faces above.

### Audio (`src/platform/audio.hpp`)

```cpp
audio_music("tp_music", music_on);           // crossfades; "" fades out
audio_sfx("tp_press", gain, rate, sound_on); // rate 0.5..2 shifts pitch
audio_duck_music(.6f);                       // dip the music under a stinger
audio_tick(seconds);                         // from the view's timer
audio_needs_tick();                          // true while loading or fading
audio_cabinet(foreground, music, sound);     // from set_cabinet
```

Names are file stems in `assets/audio/`. A missing file is silent. Sound loads
in the background; after asking for one, keep ticking while `audio_needs_tick()`
is true (the template does). The shared player has two crossfading music slots
and twenty-four effect voices.

**Files and the manifest.** Put sources in `assets/audio/`: effects as short
`.wav`, music and long loops as `.m4a` or `.wav`, 48 kHz. If the game has looping
music, add `assets/audio/<id>_audio_manifest.json` and name it in `GAME.json`
(`audio_manifest`):

```json
{
  "music": [
    {"id": "tp_music", "loop_end_sample_exclusive": 3072000, "seconds": 64.0}
  ],
  "stingers": ["tp_tune_won"]
}
```

`loop_end_sample_exclusive` is the loop's exact length in frames at 48 kHz; the
asset preparation step trims to it so the loop closes without a click. Three
checks in the repository state the exact number of sound files; `wire_shelf.py`
updates them.

**More than this.** Music that changes on a bar line (Four Pegs, Liar's Dice) and
looping beds whose volume and pitch follow the game (Rock Stack's brook and
motor) need a few more lines in your audio adapter. Copy the pattern from
`vendor/zenconstruction/src/platform/audio.cpp` (beds on `games::PcmPlayer`) or
`src/fourpegs_audio.*` (bar transport), and keep the extra functions in your
`platform/audio.hpp`.

### The save envelope (`src/save.hpp`)

`seal`, `unseal`, `write_save`, `read_save`: magic line, bounded text body,
checksum, atomic replacement. See [anatomy](03-anatomy.md).

### Seeded randomness (`rules.cpp`)

`next_random(state)` is splitmix64: integer-only, identical everywhere. Derive
separate streams by seeding separate states (seed, seed + 1, ...).

## The headless harness (`new-games/kit/`)

Development only; never linked into the application.

| File | What it gives you |
|---|---|
| `headless_text.hpp` | The PlaySuite fonts rasterised to masks with no toolkit, so previews show real type |
| `png_writer.hpp` | `kit::write_png(path, w, h, canvas.px)`. Pixels are stored uncompressed; run `tools/shrink_png.py` on pictures you keep |
| `view_contract_test.hpp` | The hosted-view contract as one reusable test |
| `third_party/stb_truetype.h` | The font rasteriser behind `headless_text` (public domain / MIT) |

A game's `dev/text_headless.cpp` and `dev/audio_silent.cpp` adapt these to its
namespace. The silent audio keeps a log (`audio_log()`) so a test can check that
a move asked for its sound.

## To borrow

Copy the files into your folder, change `namespace` to yours, add them to
`core_sources` and `borrowed` in `GAME.json` and to your `CMakeLists.txt`, and
record the source in `HANDOFF.md`. Take the newest copy; older ones lack fixes.

| What | Newest copy | Notes |
|---|---|---|
| Software 3D renderer | `vendor/zenconstruction/src/platform/r3d.*` | Orthographic or perspective camera, z-buffer, textured triangles with nearest sampling and mip levels, Gouraud or toon lighting, fog, cut-out and translucent materials, sun shadows, low-resolution output with dithered upscale (`present`). `project` and `unproject_plane` for picking. This copy fills in bands on worker threads; if you do not need that, `vendor/penthesheep/src/platform/r3d.*` is the single-threaded version. |
| Mesh helpers | `vendor/zenconstruction/src/platform/mesh.*` | Sphere, hemisphere, cylinder, cone, box, disc, torus, star, lumpy rock; `draw_mesh`, inverted-hull `draw_outline`, `tint` |
| Procedural textures, ground, plants, creatures | `vendor/eggy/src/textures.*`, `ground.*`, `flora.*`, `critters.*` | Generated, tileable |
| Character posing and faces | `vendor/switchbox/src/` (`girl.*`, `face.*`, `actor.*`) | A jointed, expressive 3D character with speech timing |
| Speech lines and typewriter text | `vendor/switchbox/src/platform/lines.*` | Line pools and reveal timing |
| Felt and cloth | `vendor/paint/carpet.*`, `image.*` | Fibre-rendered felt (`paint::render_carpet_tile`), under its own MIT license. This one is linked once by the suite; use it through `paint::` as `vendor/koikoi` does, do not copy it. |
| Card finish | `vendor/koikoi/src/card_finish.*` | The lit, slightly raised card look |
| Rigid-body physics | `vendor/zenconstruction/phys/` | Convex hulls, friction, sleeping, deterministic. Read `vendor/zenconstruction/PHYSICS_SPEC.md` and `phys/NOTES.md` first. |
| Hex-grid puzzle generation with a proving solver | `vendor/penthesheep/src/field.*` | The pattern for "generate, then prove solvable" |
| Deduction puzzle generation | `vendor/parrots/src/logic.*` | Generating a logic puzzle and checking its solution |
| Sound synthesis | `vendor/zenconstruction/audio_src/engine/synth.py` | Plucks, mallets, pads, flutes, percussion, a loop mixer and a reverb (NumPy). The template's `make_sfx.py` is the no-dependency starting point. |
| Low-resolution scene with crisp text on top | `vendor/penthesheep/src/sheep_view.cpp` (`publish`, `blit_texts`) | The house 3D look: scene at one pixel per two points, text blended at device resolution afterwards |
| Background generation on a future | `vendor/penthesheep/src/sheep_view.cpp` (`pending_`) | `std::async`, polled on a tick, awaited in the destructor |

The older games' views contain platform adapters (`*.mm`) and style that predate
this kit. Borrow their techniques and their portable files; do not copy their
view plumbing. The template's view is the one to start from.

## From the shell (`src/`)

Available to the view and to your emblem, through `suite.hpp`:

- `games::CommandSource`, `games::GameCommand`: the capsule's menu.
- `games::state_directory()`, `games::asset_directory()` (`runtime_paths.hpp`).
- For the emblem in `src/suite.cpp`: `disc`, `fill_vertical`, `mix_color`,
  `with_alpha`, `paint_polygon` (`presentation.hpp`), and the `gf::Painter` calls
  `fill_rect`, `fill_rounded_rect`, `stroke_rounded_rect`, `draw_line`,
  `fill_linear_gradient`, `fill_radial_gradient`.

A game does not use the shell's buttons, dialogs or text sprites on its own
surface; it draws its own with the canvas so it looks like itself.

## From GUI.Forms

The toolkit is the owner's retained-mode C++20 GUI library, used by PlaySuite and
his other applications. It lives in `gui_forms/` of the public repository
`falseywinchnet/file_manager`, pinned by commit in
`.github/workflows/applications.yml`. `check_game.py --fetch-toolkit` fetches its
headers for the syntax check.

A view uses a small part of it, all visible in the template:

| Piece | Use |
|---|---|
| `gf::Control` | Base class: `arrange`, `on_paint`, `on_pointer`, `on_key`, `on_attached_to_window`, `on_detaching_from_window`, `visible`, `invalidate`, `set_cursor`, `point_from_window`, `client_rectangle` |
| `gf::LiveSurface` | The game's pixels: `create`, `reconfigure`, `try_acquire_write` returning a lease with `pixels`, `row_bytes`, `publish` |
| `gf::Window` | `scale`, `request_focus`, `queue_live_surface_presentation`, `active`, `occluded` |
| `gf::Timer` | `start`, `stop`, `enabled`, `interval`, `set_interval`, and a `tick()` event bound with `gf::Delegate<>::bind` |
| `gf::PointerEvent`, `gf::KeyEvent`, `gf::PhysicalKey` | Input |
| `gf::Painter` | Only for `draw_live_surface` in `on_paint`, and for the emblem |

**Do not change the toolkit to suit a game.** Reusable capabilities are meant to
become toolkit features, decided by its owner. If a game needs something the
toolkit lacks, write down what and why in `HANDOFF.md` (the existing list is
`docs/TOOLKIT_REQUESTS.md`) and work within what exists.

Known toolkit behaviour to design around:

- Nothing may be drawn over a live surface by another control. That is why the
  capsule has a rail, and why a game's panels are drawn by the game.
- A retained repaint shows the last published frame through `on_paint`. Always
  implement `on_paint` with `draw_live_surface`.
- Letter spacing is unreliable; do not depend on it.
