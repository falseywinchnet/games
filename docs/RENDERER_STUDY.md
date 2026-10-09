# The suite's 3D renderers: a study toward one engine

2026-10-08. Every 3D game in PlaySuite draws on the CPU with a renderer of its own.
GUI.Forms gives them a window, Skia for 2D, text and audio, but no 3D. This study
inventories those renderers, follows how geometry and effects reach the pixels,
measures where the time goes, and compares the design with other small engines, as
groundwork for one fused engine.

## 1. Inventory

| Renderer | Where | Used by | Kind |
|---|---|---|---|
| `r3d` | `games/<id>/src/platform/r3d.*` | Pen the Sheep, Atom Probe, Catching Thieves, Koi-Koi, Liar's Dice, Parrots (7 copies differing only in namespace); Four Pegs and Switchbox (small forks); Rock Stack (heavy fork: shadow maps, sky/ground ambient, gloss, banded threads) | Immediate-mode scanline rasterizer, orthographic or simple perspective, affine textures |
| Eggy's `r3d` | `games/eggy/src/r3d.*` | Eggy | An earlier fork of the same (89 lines differ) |
| `soft3d` | `games/maze/src/soft3d.*` | Maze 95 | First-person: perspective-correct texturing, near-plane clipping, z-buffer, camera lamp |
| `raster3d` (ambient) | `shared/ambient/src/raster3d.hpp` | Stillwater, the ambient engine | Fixed-point edge functions, top-left fill rule, near clipping, per-pixel "policy" templates (depth, visibility buffer, Gouraud, fragment lists) |
| `model3d` | `games/mowingman/src/model3d.*` | Mowing | Builder meshes, 4 samples a pixel into a visibility buffer, per-pixel material shading, shadow map, baked vertex AO, kept still pictures |
| `raster3d` (cube) | `games/cube/src/raster3d.*` | Nature Cube | Edge-function rasterizer with a panorama reflection per pixel |
| `PuzzleRaster` | `shared/puzzles/puzzle_render.*` | the puzzles; parent of the cube's | 2D/2.5D tiles and pieces |

And beneath most of them, a 2D layer:

| Layer | Where | Copies |
|---|---|---|
| `Canvas` (`platform/raster.*`): anti-aliased vector paths, curves, gradients, strokes, BGRA premultiplied | every `r3d` game, Eggy, Mowing | 11 (4–10 lines apart) |
| `mesh.*`: unit sphere, cylinder, cone, box, torus, star, lumpy rock; outline helper | every `r3d` game | 9 (4 lines apart in 8) |

About 20,000 lines of renderer code exist to do, in seven variations, what one engine
of a few thousand lines would do.

## 2. How geometry and effects reach the pixels (the `r3d` family)

1. **Textures** are painted procedurally at startup with the 2D `Canvas` into small
   power-of-two `Tex` images (64–256 px), box-filtered into a mip chain.
2. **Geometry** comes from the mesh builders as unit-sized, *non-indexed* triangle
   lists (`std::vector<Vtx>`, three vertices per triangle), cached per shape and
   detail level. A scene is a sequence of `draw_mesh(mesh, transform, texture, tint,
   material)` calls each frame.
3. **Per draw call:** the whole mesh is copied to apply the tint and texture scale,
   and the transform's normal matrix (a 3 × 3 inverse) is recomputed.
4. **Per vertex, per triangle:** each vertex is transformed, projected and lit
   (Gouraud: ambient + sun · N·L), and its fog factor computed. A vertex shared by six
   triangles does all of this six times.
5. **Per triangle:** back-face test, bounding rows, then plane equations for up to 11
   attributes (z, s, t, r, g, b, a, fog, splat weight, toon light), and a mip level.
6. **Per scanline:** the span is found by intersecting all three edges.
7. **Per pixel:** depth test, then as the material flags say: toon ramp (smoothstep),
   nearest texel (with an optional second "splat" texture chosen by a noise
   threshold), cut-out discard, fog, and an opaque, alpha or additive write into a
   **floating-point RGB buffer** (12 bytes a pixel) plus a float depth buffer.
