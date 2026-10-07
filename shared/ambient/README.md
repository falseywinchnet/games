# The ambient scene engine

Living scenes for PlaySuite, drawn by the processor alone. No GPU is needed or
used. A scene is retained 3D geometry: fixed scenery, foliage that sways in a
current, creatures on analytic routes, rising particles, and an animated light
pattern. The engine keeps it all inside a measured processor budget and does no work
at all when paused or hidden.

Stillwater (`games/stillwater`) is the first scene. Later scenes are new games that
declare `"engines": ["ambient"]` and supply an archive and a look.

## How a frame is made

The picture is drawn in three retained layers that change at different rates, so the
processor redoes only what moved:

| Layer | Contents | Redrawn |
|---|---|---|
| fixed | Everything that does not move: sand, rock, wood, pebbles, ferns, and foliage that cannot move half a pixel at this size. Shaded once per pixel with material maps, normal maps, shadows and 2 × 2 supersampling. Keeps colour, depth, and for surfaces that take animated light, their world position and brightening. | When the view's size or detail changes (on a worker) |
| sway | The fixed layer plus moving foliage, bent by the current, Gouraud-shaded. Two buffers: one is shown while the other is rebuilt on a worker | On the sway cadence (8 per second preferred), beside the frames |
| frame | The sway layer, the caustics (or simpler animated light) on the lit pixels, then creatures (shaded per pixel, caustics included) and particles | Every animation tick (24 per second preferred) |

The frame is enlarged to the device surface with nearest-pixel sampling. Each scene
pixel covers a whole number of device pixels, and a detail-specific pixel budget caps
the raster, so a full-screen window costs no more than a large one.

## Caustics

`CausticField` (`src/caustics.hpp`) is the light a rippling surface focuses onto
everything below it. When a look is created it traces the pattern once: a tileable
height field of fourteen travelling waves (integer wave vectors, so the tile wraps;
whole cycles per loop, so time wraps), a grid of photons bent by its slope and
gathered where they land. Where the surface focuses them they pile up into the
branching web. 32 steps through one loop are kept as bytes, sharp and blurred
(128 x 128 texels, 1 MB, about 20 ms).

Each lit surface point is reduced once, when its layer is built, to a *tap*: the
texel straight up the light from it (so slanted light shifts the pattern with
height), its sharpness (crisp just under the surface, softer toward the floor) and
its gain (dimmer deeper). A frame mixes the tap's sharp and soft values between the
two stored steps around the current time: two table reads and a little integer
arithmetic per lit pixel. The pattern moves because the waves move; nothing scrolls.
Fixed surfaces get their taps once per layer; swaying leaves interpolate texel
positions across each triangle on every sway update and take only the soft light
(thin blades seen edge-on would turn sharp lines into dashes); creatures read the
field per pixel (`ActorSample::light`). A look opts in by returning its field from
`Look::caustics()` and giving leaves a `SwayShade::light_boost`; a look without one
keeps the older `animated_light`.

## Several scenes

A Diorama may offer several scenes (`DioramaScene`: an id, a name, an archive, a
look and dressing, bounds), passed as `SceneSetup::scenes`. The Scene command (S)
moves to the next; the choice is saved as the `scene` setting, and a settings file
without one opens the first. The next scene loads and builds its first fixed layer on
workers while the shown one keeps playing and dims (0.45 s); once dark and ready they
swap and the new one brightens (0.6 s). The fade runs on the wall clock and keeps the
view ticking (`SceneContext::redraw`), so it also completes while paused. Pressing
again during a change redirects it. Dev scripts can say `action scene <id>`.

## Staying cheap

- **Paused, hidden or occluded means stopped.** Pause stops the timer; the last
  frame stays on screen. Behind the shelf there are no timer callbacks, frames or
  sound.
- **A governor holds the budget.** `Governor` measures the time each frame and each
  sway update takes and lowers the rates, foliage first, so the total stays within
  the scene's budget (Stillwater: 12 % of one core; Light detail halves it, Fine
  doubles it). Floors keep motion fluid: 12 frames and 3 sway updates a second.
- **An inactive window halves the budget** and the preferred rates.
- **Reduced motion** holds the foliage and the light still; creatures keep swimming.
- **Hidden foliage is dropped per size.** With each fixed layer, foliage that the
  fixed scenery hides wherever the current can carry it (or that stays off the
  picture) is left out of the sway updates; the picture is identical.
- **Frames never wait for the grass.** Sway updates after the first run on a worker
  into the hidden sway buffer and are shown with the next frame, so a frame costs
  the same whether or not the foliage moved.
- **Foliage lighting is linearized** in the bend: each vertex is shaded at rest and
  at a small tilt once, and each update interpolates (a first-order approximation).
- **Foliage is ordered by plant**: see-through plants farthest first so blending
  layers correctly, opaque plants nearest first so hidden pixels fail the depth test.

