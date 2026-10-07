# Stillwater

A living aquarium for the shelf: the first of PlaySuite's ambient scenes, and the
first game on the shared ambient engine (`shared/ambient`).

## What it is

A planted tank seen through the front glass. Tall grass leans and ripples in a slow
current; a mossy driftwood branch arches across the middle; dark boulders, gravel
and pebbles sit on pale sand where light from the surface ripples. Sixteen silver
tetras with blue sheen and red fins swim slow loops at different depths; two crabs
shuffle on the sand; an air stone sends up a thin column of bubbles. A little
treasure chest lies half sunk in the sand, its lid ajar over a heap of gold that
glints as the light ripples across it, and now and then a bubble escapes from it.
Sunlight through the rippling surface throws caustics, a slowly moving, branching
net of bright lines, over the sand, the stones, the leaves and the fish: crisp near
the surface, softer and dimmer on the floor.

Two more tanks can be chosen with the Scene command:

- **Reef**: white coral sand under a wall of live rock crusted pink and purple with
  coralline algae, and two bommies with a channel of sand between them running back
  from the glass. Table corals stand out from the rock at several heights over
  staghorn and bushy thickets, finger, brain and plate corals, leather toadstools,
  mushroom discs, zoanthid mats and tube sponges, with lace sea fans behind and
  rubble along the channel. A cloud of small green chromis and a school of lyretail
  anthias hang over the reef, a few yellow and blue tangs graze along it, clownfish
  keep to their three anemones, and a red hermit crab walks by the chest. Sunbeams
  slant down through the blue water.
- **River pool**: dim, tea-coloured water under tree roots reaching down from the
  bank, a far bank of stones rising out of the murk, sunbeams through the brown
  water, sunken branches, smooth stones, gravel and leaf litter, long eelgrass in the
  current; a shoal of olive minnows keeping together, a few bronze rudd with red fins,
  darters on the bottom and a dull crab under a branch.

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
- **Detail** (Q or the capsule): Light, Balanced (default) or Fine, the size of the
  scene's pixels and the processor budget.
- **Scene** (S or the capsule): the planted tank (default), the reef or the river
  pool, in turn. The tank dims to dark water while the next one is prepared on a
  worker, then brightens; this works while paused too.
- **Help** (F1 or H).

Settings (paused, detail, scene) persist in `stillwater-v1.txt`; a file from before
there was a choice opens the planted tank. There is no progress to
save; the fish start their loops afresh each session.

## Look and sound

The scene is Riverscape from Desktop Habitats (Chase Lean, MIT), as translated into
the native Stillwater aquarium (github.com/falseywinchnet/stillwater, commit 4a80ee3):
the same geometry, layout, fish anatomy, strand motion and material formulas,
re-implemented for the processor. Sand, rock and wood use Poly Haven's CC0 maps.

House look applies: the scene is drawn at one pixel per two points (Balanced) and
enlarged exactly, like the collection's other 3D games. Fine draws smaller pixels.

Sound is the tank itself, synthesized as it plays (`src/tank_voice.cpp`) to the
native Stillwater's recipe: the filter motor's quiet 60 Hz hum, strongest at 120 Hz
and stirred by mechanical noise; a very quiet, continuous band of return water; and
the air stone's bubbles, each a short ring at the Minnaert pitch of its size, formed
at random moments whose pace slowly wanders, sounding from where the column rises.
Each tank has its own mix: the reef's stronger circulation; in the river pool a far
filter, the current and the odd larger bubble of gas from the silt. A tap on the glass
is a dull fingertip, placed where the glass was touched and a little different each
time. All of it is faint, under the Sound master; it stops while paused or hidden.

Music is an island band on an old library record, making up lazy Hawaiian tunes as
it plays (`src/island_voice.cpp`): a lap steel sliding into the melody (sometimes in
sixths), a ukulele strumming the island rhythm, an upright bass, vibraphone and
marimba answers, a shaker and a few bubbles, in a swung 4/4 on sixth and seventh
chords joined by the II7-V7 vamp, through a spring reverb and a worn tape. It follows the
Music master and M. Where the toolkit cannot play live voices, the same recipes are
played from clips the same code renders (`audio_src/make_audio.py`: 64 seconds of the
planted tank's water, one tap, and 96 seconds of the band). A paused tank is silent.

## Processor, not graphics card

The target machines may have no GPU. The original renders ~890,000 triangles a
frame with Metal; this port keeps the picture by doing less, not by drawing less:

- the fixed scenery (~330,000 triangles after decimation) is shaded once per window
  size, supersampled, with shadows and material maps;
- foliage ribbons are thinned along their length offline (502,000 → 126,000
  triangles); foliage that cannot move half a pixel at the current size is drawn
  once with the fixed scenery; the rest is redrawn about eight times a second;
- fish are decimated (11,916 → 1,500 body triangles) and shaded per pixel;
- the caustics are traced once as a tank opens (about 20 ms on a worker) into a
  looping, tileable pattern of 32 steps, kept sharp and soft; a frame then reads two
  bytes per lit pixel;
- the engine measures its own cost and holds the tank within a processor budget,
  half of it while another window is in front, none while paused or hidden.

## Out of scope

Feeding, opening the chest, fish behaviour beyond loops and escapes, moving the camera, editing the
tank, and desktop-wallpaper placement (Stillwater's desktop mode) are not part of
this game.
