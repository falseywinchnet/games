# The paper engine

A painter of paper for anything printed on in PlaySuite: the help document and the
Settings screen are the first two sheets, and any game that shows a page, a card, a
label, a notebook or a wrapped parcel can declare `"engines": ["paper"]` and link
`paper_core`.

## Why it looks like paper

Studied from book, laid, Bible, parchment, cotton rag and kraft papers, and from the
decisions of the Pocket Bible interview (a private sibling project that chose its page
material, ink and night mode in detail). The interview's choices shaped the defaults:
the paper is *quiet* (you notice it only when you look for it), there is no
show-through, foxing or ageing, the light is fixed and baked once, and the picture is
"pregenerated paper with minor noise displacement": layered value noise plus analytic
fibres, which is instant, rather than the felt engine's traced fibres, which take a
second a tile.

| In the sheet | How it is made |
|---|---|
| The pulp settled unevenly | Three octaves of value noise, `mottle_size` points across, move the tone by up to `mottle` of full scale. |
| The surface has tooth | A finer noise is a relief; its slope, lit from the upper left, shifts each pixel by about `tooth`. The relief is sampled two pixels apart, so the tooth stays the same size in points at every display scale. |
| Grain | A per-pixel speckle of `grain`. |
| Fibres | Short quadratic curves, `fibres` of them per 10,000 square points, half lighter than the sheet and half darker, each a little under a point wide. Where strokes cross, the stronger shows; a stroke never piles up on itself. |
| Laid paper | Fine wire lines `laid_spacing` points apart down the sheet and wider chain lines `chain_spacing` apart across it, each a soft pulse `laid_depth` deep. |
| Parchment | Broad patches (`blotch`) of darkening, balanced so the sheet keeps its colour on average. |
| Darkening warms | With `warmth` at 1 a darker patch keeps its reds and gives up blues, as pigment in paper does; at 0 it darkens in grey. |
| A bound page | A sheet may carry a shadow into the gutter at either side and a faint fall-off toward every edge (`Shading`). |

Everything is sized in points. Give `pixels_per_point` the window's scale and the same
parameters make the same paper, feature for feature, at 1x and 2x.

## Interface (`src/paper.hpp`)

| Piece | Role |
|---|---|
| `Parameters` | `pixels_per_point`; `colour` (a flat, open patch comes out exactly at it); `mottle`, `mottle_size`; `tooth`, `tooth_size`; `grain`; `fibres`, `fibre_length`, `fibre_contrast`; `laid_spacing`, `chain_spacing`, `laid_depth`; `blotch`, `blotch_size`; `warmth`; `seed`. Out-of-range values are clamped and non-numbers replaced. |
| `preset(Preset)` | Seven starting points: `wove` (the usual cream book paper), `laid`, `india` (thin, smooth, slightly grey Bible paper), `parchment`, `rag` (bright cotton with tooth), `kraft` (brown wrapping paper), `night` (dark warm paper for light ink). |
| `generate_tile(parameters, width, height, out)` | A picture that repeats seamlessly in both directions, 4 to 4096 pixels a side. Mottle, tooth and lines are fitted to whole periods across it. |
| `generate_sheet(parameters, width, height, shading, out)` | One sheet of any size, not repeating, with a `Gutter` shadow (`gutter_width`, `gutter_depth`) and an edge fall-off (`edge_width`, `edge_depth`). |
| `Image` | Straight-alpha sRGB bytes (red, green, blue, alpha), alpha always 255. |

Generation is deterministic: the same parameters and seed give the same bytes on every
platform (integer hashing, no fast-math, `-ffp-contract=off`). Failure leaves `out`
untouched.

## Cost

Measured on an M-series laptop (Release; `paper_tests` prints these):

| Picture | Time |
|---|---|
| 256 × 256 tile at 1x | about 1 ms |
| 512 × 512 tile at 2x (a 256-point tile on a Retina display) | 2 to 7 ms |
| 1024 × 1024 tile | about 10 ms |

Make a tile once per display scale and keep it; a sheet of any size is then one
pattern fill. The shell does exactly that (`src/paper_sheet.*`): a 256-point wove tile
in the help document's cream, made when the help book or the Settings sheet attaches to
a window and drawn with the painter's `fill_image_pattern`. Nothing is generated per
frame.

## Seeing it

`paper_swatches sheet.ppm [side] [pixels_per_point] [zoom]` writes every preset as a
tile shown twice across (so the seam can be judged), then a bound sheet with a gutter
and edge shading, with each picture's time. `paper_swatches sheet.ppm 96 1 4` shows the
fibres and tooth pixel by pixel.

## Tests (`tests/paper_tests.cpp`, CTest `paper_engine`)

Determinism of every preset and a different seed giving different paper; opacity and
size; a sheet with every amount at zero being exactly its colour; every preset keeping
its colour on average, textured but quiet, with India paper smoother than rag; seams
across four seeds of every preset; what each layer does alone (mottle slow, grain fine,
fibres sparse and two-toned, tooth zero on average, laid and chain lines repeating at
their spacing, blotches balanced, warmth sparing the blue channel); the same paper at
2x; gutter and edge shading at both scales; refusal of bad sizes leaving the picture
alone and clamping of bad parameters; and the cost at three sizes.

## Limits

- Fibres are strokes on the surface, not fibres in the sheet: there is no
  show-through, no translucency and no edge deckle.
- The light is baked from the upper left, which is how people expect a page to be lit;
  a page lit from elsewhere needs its tooth drawn by the caller.
- Text is the toolkit's business. Dark text blended in gamma-encoded colour over light
  textured paper can look a little thinner than with linear-light blending; the request
  for that option is in `docs/TOOLKIT_REQUESTS.md`.
