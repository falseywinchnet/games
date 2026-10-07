# Mowing

The second ambient scene, after Stillwater: a garden seen from straight above while
Eggy mows it on a ride-on mower. Built on two shared engines: `shared/ambient`
(the scene host: governed clock, pause, detail, help, sound beds) and
`shared/coverage` (the mower's brain).

## What it is

A 16 x 10 m lawn behind a brick edge and a paved terrace, with two to four flower
beds, sometimes a birdbath, rings of mushrooms, dandelions (yellow, and white
clocks), and patches where clover and crabgrass have woven into the turf. The
grass starts long. The mower works the whole garden on its own, in stripes, then
admires the job; the grass grows back in a new garden and it starts again. One
garden takes about eight minutes.

It is a scene, not a puzzle: there is no score and no failure. It belongs on the
shelf for the collection's fourth purpose, and for the small pleasures of watching
stripes appear and a job get finished.

## The mower's mind

`shared/coverage` is a robot-mower brain. It knows only the lawn's boundary
(as a perimeter wire would tell it). A simulated lidar looks ahead in a 120-degree
fan for 1.8 m and builds a belief map; beds and the birdbath are discovered as
they come into view. A planner sweeps lanes one deck-width apart (the stripes),
backtracks to the nearest unswept lane when boxed in, then cleans up any patch a
collision-free pose can still reach. A bumper catches anything the lidar missed.
It drives like a zero-turn mower: pivot, then go.

## Interaction

- **Drag the mower** to take over: it follows the pointer, cutting as it goes,
  without driving into a bed. Let go and the planner carries on from there.
- **The gnome.** Every half minute to a minute and a half a garden gnome jumps up
  out of the long grass, looks round (with his "hoo"), and hides. Click him while
  he is up and he freezes mid-pose. When the mower's lidar spots him it goes
  straight for him: a shatter of porcelain, a faint shrill scream, and he is gone.
- **Mower** chooses the orange H, red T or green JD machine: different engines
  (a big single, a parallel twin, a V-twin), each with its own sound.
- Pause, Detail and Help as in every ambient scene.

Settings (paused, detail, mower) persist in `mowing_man-v1.txt`.

## Look and sound

Top-down, lit from the upper left. Long grass is drawn blade by blade (each
garden's grass is grown fresh); mown grass comes from the suite's felt generator
and is striped light and dark by the direction it was cut, as a real lawn is. Cut
edges cast the long grass's shadow and a fringe of overhanging blades. The grass
art sits behind one seam (`src/grass_art.*`, grown by the shared grass engine in `shared/grass`) so better grass can replace it
without touching anything else.

The mower's sound is synthesized as it plays: a V-twin at 3600 rpm with a
three-spindle deck, driven by the key and the grass under the deck (see
`audio_src/README.md`). Builds whose toolkit cannot play a live source use the same
voice rendered to clips. `E` turns the engine off and on.

The music is a bluegrass band making up fiddle tunes as it plays
(`src/banjo_voice.*`), under the Music master and `M`. It is arranged like an old
Flatt and Scruggs record: a five-string banjo in open G kicks off with a Scruggs
break (the melody on the roll's accents, the fifth string droning, the G lick to
close), a fiddle then bows the tune in long notes with double stops while the banjo
backs it quietly and the guitar closes the verse with a G run, and the banjo breaks
again to finish. An upright bass, the guitar's boom-chuck and a mandolin chop sit
well below. The banjo's strings are plucked delay lines driving a set of head
resonances; the fiddle is a bowed sawtooth through its body's resonances. Two other
bands are kept selectable in code (`BanjoStyle`): a gentler porch band with quiet
passages between tunes, and an open-back clawhammer. `mm_banjo_render out.wav 90 21
scruggs fiddle` renders any style, or one part alone.

## Out of scope

Edging and trimming the strip against the beds (a real mower cannot reach it
either), weather, and the gnome doing anything but looking, hiding and breaking.
