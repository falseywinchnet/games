# Specimen Cabinet: game record

## VERIFIED

- Rules (`ctest -L design`, `specimencabinet_rules`): every puzzle of 4, 5 and 7 is a
  Graeco-Latin square with exactly one answer (4: 4-5 clues, 5: 6-7, 7: 13, slowest
  generation 0.1 s on the M4); play, swaps, the free felt, pinned sockets, the pile's spots,
  the win and records; the save's round trip and damaged saves refused; the layout at
  every size from 600 x 370 up, its panel buttons included.
- On the M4 (`.build/app-dogfood`, the full application): every target built,
  `ctest -LE design` 54 of 54 passed, `specimencabinet_view_contract` and `stones_engine`
  passed. Scripted native runs (`--dev --script`) in the suite at 1150 x 800, 1250 x 800
  and the 600 x 420 window: the chooser, a 4, 5 and 7 cabinet, the tumbler dropping its
  stones once they are drawn, stones laid on the felt, set and refused, a stone in hand,
  the solved card. Its twelve sounds load (the prepared runtime was rebuilt for them).
- Headless previews (`sc_preview`) at scale 1, 1.5 and 2, including 600 x 370 at 2.
  `screens/` holds those inspected; `readme.jpg` is a native capture at 1250 x 800.

## NOT VERIFIED

- The sounds by ear in play: they are Tumbled Stones' (provenance in
  `audio_src/README.md`), reused, and have not been listened to in this game.
- A long session: no hour of play has been timed for idle cost. A settled table stops
  its timer except for the clock, which wakes once a second while a cabinet is open.
- Retina: the M4's display is 1x; scale 2 is checked only headless.

## DECISIONS

- The stones, their room light and the walnut are the stones engine (`shared/stones`),
  made from Tumbled Stones, which is retired (`"enabled": false`; its ids stay reserved).
- The felt is the card tables' (`games::felt_tile()`, `shared/cards/table_art`), and the
  pictures are made on that crew (`games::run_on_every_core`).
- A move is a stone set in a socket; the felt costs nothing. A record is fewest moves,
  then the shortest time. There is no 6 x 6: none exists (Tarry, 1900); help says why.
- The save (`specimen_cabinet-v1.txt`) keeps the whole square and its looks, so a 7 x 7
  needs no regeneration on load. A pile stone keeps its spot (`Where::cell`).
- Gate: the contribution gate's FAILs (`include(CTest)`, linking `stones_core` and
  `render_core`) are the in-house game pattern that Mowing Man, Stillwater and Maze also
  follow; this game is maintained here, not contributed from a fork.
