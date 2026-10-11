# The felt renderer

Fibre-rendered cloth for table games: the green baize under the card games, the velvet
of Koi-Koi's table, and twelve named presets from royal velvet to ivory felt. It is the
native port of Plan Paint's Fabric Lab fibre, capsule and optics renderer, carried
under its own MIT licence (`LICENSE`), in the `paint` namespace.

It is not an engine with an `ENGINE.json`: the suite links it once (`game_felt` in
`cmake/Application.cmake`) and the card table and Koi-Koi use it through `paint::`.
A game that wants cloth includes `carpet.hpp` and links `game_felt`; do not copy the
files.

## What it does

Thousands of analytic fibres are grown from a base plane (a wave field gives the pile
its disorder and crimp), each a chain of capsules with a radius and a dye; they are
projected into a height buffer and lit with a hair-shading model from a light at
`light` degrees and a viewer at `view`, then exposed. The result is a picture of cloth
with real pile direction, sheen and shadow between the fibres, not a noise pattern.

## Interface (`carpet.hpp`, `image.hpp`)

| Piece | Role |
|---|---|
| `CarpetParameters` | `color`; `web` (how much of the backing shows), `radius` (fibre thickness), `height`, `length`, `disorder`, `crimp`; `roughness`, `azimuth_roughness`, `dye`, `coverage` (fibres per area); `magnification`, `light`, `view`, `exposure`; `seed`; `hillshade`. |
| `carpet_preset(index)`, `carpet_preset_name(index)` | Twelve presets: Royal velvet, Brushed sapphire, Oxblood velvet, Billiard green, Fine wool felt, Slate felt, Emerald velvet, Plum velvet, Charcoal velvet, Moss felt, Terracotta felt, Ivory felt. The card table draws preset 3. |
| `render_carpet(parameters, width, height, cancel, parallel)` | One picture, not repeating. |
| `render_carpet_tile(parameters, side, cancel, parallel)` | A seamless square tile. |
| `Parallel` | Lends a render several threads: a `run(runner, count, job, context)` function and a thread count. Without one the render runs on the caller alone; the picture is the same either way. The card table hands it GUI.Forms' thread pool. |
| `Image`, `Color`, `composite`, `cropped`, `to_oklab` | Straight-alpha sRGB pixels and a few helpers. |

Cancellation is a `std::atomic<bool>*` checked between fibres and rows; temporaries are
released on return.

## Cost

About a second a tile on one core of an M-series laptop (0.9 s at 128 or 256 pixels,
1.3 s at 512: the fibre count follows `coverage`, not the picture size), and tens of
megabytes of scratch; `Parallel` divides that across the cores it is lent. Render once,
on a worker or at attach time, and keep the tile; the card table makes its tile when it
attaches to the window and fills the table with `fill_image_pattern`. Never render felt per frame, and never on the UI thread of a
window that is already showing.

For a material that must be made instantly, or at several display scales, use the
[paper engine](../paper/README.md) (noise and analytic strokes, milliseconds) as the
model and give the cloth the same treatment; the felt renderer is for the few places
where its pile and sheen are worth a second.

## Seeing it

`games/koikoi/src/paint/carpet.cpp` is the same renderer with Koi-Koi's presets; its
tools preview the table. There is no swatch sheet of the twelve presets yet.
