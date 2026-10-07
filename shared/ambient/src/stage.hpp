#pragma once
// The retained ambient pipeline. A scene is drawn in three layers that change at
// different rates, so the processor redoes only what moved:
//
//   fixed   sand, rock, wood and every other unmoving surface, shaded once per
//           size with shadows, material maps and supersampling. Keeps colour,
//           depth, and for surfaces that take animated light, their world xz and
//           the display-space brightening at full strength.
//   sway    the fixed layer plus foliage bent by the current, lit per vertex.
//           Rebuilt on the sway cadence (about ten times a second).
//   frame   the sway layer plus the animated light pattern, then creatures and
//           rising particles shaded per pixel. Rebuilt every animation tick.
//
// Shading decisions belong to the scene's Look; buffers, order and cost belong here.
#include "ambient_math.hpp"
#include "archive.hpp"
#include "caustics.hpp"
#include "motion.hpp"
#include "raster3d.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace ambient {

// Display colour channels in 0..1 (already tone mapped and encoded).
using Rgb = Vec3;

// A fixed-surface sample, everything the Look needs to shade it once.
struct FixedSample {
    Vec3 world{};
    Vec3 normal{};     // geometric, facing the camera
    Vec3 tangent{};    // world direction of increasing u (unnormalized, zero when degenerate)
    Vec3 bitangent{};  // world direction of increasing v
    float u{};
    float v{};
    float texture_lod{};  // log2 texels per pixel along the surface
    Rgb albedo{};         // linear
    float moss{};
    std::uint32_t material{};
    float light_visibility{};  // 0 shadowed .. 1 lit, from the shadow map
    bool front{};
};
struct FixedShade {
    Rgb color{};        // display colour with the animated light off
    Rgb light_boost{};  // display colour added at full animated light (zero when none)
};

struct SwaySample {
    Vec3 world{};
    Vec3 normal{};  // already bent by the current
    Rgb albedo{};   // linear
    float translucency{};
    float light_visibility{};
    float u{};  // across the leaf, 0..1
};
struct SwayShade {
    Rgb front{};
    Rgb back{};
    float alpha{1};
    Rgb light_boost{};  // display colour added at full caustic light (zero when none)
};

struct ActorSample {
    Vec3 world{};
    Vec3 normal{};  // facing the camera
    Vec3 local{};   // deformed position in the mesh's own space
    float u{};
    float v{};
    float part{};
    float fin{};
    Rgb tint{};
    std::uint32_t material{};
    float screen_v{};  // 0 top .. 1 bottom of the view
    float light{};     // caustic light strength here, 0..1 (zero when the look has none)
};

class Look {
  public:
    virtual ~Look() = default;
    // Display colour where no surface is drawn; u, v are normalized view coordinates.
    [[nodiscard]] virtual Rgb background(float u, float v) const = 0;
    // The light the shadow map is rendered from.
    [[nodiscard]] virtual LightFrame light_frame() const = 0;
    [[nodiscard]] virtual FixedShade shade_fixed(const FixedSample& sample, const SceneData& scene) const = 0;
    // Strength 0..1 of the animated light at a world position and time. Used only
    // when the look has no caustic field.
    [[nodiscard]] virtual float animated_light(float x, float z, float time) const = 0;
    // The caustic pattern lighting this scene, or null for the simpler animated light
    // above. With one, foliage (SwayShade::light_boost) and creatures
    // (ActorSample::light) take the light as well as fixed surfaces.
    [[nodiscard]] virtual const CausticField* caustics() const {
        return nullptr;
    }
    [[nodiscard]] virtual SwayShade shade_sway(const SwaySample& sample) const = 0;
    // Material-specific motion of a creature vertex in mesh space; may tilt the normal.
    virtual void deform_actor(std::uint32_t material, const ActorVertex& vertex, float creature, float time,
                              Vec3& position, Vec3& normal) const = 0;
    // Back faces of these materials are never drawn.
    [[nodiscard]] virtual bool closed_actor(std::uint32_t material) const = 0;
    // Returns the display colour and coverage (alpha) of a creature pixel.
    [[nodiscard]] virtual Rgb shade_actor(const ActorSample& sample, float& alpha) const = 0;
    [[nodiscard]] virtual Rgb shade_riser(Vec3 world, Vec3 normal, float screen_v) const = 0;
    // Material bends of rigid creature parts (legs bobbing): world-space y offset.
    [[nodiscard]] virtual float joint_lift(float joint, float joint_phase, float time) const = 0;
    // Back faces of fixed surfaces with these materials are skipped.
    [[nodiscard]] virtual bool closed_fixed(std::uint32_t material) const = 0;
};

