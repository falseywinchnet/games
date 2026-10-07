#pragma once
// Caustics: the net of bright lines that sunlight focused by a rippling surface
// throws on everything below it.
//
// The pattern is made once, when a scene's look is created, by tracing light through
// a rippling surface: a tileable height field of a dozen travelling waves (integer
// wave vectors, so the tile wraps; whole cycles per loop, so time wraps), photons
// on a regular grid bent by its slope and gathered where they land. Where the
// surface focuses them, they pile up into the familiar branching web. Each of
// `frames` steps through one loop is kept twice, sharp and blurred, as bytes.
//
// Drawing it is two table reads per pixel. Each lit surface point is reduced once,
// when its layer is built, to a tap: the texel straight up the light from it, how
// sharp the light is at its depth (sharp near the surface, soft near the floor) and
// how bright. A frame then mixes the tap's sharp and soft values between the two
// stored frames around the current time. The pattern moves because the waves move;
// nothing scrolls.
#include "ambient_math.hpp"

#include <cstdint>
#include <vector>

namespace ambient {

struct CausticSpec {
    int size{128};            // texels per tile side, a power of two (16..256)
    int frames{32};           // stored steps through one loop
    double period{18};        // seconds per loop
    float tile{3.2F};         // world units per tile side
    float angle{0.47F};       // the tile's turn about the vertical, radians (hides the grid)
    float surface{9.0F};      // world height of the water surface
    float floor{0.0F};        // world height where the light is softest and dimmest
    float focus{0.0055F};     // how strongly the ripples bend light (tile units per unit slope)
    float floor_gain{0.7F};   // brightness at the floor, relative to just under the surface
    std::uint32_t seed{1};
};

// A point's position in the pattern before wrapping, so it can be interpolated
// across a triangle: texel coordinates and the point's height.
struct CausticPoint {
    float u{};
    float v{};
    float height{};
};

class CausticField {
  public:
    CausticField() = default;
    // Traces the pattern; `light` points toward the light (it need not be unit length).
    CausticField(const CausticSpec& spec, Vec3 light);

    [[nodiscard]] bool empty() const {
        return frames_ == 0;
    }
    // A surface point's tap: texel (low 16 bits), sharpness (next 8), gain (top 8).
    [[nodiscard]] std::uint32_t tap(Vec3 world) const;
    // The same, from a position in texels (already projected) and a height.
    // `sharpness` scales how sharp the light may be there: thin leaves seen edge-on
    // take the soft light only, which reads as moving patches rather than dashes.
    [[nodiscard]] std::uint32_t tap_texel(float u, float v, float height, float sharpness = 1.0F) const;
    // The same in two parts, for many points sharing a height and sharpness (a leaf
    // triangle): the tap's upper bits once, then each point's texel.
    [[nodiscard]] std::uint32_t tap_bits(float height, float sharpness) const;
    [[nodiscard]] std::uint32_t texel_index(float u, float v) const {
        int x = static_cast<int>(u);
        int y = static_cast<int>(v);
        x -= u < static_cast<float>(x) ? 1 : 0;
        y -= v < static_cast<float>(y) ? 1 : 0;
        const std::uint32_t result = static_cast<std::uint32_t>((y & mask_) * size_ + (x & mask_));
        return result;
    }
    // Projects a point up the light into texel coordinates (unwrapped).
    void texel_of(Vec3 world, float& u, float& v) const;

    // The two stored frames around a time and how far between them (0..256).
    struct Phase {
        const std::uint16_t* first{};
        const std::uint16_t* second{};
        std::uint32_t blend{};
    };
    [[nodiscard]] Phase phase(float time) const;
    // Light strength 0..1 at a tap.
    [[nodiscard]] static float strength(const Phase& phase, std::uint32_t tap) {
        const std::uint32_t texel = tap & 0xFFFFU;
        const std::uint32_t sharp = (tap >> 16U) & 255U;
        const std::uint32_t gain = tap >> 24U;
        const std::uint32_t a = phase.first[texel];
        const std::uint32_t b = phase.second[texel];
        const std::uint32_t va = (a & 255U) * sharp + (a >> 8U) * (255U - sharp);
        const std::uint32_t vb = (b & 255U) * sharp + (b >> 8U) * (255U - sharp);
        const std::uint32_t mixed = (va * (256U - phase.blend) + vb * phase.blend) >> 8U;
        const float result = static_cast<float>(mixed * gain) * (1.0F / (255.0F * 255.0F * 255.0F));
        return result;
    }
    // The same as a level 0..256 (about 256 times the strength), for integer blending.
    [[nodiscard]] static std::uint32_t light_level(const Phase& phase, std::uint32_t tap) {
        const std::uint32_t texel = tap & 0xFFFFU;
        const std::uint32_t sharp = (tap >> 16U) & 255U;
        const std::uint32_t gain = tap >> 24U;
        const std::uint32_t a = phase.first[texel];
        const std::uint32_t b = phase.second[texel];
        const std::uint32_t va = (a & 255U) * sharp + (a >> 8U) * (255U - sharp);
        const std::uint32_t vb = (b & 255U) * sharp + (b >> 8U) * (255U - sharp);
        const std::uint32_t mixed = (va * (256U - phase.blend) + vb * phase.blend) >> 8U;
        const std::uint32_t result = (mixed * gain + 32768U) >> 16U;
        return result;
    }
    // Strength at a point and time, for things drawn per pixel (creatures).
    [[nodiscard]] float at(Vec3 world, float time) const {
        const float result = strength(phase(time), tap(world));
        return result;
    }
    [[nodiscard]] int size() const {
        return size_;
    }
    [[nodiscard]] int frames() const {
        return frames_;
    }
    // A stored frame's texel (sharp in the low byte, soft in the high), for tests.
    [[nodiscard]] std::uint16_t texel(int frame, int index) const;

  private:
    int size_{};
    int mask_{};
    int frames_{};
    double period_{1};
    float texels_per_unit_{};
    float cos_angle_{1};
    float sin_angle_{};
    Vec3 slant_{};  // horizontal shift per unit of height, up the light
    float surface_{};
    float floor_{};
    float floor_gain_{1};
    std::vector<std::uint16_t> data_{};  // frames * size * size
};

} // namespace ambient
