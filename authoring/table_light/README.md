# The card table's light

The light on the card tables' cards (Solitaire, Spider, FreeCell, Hearts) comes from a room
traced here once. The table only looks the result up: see `shared/cards/table_light.hpp`.

## The room

The cards are 63 mm wide, which sets the scale; a table 1100 points wide is about 61 cm of
cloth. The player's eyes are 30 cm behind the table's near edge and 42 cm above it. The
room around them is about 8 by 9 m and 4 m high:

- a wall of glass ahead, beyond the table, and more glass on the left;
- the sun, 32° up and 80° left of straight ahead, coming through the side glass;
- a hazy sky, sunlit paving and pale buildings outside;
- a grid of ceiling lights;
- plaster walls, a wooden floor and the felt passing light on.

The cards are paper under a varnish: GGX microfacets, roughness 0.10, index 1.5.

`trace.cpp` path-traces the room, using only the standard library. It gives two things for
a grid of points on the cloth: the light arriving from every direction, binned by
direction with the sun's disc kept apart (`cells`), and what the varnish mirrors toward the
eyes (`coat`).

## What the game uses

Because the room never changes and the cards lie flat, what the room does to a card depends
only on where the card lies in front of the eyes. Fitted in the screen's own sRGB values, it
comes to:

    seen = face x level + gloss

The table draws this as one premultiplied overlay over each card, clipped to the card.
`export.py` traces a patch 1.2 m across and 1 m deep and writes the grid to
`shared/cards/table_light_bake.cpp`, in about ten seconds:

    python3 authoring/table_light/export.py <work dir> shared/cards/table_light_bake.cpp

The eye is taken to be adapted to the sunlit paper, so white paper stays neutral.
Exposure keeps the best-lit paper at the face's own colour. On Solitaire this overlay
matches the full render's card surfaces to within 1–4 levels on average. Black ink rises
from 56 to 65 (traced: 68), and paper is unchanged.

## The full render

`bake.py` renders a whole table under the room, from a capture of the game:

- the table drawn unlit;
- every card's exact rectangle;
- each card's height (0.6 mm, stacks shown at most four cards high);
- cards draping over the cards beneath them;
- the cut edges facing the eyes;
- the sun's shadows, with light scattering a quarter millimetre in paper and felt.

It made the comparison sheets in `review/card-lighting/`. The owner kept today's shading,
the cloth's falloff and the drawn drop shadows, and took only the card surface. So the
sides and sun shadows stay here and are not in the game.

The captures need temporary passes in the table (`PLAYSUITE_LIGHT_PASS=albedo|height`,
`PLAYSUITE_LIGHT_DUMP`) and a 2× capture switch. `capture-passes.patch` adds them to the
table as of commit b7162d6:

    bake.py <albedo.ppm> <cards.txt> <out dir> [card width in points]