8. **Effects are material flags and extra draws:**
   - outlines: the mesh drawn again, inflated along its normals, back faces only;
   - glows and sparkles: camera-facing billboards with additive blending;
   - haze: per-vertex fog;
   - two-tone light: `toon`.

   There is no clipping (a perspective divide is clamped instead), and translucent
   surfaces are blended in submission order.
9. **Present:** the float buffer is quantised to 15-bit colour with a 4 × 4 ordered
   dither and enlarged by whole pixels (nearest) into a `Canvas`.
10. **Publish:** the game composes its 2D HUD over that `Canvas` and copies it into a
   GUI.Forms `LiveSurface`, enlarged again to device pixels.

The other designs differ at the points that matter:
- the ambient engine scans with exact fixed-point coverage and clips;
- Maze divides per pixel for perspective-correct textures;
- Mowing shades once per visible triangle per pixel after a 4-sample visibility pass,
  and keeps whole pictures of anything that does not move.

## 3. Where the time goes (measured)

The real app on the M4, 1100 × 720 at scale 2, each game idle on screen. `sample`
took 6 s at 1 ms (≈ 6,000 samples = one core).

| Game | Hot spots (samples) | Share of a core |
|---|---|---|
| Rock Stack | `Canvas::rasterize` 1,222; `Canvas::clear` 149 | ≈ 23 % |
| Four Pegs | `R3D::draw` 976; `present_supersampled` 167; `Canvas::rasterize` 83 | ≈ 20 % |
| Pen the Sheep | `R3D::draw` 361; `present` 161; publish copies 97; clears 194 | ≈ 14 % |
| Stillwater | Gouraud scan 215; fragment scan 101; compose 123; actors 111; sway 101 | ≈ 11 % |
| Eggy | `R3D::draw` 215; text blit 54; `Canvas::rasterize` 42 | ≈ 7 % |
| Mowing | simulation and lawn shading dominate; the 3D models barely appear (kept pictures) | — |

Nature Cube, timed directly: 10 ms a moving frame at 810 × 810 with full shading,
4 ms in its old draft mode.

**Expensive:**
- **Redrawing what has not changed.** Most games redraw their whole scene every
  frame: Four Pegs' lair, Rock Stack's scene with its panel and HUD, the 2D layers.
  Only Pen the Sheep keeps its static land, and only Mowing and Rock Stack keep
  static pictures or shadows. This is the largest single cost and the cheapest to remove.
- **The 2D `Canvas` at device resolution.** Tracing curves and filling with
  signed-area coverage is accurate but slow, and it runs per frame for HUDs and panels
  that rarely change (Rock Stack's 20 %).
- **Per-pixel work multiplied by samples:** the cube's panorama reflection (4 texel
  reads × 3 channels in doubles) and Four Pegs' 2 × 2 supersampling (four times the
  rasterizer cost of its lair).
- **Wide float buffers:** 12-byte RGB plus 4-byte depth per pixel, then a separate
  quantise-and-dither pass, then one or two more enlarging copies before publication.