// The retained fixed layer for one view size.
struct FixedLayer {
    int width{};
    int height{};
    std::vector<std::uint32_t> color{};      // width * height, 0x00RRGGBB display colour
    std::vector<float> depth{};              // reciprocal depth; 0 where nothing is drawn
    std::vector<std::uint32_t> boost{};      // 0x00RRGGBB animated light at full strength
    std::vector<float> light_position{};     // width * height * 3: world x, y, z where boost != 0
    std::vector<std::uint8_t> settled{};     // per foliage triangle: 1 when drawn here, at rest
    std::vector<std::uint8_t> hidden{};      // per foliage triangle: 1 when this layer hides it however it sways
};

// Foliage data prepared once per scene and read-only afterwards, so a worker
// building a fixed layer may share it with the stage.
struct Foliage {
    std::vector<CurrentRoot> phases{};     // per root, phase only
    std::vector<CurrentVertex> current{};  // per vertex current-response constants
    std::vector<SwayShade> rest{};         // per vertex shade at rest
    std::vector<SwayShade> gradient{};     // per vertex change of shade per unit bend slope
    std::vector<float> reach{};            // per vertex largest displacement, world units
    std::vector<std::uint32_t> order{};    // triangle draw order
    std::vector<std::uint32_t> boost{};    // per vertex caustic light at full strength, 0x00RRGGBB
};
[[nodiscard]] Foliage prepare_foliage(const SceneData& scene, const Look& look, const ShadowMap& shadows);
// Per foliage triangle: 1 when none of its vertices can move `threshold_pixels`
// at this projection, so it can be drawn once with the fixed layer.
[[nodiscard]] std::vector<std::uint8_t> settle_foliage(const SceneData& scene, const Foliage& foliage,
                                                       const Projection& projection, float threshold_pixels);

// Per foliage triangle: 1 when, wherever the current can carry it, every pixel it
// could cover is already nearer in `depth` (a fixed layer's), so drawing it can change
// nothing. Conservative: a triangle is kept unless that is certain.
[[nodiscard]] std::vector<std::uint8_t> hide_foliage(const SceneData& scene, const Foliage& foliage,
                                                     const Projection& projection, const std::vector<float>& depth,
                                                     const std::vector<std::uint8_t>& settled);

// Statistics for measurement and tests.
struct StageCounters {
    std::uint64_t fixed_builds{};
    std::uint64_t sway_updates{};
    std::uint64_t frames{};
    std::uint64_t sway_triangles{};
    std::uint64_t hidden_triangles{};  // foliage the current fixed layer hides
    std::uint64_t actor_fragments{};
};

// Shadow map of the fixed scenery and the foliage at rest.
[[nodiscard]] ShadowMap build_shadow_map(const SceneData& scene, const Look& look, int size);

// Builds the fixed layer, including foliage that settles at this size. Pure: reads
// only its arguments, so a view may run it on a worker while it keeps drawing with
// the previous layer.
[[nodiscard]] FixedLayer build_fixed_layer(const SceneData& scene, const Look& look, const ShadowMap& shadows,
                                           const Foliage& foliage, int width, int height, int supersample,
                                           float settle_threshold_pixels);

// An item (a triangle or a plant) and the depth it is ordered by.
struct SortKey {
    float depth{};
    std::uint32_t triangle{};
};

class Stage final {
  public:
    // The scene, look and foliage are observed and must outlive the stage.
    Stage(const SceneData& scene, const Look& look, const Foliage& foliage);
    [[nodiscard]] bool ready() const {
        return fixed_.width > 0;
    }
    [[nodiscard]] int width() const {
        return fixed_.width;
    }
    [[nodiscard]] int height() const {
        return fixed_.height;
    }
    // Takes ownership of a fixed layer built for the current camera.
    void adopt(FixedLayer layer);
    // Rebuilds the sway layer for `time` and shows it.
    void update_sway(double time);
    // The same in two steps, so the rebuild can run on a worker: build_sway draws into
    // the hidden sway buffer while compose() keeps reading the shown one; show_sway
    // (on the composing thread, with no build running) makes the new one current.
    // Nothing else on the stage may be called during a build, except compose().
    void build_sway(double time);
    void show_sway();
    // Composes a frame: creatures and particles at `time`, the animated light at
    // `light_time` (held still under reduced motion). The sway layer must have been
    // updated at least once.
    void compose(double time, double light_time, const std::vector<Creature>& creatures);
    // The last composed frame: width * height pixels, 0xFFRRGGBB.
    [[nodiscard]] const std::vector<std::uint32_t>& frame() const {
        return frame_color_;
    }
    [[nodiscard]] const Projection& projection() const {
        return projection_;
    }
    [[nodiscard]] const StageCounters& counters() const {
        return counters_;
    }

