# Canopy Falls

## The game

A living scene: from a wooden walkway that crosses a lily pool out of the rainforest,
you look at one broad, continuous curtain of a great river pouring 40 m over a
basalt wall, the sun standing just above its brink. Nothing is asked of the visitor; they watch,
listen, and may touch the water, the frogs and the leaves.

## Rule 2

A new ambient scene in the Stillwater family, not a game with rules. Its decisions stay
interesting the way a real waterfall does: it never repeats (the water, the spray and
the sounds are generated as they play), the light moves with the falling water, and the
pool's frogs and the forest's birds keep their own time.

## A session

The scene opens on the walkway at once; the sound comes up over two seconds. A visit
is as long as the visitor likes. Returning mid-visit finds the scene where it was
left, the frogs on their pads.

## Replayability

What varies: the water (every sheet, drop and glint is new), the wind in the canopy,
the birds and insects, and the frogs. A Light, Balanced or Fine detail setting trades
particle counts and the scene's resolution for processor time.

## Passive mode

The scene is passive: it runs itself. Touching the pool sends rings across it; a
frog touched jumps to another pad; a leaf touched sways and sheds its drops.

## Rules

None to referee. The scene is deterministic from its seed and time: the same seed and
the same moments give the same pictures, which the tests check.

## Look, sound and words

**The place.** A small section of falls like Iguazu's, one unbroken curtain (no
outcrop splits it):
- **Rock:** dark basalt laid in flows of uneven thickness, broken into columns and
  blocks, and mottled at every scale. Rust weeps from the joints; there are lichen
  crusts and orange spots, black seeps, white mineral runs, and a green film near the
  spray.
- **River:** tea-amber where it is thin.
- **Cliff top:** broadleaf crowns, with palmito palms only on the banks, never behind
  the falls.
- **Canopy:** dark and translucent round the frame.
- **Viewpoint:** standing at an adult's height on the walkway as it crosses the pool
  diagonally. You see its deck and rails running away to the right, the steep jungle
  bank and its plants beside you, the pool with its lilies and frogs, and the whole
  waterfall beyond.

**Light.** One sun above the brink, a little to the right, so the water is lit from
behind. The sky is computed, not painted: single scattering by air (Rayleigh), by the
humid haze (Mie) and absorption by ozone, along each ray through a spherical
atmosphere, with an allowance for light scattered more than once. The sun's disc is
limb-darkened, its glare in the eye a measured spread. The crest glows gold where the
river is thin; the drops throw the sun's glints at the eye, brightest near the sun's
direction and fewer and fainter down the fall. The rock face is in its own shade, lit
by the sky and by the white water beside it, with the moving light the water throws
on it.

**Sound.** Built in layers, each synthesized as it plays, each playable alone:
the roar (the impact's rumble, the body of countless bursting bubbles rather than
shaped noise, slow surges), the near water (the pool's babble, drips from the
leaves, the lap against the walkway's posts), the forest (birds, insects, frogs, the
wind in the canopy), and the space (the cliff's echo and the forest's long tail).

**Words.** Help names the scene and the few things that respond to touch.

## Controls

Pointer: touch the pool, a frog or a leaf. Keyboard: Space pauses, D changes detail,
F1 opens help. The command bar has Pause and Help; Detail is a setting.

## Not included

- Music: the falls are the music; a score would fight the roar.
- A moving camera: the scene's fixed layers are baked once per window size; moving the
  eye would rebake them.
- Weather and time of day: one hour, done well, first.

## How it is drawn

The picture is composited at the scene's resolution from layers baked when the view's
size changes, and moving parts drawn over them each frame:

| Layer | Made | Each frame |
|---|---|---|
| Sky and sun | Baked: the atmosphere integral, the disc, the glare | Nothing |
| Canopy round the frame, forest on the rim | Baked, backlit by the sky behind each leaf | A slow sway of the near leaves |
| Rock wall | Baked: relief, basalt, moss, wet streaks, sky and sun light, and per pixel a tap into the moving light | The light thrown by the water, from the tap |
| The falls | Their geometry and flow time per pixel, baked | The sheet: streaks moving at the water's own speed, coherent glass to white froth; then the particles |
| Pool | Baked: what lies under the water and the scene mirrored above it | The surface's waves bend the mirror; rings; foam |
| Walkway | Baked | A faint shimmer of the falls' light; drops on the rail |

The particle engine (`shared/particles`) carries the falling water's
clumps, drops and glints and the mist: pools of particles in arrays, emitters and
forces as data, drawn as streaks, soft discs and glints into a light buffer.
