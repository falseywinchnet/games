#pragma once
// Riverscape's materials, lighting and water for the ambient engine. The formulas
// are Stillwater's Metal shader (itself adapted from Desktop Habitats, MIT),
// evaluated on the processor: once per size for fixed surfaces, per vertex for
// swaying foliage, per pixel for creatures. Each of Stillwater's tanks gives them
// its own water, light and fish colours (TankStyle); the planted tank's are the
// original's.
#include "caustics.hpp"
#include "stage.hpp"

#include <array>
#include <vector>

namespace sw {

// Archive material numbers (Stillwater's).
enum Material : std::uint32_t {
    bubble = 3,
    crab = 6,
    sand = 7,
    rock = 8,
    wood = 9,
    foliage = 10,
    fish_body = 11,
    fish_fin = 12,
    fern = 13,
    pebble = 14,
    // Added by Stillwater's treasure chest (treasure.hpp), not in the archive.
    iron = 15,
    gold = 16,
    // Added by the other tanks' archives (scene_src/build_tanks.py).
    coral = 17,   // vertex-coloured, soft and slightly glowing
    leaf = 18,    // fallen leaves and bark litter, vertex-coloured
};

// One kind of fish's colouring. The planted tank's tetra is the default; a fish's
// archive tint picks its kind: red channel 1 to 6 for the first to the sixth.
struct FishColors {
    ambient::Vec3 back{0.0105F, 0.015F, 0.0125F};   // along the spine
    ambient::Vec3 upper{0.034F, 0.049F, 0.043F};    // just below it
    ambient::Vec3 flank{0.47F, 0.51F, 0.5F};
    ambient::Vec3 belly{0.655F, 0.66F, 0.63F};
    ambient::Vec3 sheen{0.08F, 0.41F, 0.62F};       // the lateral stripe
    float sheen_amount{0.82F};
    ambient::Vec3 warm{0.42F, 0.105F, 0.03F};       // the tail-end wash
    float warm_amount{0.45F};
    ambient::Vec3 cheek_dark{0.04F, 0.052F, 0.046F};
    ambient::Vec3 cheek_light{0.42F, 0.44F, 0.42F};
    ambient::Vec3 snout{0.05F, 0.056F, 0.046F};
    ambient::Vec3 fin_root{0.32F, 0.20F, 0.14F};
    ambient::Vec3 fin_tip{0.45F, 0.035F, 0.012F};
    // Vertical bands across the body (0 for none): their count, colour and edge.
    float bands{0};
    ambient::Vec3 band{1, 1, 1};
    ambient::Vec3 band_edge{0.01F, 0.01F, 0.01F};
    float reflect{1};  // the silvery mirror strip, 0..1
};

// A tank's water, light and fish. Defaults are the planted tank's.
struct TankStyle {
    ambient::Vec3 hemisphere_low{0.035F, 0.028F, 0.016F};
    ambient::Vec3 hemisphere_high{0.10F, 0.13F, 0.085F};
    ambient::Vec3 fill{0.06F, 0.085F, 0.10F};
    ambient::Vec3 sun{1.43F, 1.39F, 1.28F};
    // Caustics: the share of the sun's light on surfaces that the ripples gather into
    // lines (taken from the even light and given back where the lines fall), and the
    // lines' tint. `caustic_mean` is the pattern's average strength, which keeps the
    // overall light the same.
    float caustic_share{0.5F};
    float caustic_mean{0.15F};
    ambient::Vec3 caustic{1.0F, 1.0F, 0.86F};
    ambient::Vec3 absorption{0.030F, 0.006F, 0.016F};
    ambient::Vec3 water{0.0055F, 0.023F, 0.015F};
    float fog_start{16};
    float fog_density{0.001156F};
    ambient::Vec3 tank_absorption{0.038F, 0.010F, 0.018F};  // crabs and bubbles
    ambient::Vec3 sand_tint{1, 1, 1};
    ambient::Vec3 rock_tint{1, 1, 1};
    float moss{1};  // how much moss and film the surfaces carry
    // What grows on stone and wood: a thin film, and a denser turf where the coverage is
    // heavy (algae in fresh water; pink coralline crust on a reef's live rock).
    ambient::Vec3 film{0.03F, 0.055F, 0.007F};
    ambient::Vec3 turf{0.0035F, 0.013F, 0.0025F};
    ambient::Vec3 bubble{0.08F, 0.19F, 0.18F};
    ambient::Vec3 bubble_rim{0.4F, 0.62F, 0.56F};
    // Light from the surface brightening the open water toward the top of the view
    // (added to the water behind everything; none in the planted tank).
    ambient::Vec3 surface_glow{0, 0, 0};
    // Shafts of sunlight slanting down through the open water, as a share of the glow
    // (0 for none). They stand still: the water behind is drawn once per size.
    float shafts{0};
    std::array<FishColors, 6> fish{};
    ambient::CausticSpec caustics{};
};

class RiverscapeLook final : public ambient::Look {
  public:
    // Builds mip chains of the scene's six material maps (albedo, normal for
    // sand, rock and wood, in that order).
    explicit RiverscapeLook(const ambient::SceneData& scene, const TankStyle& style = TankStyle{});
    // Light strength multiplier, 1 by default.
    void set_illumination(float value) {
        illumination_ = value;
    }

    ambient::Rgb background(float u, float v) const override;
    ambient::LightFrame light_frame() const override;
    ambient::FixedShade shade_fixed(const ambient::FixedSample& sample, const ambient::SceneData& scene) const override;
    float animated_light(float x, float z, float time) const override;
    const ambient::CausticField* caustics() const override {
        return &caustics_;
    }
    ambient::SwayShade shade_sway(const ambient::SwaySample& sample) const override;
    void deform_actor(std::uint32_t material, const ambient::ActorVertex& vertex, float creature, float time,
                      ambient::Vec3& position, ambient::Vec3& normal) const override;
    bool closed_actor(std::uint32_t material) const override;
    ambient::Rgb shade_actor(const ambient::ActorSample& sample, float& alpha) const override;
    ambient::Rgb shade_riser(ambient::Vec3 world, ambient::Vec3 normal, float screen_v) const override;
    float joint_lift(float joint, float joint_phase, float time) const override;
    bool closed_fixed(std::uint32_t material) const override;

  private:
    TankStyle style_{};
    ambient::CausticField caustics_{};
    ambient::Vec3 eye_{};
    ambient::Vec3 light_{};
    float illumination_{1};
    std::array<ambient::MipChain, 6> maps_{};
    std::array<float, 256> srgb_to_linear_{};
};

// Display encoding used throughout: Three.js's ACES fit, then gamma 1/2.2.
ambient::Rgb display_aces(ambient::Rgb linear);

} // namespace sw
