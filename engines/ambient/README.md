# The ambient scene engine

Living scenes for PlaySuite, drawn by the processor alone. No GPU is needed or
used. A scene is retained 3D geometry: fixed scenery, foliage that sways in a
current, creatures on analytic routes, rising particles, and an animated light
pattern. The engine keeps it all inside a measured processor budget and does no work
at all when paused or hidden.

Stillwater (`vendor/stillwater`) is the first scene. Later scenes are new games that
declare `"engines": ["ambient"]` and supply an archive and a look.

## How a frame is made

The picture is drawn in three retained layers that change at different rates, so the
processor redoes only what moved:

| Layer | Contents | Redrawn |
|---|---|---|
| fixed | Everything that does not move: sand, rock, wood, pebbles, ferns, and foliage that cannot move half a pixel at this size. Shaded once per pixel with material maps, normal maps, shadows and 2 × 2 supersampling. Keeps colour, depth, and for surfaces that take animated light, their world position and brightening. | When the view's size or detail changes (on a worker) |
| sway | The fixed layer plus moving foliage, bent by the current, Gouraud-shaded | On the sway cadence (8 per second preferred) |
| frame | The sway layer, the animated light on the lit pixels, then creatures (shaded per pixel) and particles | Every animation tick (24 per second preferred) |

The frame is enlarged to the device surface with nearest-pixel sampling. Each scene
pixel covers a whole number of device pixels, and a detail-specific pixel budget caps
the raster, so a full-screen window costs no more than a large one.

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
- **Foliage lighting is linearized** in the bend: each vertex is shaded at rest and
  at a small tilt once, and each update interpolates (a first-order approximation).
- **Foliage is ordered by plant**: see-through plants farthest first so blending
  layers correctly, opaque plants nearest first so hidden pixels fail the depth test.

Measured costs for Stillwater are in `vendor/stillwater/HANDOFF.md`.

## Interface

| Piece | File | Role |
|---|---|---|
| `SceneData`, `parse_scene`, `load_scene` | `src/archive.hpp` | The archive: a header and a zlib stream of tagged sections. Every count, index and float is validated; a damaged file is refused without disturbing the loaded scene. |
| `inflate_zlib`, `crc32` | `src/inflate.hpp` | Built-in decoder; no compression library. |
| `rasterize`, `Projection`, `ShadowMap` | `src/raster3d.hpp` | Fixed-point scan conversion with the top-left rule and near-plane clipping; per-pixel work is a policy chosen at compile time. |
| `Creature`, `creature_pose`, `startle`, current | `src/motion.hpp` | Routes and the current as pure functions of time. |
| `Look` | `src/stage.hpp` | What a scene supplies: background, light, fixed-surface, foliage, creature and particle shading, creature deformation. |
| `Foliage`, `build_fixed_layer`, `Stage` | `src/stage.hpp` | The three layers. `build_fixed_layer` is pure, so a view builds it on a worker. |
| `Governor` | `src/cadence.hpp` | The budget. |
| `scene_size`, `present_nearest` | `src/present.hpp` | Scene sizing per detail and enlargement. |
| `Settings` | `src/settings.hpp` | Paused and detail, remembered between sessions. |
| `AmbientView`, `SceneSetup` | `ui/ambient_view.hpp` | The PlaySuite control: loading and rebuilding off the UI thread, the governed timer, pause, detail and help commands, glass taps, sound, settings, and a standalone help card. |

A scene game's whole view is a `SceneSetup` and a constructor:

```cpp
class StillwaterView final : public ambient::AmbientView {
  public:
    StillwaterView(ambient::gf::StableId id, ambient::ViewOptions options)
        : AmbientView(std::move(id), stillwater_setup(), options) {}
};
```

`SceneSetup` names the archive below the game's asset folder, the settings file, the
ambience loop and tap sound, the help words, the look factory, the creatures'
bounds, the cadence limits and a backdrop colour shown while loading.

## Making a scene

An archive is written offline by the game's authoring script (Stillwater's is
`vendor/stillwater/scene_src/build_scene.py`). Prepare geometry for the processor:
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
- The animated light is a labeled wave approximation of caustics, not optics.
- Creature motion is analytic loops and escapes: no flocking or obstacle avoidance.

## Tests

`tests/ambient_tests.cpp` (CTest `ambient_engine`) covers the decoder (stored, fixed
and dynamic blocks, damage), exact-once coverage of shared edges, culling and
clipping, archive validation and refusal, route continuity through a startle, the
current's bound, the governor, sizing, presentation, settings, and an end-to-end
synthetic stage including reproducibility.
