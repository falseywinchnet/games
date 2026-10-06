#pragma once
// Riverscape's materials, lighting and water for the ambient engine. The formulas
// are Stillwater's Metal shader (itself adapted from Desktop Habitats, MIT),
// evaluated on the processor: once per size for fixed surfaces, per vertex for
// swaying foliage, per pixel for creatures.
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
};

class RiverscapeLook final : public ambient::Look {
  public:
    // Builds mip chains of the scene's six material maps (albedo, normal for
    // sand, rock and wood, in that order).
    explicit RiverscapeLook(const ambient::SceneData& scene);
    // Light strength multiplier, 1 by default.
    void set_illumination(float value) {
        illumination_ = value;
    }

    ambient::Rgb background(float u, float v) const override;
    ambient::LightFrame light_frame() const override;
    ambient::FixedShade shade_fixed(const ambient::FixedSample& sample, const ambient::SceneData& scene) const override;
    float animated_light(float x, float z, float time) const override;
    ambient::SwayShade shade_sway(const ambient::SwaySample& sample) const override;
    void deform_actor(std::uint32_t material, const ambient::ActorVertex& vertex, float creature, float time,
                      ambient::Vec3& position, ambient::Vec3& normal) const override;
    bool closed_actor(std::uint32_t material) const override;
    ambient::Rgb shade_actor(const ambient::ActorSample& sample, float& alpha) const override;
    ambient::Rgb shade_riser(ambient::Vec3 world, ambient::Vec3 normal, float screen_v) const override;
    float joint_lift(float joint, float joint_phase, float time) const override;
    bool closed_fixed(std::uint32_t material) const override;

  private:
    ambient::Vec3 eye_{};
    ambient::Vec3 light_{};
    float illumination_{1};
    std::array<ambient::MipChain, 6> maps_{};
    std::array<float, 256> srgb_to_linear_{};
};

// Display encoding used throughout: Three.js's ACES fit, then gamma 1/2.2.
ambient::Rgb display_aces(ambient::Rgb linear);

} // namespace sw
