# One renderer: r2d, r3d and one mesh library

2026-10-08. The plan that follows `RENDERER_STUDY.md`. The suite ends with one shared
engine in `shared/render/`: r2d, r3d, one mesh library, the bug fixes made once, and
every game drawing through it. The aim is to **do less work, not to do the same work
faster**. Threads may spread work over cores, but they do not reduce it, so they are an
option a game turns on, never the plan.

## The principle: the scene says what to do

Generic bookkeeping is itself work: dirty flags on every object, per-tile hashes, and
comparing frames to find what changed. We don't add it. The scene is arranged so that
its structure already answers the questions:

| The scene already knows | So the engine skips |
|---|---|
| What never moves (backdrop, terrain, rooms, a resting cube) | Redrawing it: kept as pixels with depth, and restored instead of drawn |
| What moved this frame (its animation state says so) | Everything outside that thing's screen bounds, this frame's and last frame's |
| Which side of a closed shape faces the camera | The far side, before anything is projected |
| Which surfaces cover which (tiles over the glass, a wall over the floor) | Drawing the covered surface, by drawing near things first so depth rejects the rest before shading |
| What the display wants (format, scale, opacity) | Conversions: pixels are written once, in the window's own byte order, at device resolution |

The engine asks for no more than the game naturally has: a list of layers tagged
*backdrop*, *still* or *moving*, and for each moving thing its bounds.

## What others do, and what we take

| Who | What they do | What we take |
|---|---|---|
| EGL `EXT_buffer_age`, Android and Chrome partial redraw | A swap-chain buffer still holds the frame it showed *n* frames ago, so only the damage since then is redrawn | `Surface`: GUI.Forms' LiveSurface buffers keep their pixels at stable addresses, so each frame repairs only that buffer's stale rectangle, never a whole-frame copy |
| Flutter's raster cache, Chrome's layers, Quake's surface cache | Layers that don't change are rasterised once and reused as pixels | *Still* layers kept with their depth (Mowing's `Shot` and Rock Stack's saved shadow, generalised) |
| Quake's span renderer | World polygons sorted so each screen pixel is written exactly once | Near-to-far order within a layer, with depth tested *before* shading; a visibility buffer when shading is dear |
| Visibility buffer (Burns and Hunt, 2013) | Rasterise triangle ids and depth, then shade each visible pixel once | An option for heavy materials (Mowing's patterns): overdraw costs a depth test, not a shade |
| three.js | Retained scene, geometry uploaded once and indexed, matrices recomputed only when dirty, frustum culling by bounding sphere, programs compiled per material, render only when asked | Indexed meshes transformed once per frame per instance; culling by sphere; the pixel loop chosen per material at compile time; drawing only on change |
| Hierarchical Z (Greene, 1993), llvmpipe binning | Whole tiles rejected before per-pixel work | Scissoring: a triangle outside the damage rectangle costs its bounds test and nothing else |
| Bilinear upscalers, the old present steps | Render small, enlarge, enlarge again | Removed: one resolve writes device pixels directly (k × k blocks for the pixel-art games) |

## Layout

```
shared/render/
  src/target.hpp     Rect, byte order, a Target: pixels the engine writes, not owned
  src/r2d.*          the 2D layer, mostly passthrough: restore a rectangle, fill, blend an
                     alpha mask (text), round-rect cards, dimming, cover-fit pictures
  src/r3d.*          rasteriser: 8-bit subpixel fixed-point edges, top-left rule, exact
                     spans, depth test before shading, scissor, optional ids for picking;
                     shaders chosen at compile time (flat, Gouraud, mirror, over, ...)
  src/mesh.*         one mesh library (later): indexed, bounded, Mowing's Builder plus
                     the r3d primitives
  ui/surface.*       GUI.Forms side: a LiveSurface in the native byte order, opaque, with
                     per-buffer age, handing the game a Target and the rectangle to repair
```

`render_core` is portable and has no GUI.Forms dependency, so the preview tools and
the core tests draw the same frames the window shows.

### Options a game turns on

Each costs nothing unless a game enables it:

| Option | Default | For |
|---|---|---|
| `ids` (picking buffer) | off | the cube, card and puzzle games that pick on the picture |
| `shadows` (sun map, static half kept) | off | Rock Stack, Mowing, the r3d games that want it |
| `samples` (4-sample visibility, resolved once) | 1 | Mowing's clean edges; Four Pegs' lair |
| `perspective_correct` textures | off | Maze, close-up perspective scenes |
| `grid` (k × k output pixels) and `dither` | 1, off | the low-resolution r3d games |
| `threads` (bands over GUI.Forms' `AtomicThreadPool`) | off | Only once the work is already minimal |

## Order of work

1. **Nature Cube** (first). It draws straight into the window's buffer at device
   resolution, in the native byte order. That removes:
   - its own raster and the bilinear composite;
   - the whole-frame copy into the surface;
   - the per-pixel conversion from BGRA on macOS.

   A turning cube repairs only its own bounds (this frame's and last frame's). A
   resting cube that is being traced repairs only the cells of the animating lines and
   the hover. The panorama is prepared once, already darkened, in fixed point.
2. The seven identical `r3d` games, then the Four Pegs, Switchbox and Eggy forks.
   Indexed meshes from the one library, tints as draw constants instead of copied
   meshes, outlines as an edge pass, and present and publish merged into one resolve.
3. Rock Stack (shadows, bands) and the 2D `Canvas` users onto r2d and GUI.Forms'
   painter.
4. Stillwater (the ambient engine's core is r3d's ancestor), Maze (perspective-correct),
   and Mowing (visibility buffer, materials and kept shots move into the engine).
5. The game copies are deleted.

Each step lands with a timing against the previous build on the M4, measured the same
way: work per frame, and frames per interaction.