Measured costs for Stillwater are in `games/stillwater/HANDOFF.md`.

## Interface

| Piece | File | Role |
|---|---|---|
| `SceneData`, `parse_scene`, `load_scene` | `src/archive.hpp` | The archive: a header and a zlib stream of tagged sections. Every count, index and float is validated; a damaged file is refused without disturbing the loaded scene. |
| `inflate_zlib`, `crc32` | `src/inflate.hpp` | Built-in decoder; no compression library. |
| `rasterize`, `Projection`, `ShadowMap` | `src/raster3d.hpp` | Fixed-point scan conversion with the top-left rule and near-plane clipping; per-pixel work is a policy chosen at compile time. |
| `Creature`, `creature_pose`, `startle`, current | `src/motion.hpp` | Routes and the current as pure functions of time. |
| `CausticField` | `src/caustics.hpp` | The traced, looping caustic pattern and its per-point taps. |
| `Look` | `src/stage.hpp` | What a scene supplies: background, light, fixed-surface, foliage, creature and particle shading, creature deformation. |
| `Foliage`, `build_fixed_layer`, `Stage` | `src/stage.hpp` | The three layers. `build_fixed_layer` is pure, so a view builds it on a worker. |
| `Governor` | `src/cadence.hpp` | The budget. |
| `scene_size`, `present_nearest` | `src/present.hpp` | Scene sizing per detail and enlargement. |
| `Settings` | `src/settings.hpp` | Paused and detail, remembered between sessions. |
| `AmbientView`, `SceneSetup` | `ui/ambient_view.hpp` | The PlaySuite control: loading and rebuilding off the UI thread, the governed timer, pause, detail, scene and help commands, glass taps, sound, settings, and a standalone help card. |

A scene game's whole view is a `SceneSetup` and a constructor:

```cpp
class StillwaterView final : public ambient::AmbientView {
  public:
    StillwaterView(ambient::gf::StableId id, ambient::ViewOptions options)
        : AmbientView(std::move(id), stillwater_setup(), options) {}
};
```

`SceneSetup` names the archive below the game's asset folder, the settings file, the
ambience (a sound bed, under the Sound master), music (under the Music master), the
tap sound, the help words, the look factory, an optional dressing that adds the
game's own props to the archive as it loads, the creatures' bounds, the cadence
limits and a backdrop colour shown while loading. Where the toolkit plays
generators, `live_ambience` and `live_music` name voices synthesized as they play
(`SoundDesk::live` and `SoundDesk::live_music`); the clips are the fallback. The
live ambience is a `SceneVoice`: it hears which scene is shown (`show_scene`) and may
play the taps on the glass itself (`tap` returns true, and the tap clip is skipped).
Both calls come from the UI thread, so a voice hands them to its audio thread
without locking.

Set `GAMES_SCENE_TRACE` to have a scene print its frame pacing (interval, spread,
late frames), drawing and presentation times and missed surface leases when it
closes.

## Making a scene

An archive is written offline by the game's authoring script (Stillwater's is
`games/stillwater/scene_src/build_scene.py`). Prepare geometry for the processor:
decimate meshes to what the scene raster can show, thin foliage ribbons along their
length, and downsample material maps. Sections:

| Tag | Contents |
|---|---|
| `CAMR` | Eye, target, vertical field of view, near and far planes, light direction |
| `TEXT` | Power-of-two RGB material maps (albedo or OpenGL normal) |
| `STAT` | Fixed meshes in their own space, and placements: transform, tint, uv scale, material |
| `SWAY` | Foliage roots, and one world-space mesh whose vertices carry bend direction, compliance, ribbon axis and distance from the root |
| `AMSH` / `APRT` / `ACTR` | Creature meshes, their parts (transform, tint, material, joint) and creatures (route centre and scale, cruise, kind) |
| `RISE` | Rising particles |

Material numbers mean whatever the scene's `Look` says. The record layouts are in
`src/archive.hpp`.

## Limits

- The camera is fixed per scene; moving it would rebuild the fixed layer.
- Shadows of moving things are not drawn; foliage shadows are cast at rest.
- Foliage translucency is order-dependent blending per plant, an approximation of
  the coverage-based transparency a GPU would use.
- Caustics are traced through one flat surface plane along the light's slant; things
  in the light's path dim them only through the shadow map (a shadowed point takes
  none), and the pattern repeats every tile (turned 27 degrees to the view to hide it).
- Creature motion is analytic loops and escapes: no flocking or obstacle avoidance.

## Tests

`tests/ambient_tests.cpp` (CTest `ambient_engine`) covers the decoder (stored, fixed
and dynamic blocks, damage), exact-once coverage of shared edges, culling and
clipping, archive validation and refusal, route continuity through a startle, the
current's bound, the governor, sizing, presentation, settings, and an end-to-end
synthetic stage including reproducibility, and caustics (valid sizes, range,
looping, sharper near the surface, determinism, motion on a stage).
