# The grass engine

Whole-lawn pictures grown blade by blade on the processor, for games whose ground is
grass. It came from Mowing, whose lawn it draws, and now also grows the field round
the garden in Catching Thieves.

## What it makes

| Look | Picture |
|---|---|
| `tall` | Uncut grass with the scene's wildflowers and mushrooms grown into it |
| `mown_dark`, `mown_light`, `mown_quarter`, `mown_three_quarter` | Short turf laid towards, across and away from the sun, as a mower leaves it |
| `mulch`, `beds` | Shredded bark, bare or with the scene's bedding plants growing in it |
| `render_tree` | A tree's canopy as a cut-out seen from the front and above |

Each picture covers the asked-for metres at the asked-for pixels per metre, with no
repeats. A `LawnScene` says where the grass must stop (a distance grid; it thins into a
bare strip there) and where flowers and bed plants grow. `LawnWeather` sets the time of
year: `fresh` spring growth (lighter, yellower), `dry` late-summer straw, and `frost`
(rime on the blades, snow on what faces the sky and on the ground). All zero is the
early-summer lawn Mowing has always drawn, bit for bit.

## How

1. A kit of ready-drawn pieces is made once per look, scale and weather: a few hundred
   plants, flowers or bark pieces, each built from round segments into a height
   buffer and lit (thin pigmented leaves, matte petals, bark).
2. A field is then stamping: each plant is a "keep the higher sample" copy of a kit piece.
3. Shade from one plant on the next comes from a coarse map of leaf cover and a few
   looks towards the sun.

## Interface

`src/lawn.hpp`: `Layer`, `Cutout`, `sample_clamped`, `LawnScene`, `LawnFlower`,
`LawnPlant`, `LawnLook`, `LawnWeather`, `render_lawn`, `render_tree`. Pictures are
0xAARRGGBB, row-major, the top row first; scene positions are metres from the top-left.

## Costs and limits

- Generation uses every core inside one call (a fork-join pool; tiles share only
  read-only maps, so the picture does not depend on the number of cores). A game calls
  it from a worker, never from a frame, and keeps the result.
- The first kit for a look takes some tenths of a second; a 1088 x 1088 field about
  0.15 s on an M-series laptop. Kits stay in memory for the process (a few MB each).
- Drawing the picture is the game's business; the engine does no work after it returns.
- There is no cancellation inside one call; games check before and after.

`tests/grass_tests.cpp` checks sizes, opacity, determinism, that no weather is the
default exactly, what each weather does to colour, flower and bare-strip placement,
sampling and canopies.