**Cheap:**
- per-triangle setup at these triangle counts (hundreds to a few thousand);
- flat opaque spans (the cube's fast path);
- mip selection;
- kept still pictures (Mowing's models cost almost nothing once drawn).

**Foundational redundancy:**
- seven-plus copies of one rasterizer, eleven of one 2D canvas, nine of one mesh library;
- a hand-written 2D vector rasterizer in every game, while GUI.Forms already offers
  Skia, including an offscreen `PaintFramebuffer`;
- non-indexed meshes: each shared vertex transformed and lit several times;
- a mesh copy per draw call just to apply a tint;
- outlines drawing every mesh twice;
- three separate enlargement steps (present, then publish, then the host);
- seven ways to answer the same questions (projection, clipping or not, fill rule,
  depth convention), with fixes landing in some copies only. The `Canvas::accumulate()`
  fix for an intermittent read past the end of a row is in eight copies; Atom Probe,
  Four Pegs and Switchbox still carry the old version.

## 4. Against other small engines

| Engine | Approach | What it teaches us |
|---|---|---|
| Quake (1996) software renderer | Span-based scan conversion, perspective correction every 8–16 pixels; a *surface cache* of lit textures reused across frames | Cache work that doesn't change per frame, as Mowing's kept pictures do |
| TinyGL | A software subset of OpenGL 1.x: indexed geometry, fixed-point scanline, one thread | One API for all callers; vertices transformed once |
| tinyrenderer (educational) | Barycentric edge functions, shaders as functions | The ambient engine's "policy" idea is the same separation of coverage from shading |
| small3dlib | Header-only, integer-only, a per-pixel callback | Fixed-point everywhere keeps it portable and deterministic |
| Mesa llvmpipe, SwiftShader | Production CPU GPUs: triangles binned into screen tiles, tiles shaded on all cores with SIMD and JIT-compiled shaders, whole tiles rejected early | Tile binning plus threads plus SIMD is where the order-of-magnitude gains are |
| Visibility-buffer renderers (modern GPU practice) | Rasterize ids and depth first, shade each visible pixel once | Mowing already does this; it removes the cost of overdraw |

Ours sit between TinyGL and tinyrenderer in technique: correct, readable,
single-threaded, scalar, and redrawing everything. Nothing here is unusual for a small
engine. What is unusual is having seven of them.

## 5. What a fused engine would take from each

- **Rasterization core:** the ambient engine's. It has fixed-point edge functions, the
  top-left rule and near-plane clipping, and pixel policies chosen at compile time.
  Add perspective-correct interpolation from Maze, as a policy option.
- **Shading:** Mowing's visibility buffer with optional 4-sample resolve. Geometry is
  rasterized once into depth and ids, and each visible triangle is shaded once per
  pixel. This makes anti-aliasing affordable and overdraw free.
- **Materials as data:** the `r3d` flags (cut-out, translucent, additive, toon, unlit,
  fog), Rock Stack's gloss and sky/ground ambient, Mowing's procedural patterns,
  and the cube's panorama reflection, all as one material description evaluated in
  the shading pass.
- **Geometry:** indexed meshes with a per-mesh transformed-vertex cache; tints as a
  per-draw constant, not a copied mesh; Mowing's `Builder` (lathe, loft, sweep) and
  the `r3d` primitives in one library.
- **Effects:**
  - outlines as a screen-space edge pass on the depth and id buffers, not a second draw of every mesh;
  - shadows from Rock Stack's map, with its saved static half;
  - baked ambient occlusion from Mowing.
- **Caching as a first-class idea:** static layers (land, rooms, stones, shadows) kept
  as pictures with depth, composited under the moving parts. That generalises Mowing's
  `Shot` and Pen the Sheep's land cache.
- **Threads, optionally:** bands on GUI.Forms' `AtomicThreadPool` spread work but do not
  reduce it. They are an option a game may turn on once its work is already minimal,
  not the plan.
- **Output:** 8-bit premultiplied BGRA written once at device resolution, straight
  into the `LiveSurface`. The pixel-art games ask for a chunkier output grid instead
  of a separate enlargement pass. Dither stays a policy for the games that want it.
- **2D:** the HUDs, panels and procedural textures move to GUI.Forms' painter (Skia,
  via `PaintFramebuffer`), and the eleven `Canvas` copies go.

## 6. Order of work, by payoff

1. **Stop redrawing unchanged frames.** In each game, redraw only when its state or
   animation changes, and keep static layers. Needs no new engine. Largest win
   (Rock Stack, Four Pegs).
2. **One shared `r3d` library in `shared/`** replacing the seven identical copies, then
   the Four Pegs, Switchbox and Eggy forks. Mechanical, and fixes then land once.
3. **2D onto GUI.Forms' painter**, retiring the `Canvas` copies.
4. **The fused engine** (section 5), migrating the `r3d` games first, then Stillwater,
   Maze, the cube and Mowing.

The plan that follows from this study, and its progress, is in `RENDER_ENGINE.md`.
