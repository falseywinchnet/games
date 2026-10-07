# The soil engine

A painter of garden ground for PlaySuite's software-rendered scenes. It builds a
height field in metres, lights it, and returns a picture: tilled loam, raked beds,
ridged rows, trodden paths, a sun-baked crust, soil darkened by rain, autumn leaf
litter, frost and snow, and the fan of fresh spoil thrown out round a hole.

Catching Thieves (`games/catchingthieves`) paints its beds and the raccoons' burrows
with it. Any game that shows bare earth can declare `"engines": ["soil"]` and link
`soil_core`.

## What makes it read as soil

Studied from tilled, raked, trodden, dry, wet and frozen garden soil:

| In the ground | How it is made |
|---|---|
| Worked soil is all aggregate | Four bands of lumps (coarse clods, small clods, crumbs, fine crumbs) cover the ground more than once; small ones rest on large. Outlines carry three harmonics and a tilted facet, so clods are not domes. A lump under a texel and a half becomes mottling instead of vanishing. |
| Clods shadow each other | Light is marched across the height field toward the sun with a penumbra that widens with distance. Two blurred copies of the relief darken crevices and lift tops. |
| Damp soil is about half as bright | Moisture, plus blotches where it has dried unevenly, plus extra damp in hollows and cracks, darkens the soil and enriches its colour; very wet soil glints, a few crumbs at a time. |
| Each clod is its own shade | Clods vary in value and warmth; `subsoil` turns up paler, yellower clay lumps. |
| Rows and rake marks | Furrows are ridges with narrower troughs, wandering slightly; rake tines are shallow grooves that lift off in places and ride over stones. Both are fitted to whole periods so the picture still repeats. |
| Stones and grit | Pebbles in five stone colours (flint, sandstone, ochre, slate, quartz), sunk deeper in trodden ground, polished; grit is single grains, mostly the soil's own mineral colour. Things smaller than a texel are drawn fewer, so the share of ground they cover stays true at any resolution. |
| Organic matter | Straw (pale, slightly bent, glossy), twigs (bark brown to grey, with side shoots), leaves (pointed ovals with a midrib and curled edges, from the season's colour to brown). |
| A dry crust | Two levels of Voronoi plates: wide cracks round large plates, finer ones that split them and run out; crack width swells and narrows, plate rims curl up. The lattice is shifted by the seed so no line of the picture favours a crack. |
| Frost | Crystals on what faces the sky, thickest on convex edges; the soil shows through and the hollows stay dark; a few sparkle. |
| Snow | Fills hollows first, then caps what faces up; steep clod sides stay bare. Its shading uses a smoothed relief and blue shadows. |

## Interface (`src/soil.hpp`)

| Piece | Role |
|---|---|
| `Parameters` | Scale (`texels_per_metre`: every size is in metres, so one set of parameters makes the same ground at any resolution), `colour`, `moisture`, `moisture_patches`, `tilth` (clod size), `looseness` (worked to trodden), `relief`, `subsoil`; furrow and rake angle, spacing and depth; `pebbles`, `grit`, `straw`, `twigs`, `leaves`, `leaf_colour`; `cracks`, `frost`, `snow`; the baked light and the viewer's direction; `seed`. Out-of-range values are clamped and non-numbers replaced. |
| `preset(Preset)` | Ten starting points: tilled loam, raked bed, furrowed rows, compacted path, dry crust, damp and dark, leaf litter, frosted, snow dusted, fresh spoil. |
| `apply_season(Parameters&, Season)` | Moves a ground toward spring, summer, autumn or winter, keeping its colour, scale and rows. |
| `generate_surface(parameters, width, height, out)` | A picture that repeats seamlessly in both directions, any size from 4 to 4096 texels a side. |
| `generate_spoil(parameters, Spoil, width, height, out)` | A heap round a hole, centred in the picture, with alpha: a raised ring at the lip, a fan thrown to one side that thins to scattered crumbs, a trodden threshold where the fan leaves the hole, and the heap's soft shadow on the ground as partly transparent dark pixels. The hole itself is left clear for the caller to draw. |
| `Image` | Straight-alpha sRGB bytes (red, green, blue, alpha), and the relief in metres for a caller that wants to light or place things. |

The light is baked in. A flat, open, dry patch comes out at `colour`, so a game keeps
its palette; draw the picture unlit. The default light comes from the upper left of
the picture, as people expect a picture to be lit; Catching Thieves' hedges cast
their shadows the same way.

Generation is deterministic: the same parameters and seed give the same bytes, on
every platform (no fast-math; `-ffp-contract=off`). Failure leaves `out` untouched.

## Cost

Generation takes milliseconds, so make a picture once per scene and size and keep
it; never per frame. Measured on the M4 Mini (Release; `soil_tests` prints these):

| Picture | Time |
|---|---|
| 128 × 128 surface, autumn rows, about 25 texels a metre | 3 ms |
| 256 × 256, about 50 texels a metre | 14 ms |
| 512 × 512, about 100 texels a metre | 68 ms |
| A dry crust adds two Voronoi layers | about 30 ms at 512 × 512 |

Catching Thieves paints its bed, four spoil fans and a pit wall in 8–20 ms at the
smallest window and 30–60 ms at 1100 × 760, once per season and size of cell;
its frames cost the same as before (within 0.2 ms of 2.5–4 ms).

## Seeing it

`soil_swatches sheet.ppm [texels_per_metre] [side] [zoom]` writes every preset, one
ground through the seasons, and five spoil heaps on one sheet, with each picture's
time. `soil_swatches sheet.ppm 55 96 3` shows them as a game draws them, pixel by
pixel.

## Tests (`tests/soil_tests.cpp`, CTest `soil_engine`)

Determinism of every preset, and a different seed giving different ground; seams
across eight seeds of six presets, in colour and relief; the spoil heap's ring,
clear hole and fan direction; moisture darkening, snow brightening, flat ground
keeping its colour and resolution keeping the tone; refusal of bad sizes leaving the
picture alone and clamping of bad parameters; and the cost at three sizes.

## Limits

- The light is baked: a picture lit for one sun looks wrong under another. Make one
  per light.
- Tiny features at low resolution are statistical: a pebble smaller than a texel is
  a one-texel speck drawn less often.
- The Voronoi crust uses one point per lattice cell; across many seeds plates are
  very slightly more regular than a true random pattern.
