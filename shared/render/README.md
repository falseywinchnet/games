# The renderer: r2d and r3d

The suite's one renderer, for every game that draws its own pixels. Its rule is to do
less work, not the same work faster: the scene says what moved, the engine redraws only
that, and pixels are written once, in the window's own byte order, at device
resolution. `docs/RENDER_ENGINE.md` is the plan it follows and the reasons; this page
is how to use it. Thirteen games draw through it (every `"engines": ["render"]` in
`games/*/GAME.json`).

## The parts

| Part | File | What it is |
|---|---|---|
| `Target`, `Rect`, `Order` | `src/target.hpp` | Where the engine writes: 32-bit premultiplied pixels it does not own (a `LiveSurface` buffer, a kept layer, a preview frame), in the window's byte order (`bgra` on Windows, `rgba` on macOS and Linux), and half-open pixel rectangles with `united`, `intersected`, `inflated`. |
| r2d | `src/r2d.hpp` | The few pixel operations around a 3D picture, each bounded by a clip rectangle: `restore` (put back a kept layer under something that moved), `fill`, `tint` (dim under a panel), `mask` (blend a colour through an 8-bit coverage mask: text), `rounded` (an anti-aliased rounded rectangle), `over` (lay a premultiplied layer at an opacity: fades). |
| Canvas | `src/r2d_canvas.hpp` | The 2D vector rasteriser: paths of lines, quadratic and cubic curves, ellipses and rounded rectangles; affine transforms with `save`/`restore`; solid, linear and radial paints with any number of stops; strokes with round joins; signed-area coverage, so same-winding overlaps union and a reversed sub-path cuts a hole; `draw_mask` for text and `draw_canvas` for layers. Output is premultiplied BGRA a `LiveSurface` takes as-is. |
| r3d | `src/r3d.hpp` | The triangle rasteriser: 8-bit subpixel fixed-point edges with the top-left rule (shared edges covered exactly once), each row's run solved exactly from the edges, depth tested before any shading, a scissor bounding every write, optional triangle ids for picking (`Buffers`), and shaders chosen at compile time (`Flat`, `Smooth`, `Mirror` over a `Panorama`, and the ones the renderer below adds). |
| Renderer | `src/r3d_renderer.hpp` | The 1990s-style low-resolution 3D view on top of r3d: an orthographic (isometric) or mildly perspective camera (`yaw`, `pitch`, `scale`, `persp`, `target`), z-buffered textured triangles with nearest sampling between dithered mip levels (`Tex::build_mips`), Gouraud or two-tone (`toon`) light with sky and ground ambient, highlights (`gloss`), haze, cut-out and translucent materials, billboards, sun shadows with a kept static half (`shadow_begin`, `shadow_cast`, `shadow_save`), optional bands across the cores (`threads`), and `present` or `present_supersampled` with ordered dithering and whole-pixel enlargement. `project`, `unproject_ground` and `unproject_plane` for picking. |
| Meshes | `src/r3d_mesh.hpp` | One mesh library: sphere, hemisphere, cylinder, disc, cone, box, torus, star, a seeded lumpy rock; `draw_mesh` with a tint and texture scale read as the mesh is drawn (no copies), inverted-hull `draw_outline`, `tint`. |
| Surface | `ui/surface.hpp` | The window's end: a `LiveSurface` in the native byte order, opaque, with per-buffer age. `begin(damage)` hands back a `Target` and the rectangle to repair (this frame's damage plus what that buffer missed), `publish` shows it. Compiled into the application only when a game uses the engine. |
| PixelSurface | `ui/pixel_surface.hpp` | For the games drawn in big pixels: a small `Canvas` frame enlarged by whole window pixels straight into the live buffer, with the game's crisp text over it at window resolution; only rows and texts that changed are enlarged again. |
| Overlays | `ui/overlays.hpp` | `refresh_overlays`: after a publish, asks the overlay-plane controls the frame reaches (the capsule, a help button, a game's floating buttons) to paint again over it, since the toolkit repaints those pixels only when the overlay itself is invalidated. A control deriving `StillOverlay` (an opaque panel, a dimmed sheet) is left alone. |

`render_core` is portable (no GUI.Forms, no clock, no threads unless `threads` is on), so
a game's preview tools and the core tests draw the same frames the window shows.

## How a frame goes

1. The game keeps layers tagged *backdrop*, *still* or *moving*, and for each moving
   thing its bounds this frame and last. That is all the engine asks of it.
2. `Surface::begin(damage)` returns the buffer and the rectangle to repair; nothing
   outside it is touched.
3. Still layers are `restore`d inside the repair rectangle; moving things are drawn with
   the scissor set to it, near things first so depth rejects what they hide before it
   is shaded.
4. Text is blended through masks from GUI.Forms' text service (`r2d::mask`), at device
   resolution, after the scene.
5. `publish`. A frame in which nothing moved is never begun.

## Options a game turns on

Each costs nothing unless enabled: `ids` (a picking buffer), `shadows` (a sun map with
the static half kept), supersampling (`present_supersampled`), `persp` (perspective),
`present`'s `scale` and `dither` (the pixel-art games), `threads` (bands over the cores,
only once the work is already minimal).

## Cost

`r3d_bench [frames]` times a scene like the games' (textured ground, toon-lit characters
with outlines, props, translucent glows, the dithered present) and prints milliseconds
per stage and a checksum, so a change can be shown to draw the same pixels in less
time. Measure a game's own frames with the application's `--profile-idle` and
`GAMES_SCENE_TRACE`; `docs/PERFORMANCE.md` has the protocol.

## Tests (`tests/render_tests.cpp`, CTest `render_engine`)

The renderer's promises: shared edges covered exactly once, nothing written outside the
scissor, depth decided before shading, and blends that stay inside a byte.

## Limits

- Textures are nearest-sampled between mip levels, by design (the house 3D look).
- Perspective is mild: depth is affine in screen space; `perspective_correct` textures
  are an option for close-up scenes (Maze), not the default.
- A game that still carries its own copy of the older `r3d` (`games/zenconstruction`,
  `games/penthesheep`) is on the list to move over; new games use the engine.
