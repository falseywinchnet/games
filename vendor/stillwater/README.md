# Stillwater

A living aquarium for the shelf: the first of PlaySuite's ambient scenes, and the
first game on the shared ambient engine (`engines/ambient`).

## What it is

A planted tank seen through the front glass. Tall grass leans and ripples in a slow
current; a mossy driftwood branch arches across the middle; dark boulders, gravel
and pebbles sit on pale sand where light from the surface ripples. Sixteen silver
tetras with blue sheen and red fins swim slow loops at different depths; two crabs
shuffle on the sand; an air stone sends up a thin column of bubbles.

There are no rules, no score and nothing to lose. It serves the collection's fourth
purpose more purely than any game on the shelf: something pleasant to glance at
beside real work, that asks for nothing and can be left at any moment.

## Interaction

- **Tap the glass** (click): fish within reach of the click dart away from it, ease
  out over about a second, and settle into a new loop where they landed. Crabs
  shuffle a little sideways. A soft glass knock plays, panned to the click.
- **Pause / Resume** (Space or the capsule's primary command): the tank stops on the
  exact frame and costs nothing until resumed. Resuming continues from the same
  moment.
- **Detail** (D or the capsule): Light, Balanced (default) or Fine, the size of the
  scene's pixels and the processor budget.
- **Help** (F1 or H).

Settings (paused, detail) persist in `stillwater-v1.txt`. There is no progress to
save; the fish start their loops afresh each session.

## Look and sound

The scene is Riverscape from Desktop Habitats (Chase Lean, MIT), as translated into
the native Stillwater aquarium (github.com/falseywinchnet/stillwater, commit 4a80ee3):
the same geometry, layout, fish anatomy, strand motion and material formulas,
re-implemented for the processor. Sand, rock and wood use Poly Haven's CC0 maps.

House look applies: the scene is drawn at one pixel per two points (Balanced) and
enlarged exactly, like the collection's other 3D games. Fine draws smaller pixels.

Sound: a 64-second seamless ambience (pump hum, moving water, an air stone's small
bubbles), quiet beside the collection's music, and a glass tap. Both are
synthesized (`audio_src/make_audio.py`); the ambience follows the Music master, the
tap the Sound master. A paused tank is silent.

## Processor, not graphics card

The target machines may have no GPU. The original renders ~890,000 triangles a
frame with Metal; this port keeps the picture by doing less, not by drawing less:

- the fixed scenery (~330,000 triangles after decimation) is shaded once per window
  size, supersampled, with shadows and material maps;
- foliage ribbons are thinned along their length offline (502,000 → 126,000
  triangles); foliage that cannot move half a pixel at the current size is drawn
  once with the fixed scenery; the rest is redrawn about eight times a second;
- fish are decimated (11,916 → 1,500 body triangles) and shaded per pixel;
- the engine measures its own cost and holds the tank within a processor budget,
  half of it while another window is in front, none while paused or hidden.

## Out of scope

Feeding, fish behaviour beyond loops and escapes, moving the camera, editing the
tank, and desktop-wallpaper placement (Stillwater's desktop mode) are not part of
this game.
