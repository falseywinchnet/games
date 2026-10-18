# The stones engine

Polished stones for the tray games: Specimen Cabinet, and Tumbled Stones before it.
`namespace stones`, library `stones_core`, built on the renderer (`shared/render`).

## What it makes

- **Shapes** (`shapes.hpp`): a family of stones is one solid with two faces, lying in
  eight orientations. Six styles: four are grown as solids and broken as real pieces are
  (`shell_volume.cpp`: a shell fragment, a mould in rock, a scalloped rock, a pottery
  shard; `wood_volume.cpp`: a chip of fossil log; `candy_volume.cpp`: a chunk of pulled
  candy; `flake_volume.cpp`: a knapped flake), meshed by surface nets in `grow_from`;
  two are drawn from an outline and two reliefs (a faceted tumble, an agate nodule).
  `Shape::make(seed, style)` is deterministic; a `parent` and `likeness` make a near
  relation, harder to tell apart. Relief too fine for the mesh is baked per face.
- **Materials** (`minerals.hpp`): twenty-four, chosen apart from the shape and printed
  through the solid (`print_mineral`), so a stone's two faces differ as a real one's do.
  Each has its colours, polish, clarity, glints and sheen.
- **Light** (`room.hpp`): the room's light, path traced once by
  `tools/bake_room.cpp` into `room_bake.cpp` (`stones_bake_room window 400 room_bake.cpp`):
  reflections by roughness, the diffuse light, the window's gathered directions.
- **Drawing** (`stone_render.hpp`): `draw_stone` renders a stone in a pose into a
  picture of what it adds and what it keeps of the wood beneath (its shade, a clear
  stone's coloured glow), on the processor; `lay_stone` lays it on any pixels. The shade
  fades before the picture's edge, so a stone can lie on felt as well as wood.
- **The tray** (`tray.hpp`): wood with sockets, lit the same way, in four woods
  (`TrayPlan::wood`: the first tray's stain, plain-sawn walnut boards, quartersawn,
  figured walnut).

## Cost

A stone's picture takes about 20 ms at a socket's size on one core, a grown solid
0.1 to 0.4 s. Make them on a `gui_forms::Worker` and keep them; never on the UI thread.

## Tests

`tests/stones_tests.cpp`: every style grows a distinct, deterministic solid, every
material prints, a drawn stone covers pixels and shades the wood round it, and the
shade is gone by the picture's edge.