  private:
    void draw_actors(double time, float light_time, const std::vector<Creature>& creatures);
    void draw_risers(double time);

    const SceneData& scene_;
    const Look& look_;
    const Foliage& foliage_;
    FixedLayer fixed_{};
    Projection projection_{};
    // Sway layer.
    // Two sway layers: one shown (read by compose), one being built.
    std::vector<std::uint32_t> sway_color_[2]{};
    std::vector<float> sway_depth_[2]{};
    std::vector<std::uint32_t> lit_pixels_[2]{};  // pixels still taking animated light
    // Per pixel, with a caustic field: the light's tap (CausticField::tap) and the
    // display colour it adds at full strength. Fixed surfaces once per layer;
    // the sway layers add the foliage's own.
    std::vector<std::uint32_t> fixed_tap_{};
    std::vector<std::uint32_t> sway_tap_[2]{};
    std::vector<std::uint32_t> sway_boost_[2]{};
    std::vector<CausticPoint> sway_light_{};  // this update, per moving foliage vertex
    int shown_{};
    std::vector<std::uint8_t> lit_mask_{};     // 1 where the fixed surface still shows
    std::vector<std::uint8_t> fixed_lit_{};    // 1 where the fixed layer takes animated light
    std::vector<CurrentRoot> roots_{};
    std::vector<std::uint32_t> moving_order_{};     // foliage triangles redrawn, in draw order
    std::vector<std::uint32_t> moving_vertices_{};  // their vertices
    std::vector<ViewPoint> sway_view_{};
    std::vector<ScreenPoint> sway_screen_{};
    std::vector<std::uint8_t> sway_in_front_{};
    std::vector<SwayShade> sway_shade_{};  // this update
    // Frame.
    std::vector<std::uint32_t> frame_color_{};
    std::vector<float> frame_depth_{};
    // Creature workspace, reused every frame.
    struct ActorVertexOut {
        ViewPoint view{};
        ScreenPoint screen{};  // projected once per frame, when in front of the near plane
        bool in_front{};
        Vec3 world{};
        Vec3 normal{};
        Vec3 local{};
    };
    std::vector<ActorVertexOut> actor_vertices_{};
    struct Fragment {
        std::uint32_t pixel{};
        std::uint32_t part{};
        std::uint32_t triangle{};
        float weight0{};
        float weight1{};
        float weight2{};
        bool front{};
    };
    std::vector<Fragment> fragments_{};
    std::vector<std::uint32_t> part_vertex_base_{};
    std::vector<std::vector<Vec3>> mesh_normals_{};  // per creature mesh, decoded once
    std::vector<Affine> part_normal_transform_{};    // per part
    std::vector<float> creature_radius_{};           // per creature, its own units
    std::vector<Pose> poses_{};                      // this frame
    std::vector<std::uint8_t> creature_visible_{};   // this frame
    StageCounters counters_{};
};

// Display colour packing.
inline std::uint32_t pack_rgb(Rgb c) {
    const std::uint32_t r = static_cast<std::uint32_t>(std::clamp(c.x, 0.0F, 1.0F) * 255.0F + 0.5F);
    const std::uint32_t g = static_cast<std::uint32_t>(std::clamp(c.y, 0.0F, 1.0F) * 255.0F + 0.5F);
    const std::uint32_t b = static_cast<std::uint32_t>(std::clamp(c.z, 0.0F, 1.0F) * 255.0F + 0.5F);
    const std::uint32_t result = (r << 16U) | (g << 8U) | b;
    return result;
}
inline Rgb unpack_rgb(std::uint32_t c) {
    const Rgb result{static_cast<float>((c >> 16U) & 255U) / 255.0F, static_cast<float>((c >> 8U) & 255U) / 255.0F,
                     static_cast<float>(c & 255U) / 255.0F};
    return result;
}

// Samples a material map with wrapping, bilinear filtering and a mip level chosen
// from `lod`. Mips are built on first use by `prepare_texture`.
struct MipChain {
    std::vector<Texture> levels{};
};
[[nodiscard]] MipChain build_mips(const Texture& texture);
[[nodiscard]] Vec3 sample(const MipChain& chain, float u, float v, float lod);

} // namespace ambient
