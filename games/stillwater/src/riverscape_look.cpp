// Riverscape material formulas adapted from Stillwater's aquarium.metal, which
// adapts Desktop Habitats (Copyright (c) 2026 Chase Lean, MIT). The ACES fit follows
// Three.js (MIT). See HANDOFF.md for provenance.
#include "riverscape_look.hpp"

#include <algorithm>
#include <cmath>

namespace sw {
namespace {

using ambient::Rgb;
using ambient::Vec3;

Vec3 water_color(const TankStyle& style, float v_up) {
    const Vec3 result = ambient::scale(style.water, 0.82F + 0.18F * v_up);
    return result;
}

// x^(1/2.2) for x in 0..1, interpolated from a table indexed by sqrt(x), where the
// curve is nearly straight even in the dark water tones. The encoding runs for every
// shaded vertex and pixel; the error stays well below one display level.
class GammaTable final {
  public:
    static constexpr int size = 1024;
    GammaTable() {
        for (int index = 0; index <= size; ++index)
            values_[static_cast<std::size_t>(index)] =
                std::pow(static_cast<float>(index) / static_cast<float>(size), 2.0F / 2.2F);
    }
    float encode(float x) const {
        const float position = std::sqrt(std::clamp(x, 0.0F, 1.0F)) * static_cast<float>(size);
        const int index = std::min(static_cast<int>(position), size - 1);
        const float t = position - static_cast<float>(index);
        const float result = ambient::mix(values_[static_cast<std::size_t>(index)],
                                          values_[static_cast<std::size_t>(index + 1)], t);
        return result;
    }

  private:
    std::array<float, size + 1> values_{};
};

// Initialized once on first use; thread-safe, so fixed layers may build on a worker.
float encode_gamma(float x) {
    static const GammaTable table{};
    const float result = table.encode(x);
    return result;
}

// Display mapping for the original tank shader path (crabs, bubbles, water).
Rgb display_tank(Vec3 linear) {
    const Rgb result{encode_gamma(1.0F - std::exp(-linear.x * 1.6F)), encode_gamma(1.0F - std::exp(-linear.y * 1.6F)),
                     encode_gamma(1.0F - std::exp(-linear.z * 1.6F))};
    return result;
}

Vec3 exp3(Vec3 v) {
    const Vec3 result{std::exp(v.x), std::exp(v.y), std::exp(v.z)};
    return result;
}

// Hash-based value noise. The shader hashed with sin(); an integer hash gives the
// same character for a fraction of the cost.
float hash3(int x, int y, int z) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8DA6B343U ^ static_cast<std::uint32_t>(y) * 0xD8163841U ^
                      static_cast<std::uint32_t>(z) * 0xCB1AB31FU;
    h ^= h >> 13U;
    h *= 0x5BD1E995U;
    h ^= h >> 15U;
    const float result = static_cast<float>(h & 0xFFFFFFU) / 16777215.0F;
    return result;
}
float value_noise(Vec3 p) {
    const float fx = std::floor(p.x);
    const float fy = std::floor(p.y);
    const float fz = std::floor(p.z);
    const int x = static_cast<int>(fx);
    const int y = static_cast<int>(fy);
    const int z = static_cast<int>(fz);
    const float tx = ambient::smoothstep(0, 1, p.x - fx);
    const float ty = ambient::smoothstep(0, 1, p.y - fy);
    const float tz = ambient::smoothstep(0, 1, p.z - fz);
    const float a = ambient::mix(hash3(x, y, z), hash3(x + 1, y, z), tx);
    const float b = ambient::mix(hash3(x, y + 1, z), hash3(x + 1, y + 1, z), tx);
    const float c = ambient::mix(hash3(x, y, z + 1), hash3(x + 1, y, z + 1), tx);
    const float d = ambient::mix(hash3(x, y + 1, z + 1), hash3(x + 1, y + 1, z + 1), tx);
    const float result = ambient::mix(ambient::mix(a, b, ty), ambient::mix(c, d, ty), tz);
    return result;
}

// Water absorption and distance fog shared by the Riverscape surfaces.
struct Water {
    Vec3 transmission{};
    float fog{};
};
Water water_at(const TankStyle& style, Vec3 eye, Vec3 world) {
    const float depth = std::max(0.0F, ambient::length(ambient::subtract(eye, world)) - style.fog_start);
    const float fog = 1.0F - std::exp(-depth * depth * style.fog_density);
    const Water result{ambient::scale(exp3(ambient::scale(style.absorption, -depth)), 1.0F - fog), fog};
    return result;
}

const FishColors& fish_colors(const TankStyle& style, const ambient::ActorSample& in) {
    const int last = static_cast<int>(style.fish.size()) - 1;
    const int kind = std::clamp(static_cast<int>(in.tint.x + 0.5F) - 1, 0, last);
    return style.fish[static_cast<std::size_t>(kind)];
}

Vec3 fish_skin(const FishColors& f, const ambient::ActorSample& in) {
    const float part = in.part;
    const float band = std::clamp(in.v, 0.0F, 1.0F);
    const float x = in.local.x;
    if (part < 0.5F) {
        Vec3 skin = ambient::mix(f.back, f.upper, ambient::smoothstep(0.02F, 0.135F, band));
        skin = ambient::mix(skin, f.flank, ambient::smoothstep(0.185F, 0.42F, band));
        const float belly = ambient::smoothstep(-0.26F, -0.12F, x);
        skin = ambient::mix(skin, f.belly, ambient::smoothstep(0.52F, 0.84F, band) * belly);
        const float sheen_band = (band - 0.25F) / 0.07F;
        const float sheen = std::exp(-sheen_band * sheen_band) * ambient::smoothstep(-0.285F, -0.225F, x) *
                            (1.0F - ambient::smoothstep(0.188F, 0.245F, x));
        skin = ambient::mix(skin, f.sheen, sheen * f.sheen_amount);
        const float warm = (1.0F - ambient::smoothstep(-0.27F, 0.0F, x)) * ambient::smoothstep(0.32F, 0.60F, band) *
                           (1.0F - ambient::smoothstep(0.88F, 1.0F, band));
        skin = ambient::mix(skin, f.warm, warm * f.warm_amount);
        if (f.bands > 0) {
            // Bands across the body from nose to tail (the body runs about -0.3..0.3 in x),
            // each with a dark edge.
            const float across = (x + 0.30F) / 0.60F * f.bands;
            const float cell = across - std::floor(across);
            const float centre = std::abs(cell - 0.5F);
            const float white = 1.0F - ambient::smoothstep(0.16F, 0.20F, centre);
            const float edge = ambient::smoothstep(0.15F, 0.19F, centre) * (1.0F - ambient::smoothstep(0.21F, 0.25F, centre));
            skin = ambient::mix(skin, f.band, white);
            skin = ambient::mix(skin, f.band_edge, edge * 0.85F);
        }
        const float head = ambient::smoothstep(0.175F, 0.22F, x);
        const Vec3 cheek = ambient::mix(f.cheek_dark, f.cheek_light, ambient::smoothstep(0.13F, 0.4F, band));
        skin = ambient::mix(skin, cheek, head * 0.92F);
        skin = ambient::mix(skin, f.snout, ambient::smoothstep(0.25F, 0.33F, x) * 0.82F);
        const float gill_y = std::clamp((in.local.y + 0.004F) / 0.078F, -1.0F, 1.0F);
        const float opercle = 0.196F - 0.03F * (1.0F - gill_y * gill_y);
        const float gill = (x - opercle) / 0.0028F;
        skin = ambient::scale(skin, 1.0F - 0.5F * std::exp(-gill * gill));
        return skin;
    }
    if (part < 6.5F || part > 11.5F) {
        const float ribs = std::pow(0.5F + 0.5F * std::cos(in.u * 6.283185F * (part < 1.5F ? 18.0F : 11.0F)), 12.0F);
        const Vec3 membrane = ambient::mix(f.fin_root, f.fin_tip, ambient::smoothstep(0.2F, 0.8F, in.v));
        const Vec3 result = ambient::scale(membrane, 0.65F + 0.6F * ribs);
        return result;
    }
    if (part < 7.5F) {
        const Vec3 result = ambient::mix(Vec3{0.62F, 0.6F, 0.415F}, Vec3{0.33F, 0.30F, 0.15F}, in.v);
        return result;
    }
    if (part < 8.5F)
        return {0.0055F, 0.0075F, 0.0085F};
    if (part < 9.5F)
        return {0.036F, 0.020F, 0.018F};
    return {0.175F, 0.168F, 0.132F};
}

} // namespace

Rgb display_aces(Rgb value) {
    // Three.js ACESFilmicToneMapping (MIT), exposure 1.
    const Vec3 v = ambient::scale(value, 1.17F / 0.6F);
    const Vec3 x{0.59719F * v.x + 0.35458F * v.y + 0.04823F * v.z, 0.07600F * v.x + 0.90834F * v.y + 0.01566F * v.z,
                 0.02840F * v.x + 0.13383F * v.y + 0.83777F * v.z};
    const Vec3 numerator{x.x * (x.x + 0.0245786F) - 0.000090537F, x.y * (x.y + 0.0245786F) - 0.000090537F,
                         x.z * (x.z + 0.0245786F) - 0.000090537F};
    const Vec3 denominator{x.x * (0.983729F * x.x + 0.432951F) + 0.238081F,
                           x.y * (0.983729F * x.y + 0.432951F) + 0.238081F,
                           x.z * (0.983729F * x.z + 0.432951F) + 0.238081F};
    const Vec3 r{numerator.x / denominator.x, numerator.y / denominator.y, numerator.z / denominator.z};
    const Vec3 out{1.60475F * r.x - 0.53108F * r.y - 0.07367F * r.z, -0.10208F * r.x + 1.10813F * r.y - 0.00605F * r.z,
                   -0.00327F * r.x - 0.07276F * r.y + 1.07602F * r.z};
    const Rgb result{encode_gamma(out.x), encode_gamma(out.y), encode_gamma(out.z)};
    return result;
}

RiverscapeLook::RiverscapeLook(const ambient::SceneData& scene, const TankStyle& style)
    : style_(style), caustics_(style.caustics, scene.camera.light), eye_(scene.camera.eye), light_(scene.camera.light) {
    for (std::size_t index = 0; index < maps_.size() && index < scene.textures.size(); ++index)
        maps_[index] = ambient::build_mips(scene.textures[index]);
    for (std::size_t index = 0; index < srgb_to_linear_.size(); ++index) {
        const float c = static_cast<float>(index) / 255.0F;
        srgb_to_linear_[index] = c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F);
    }
}

Rgb RiverscapeLook::background(float u, float v) const {
    const float up = 1.0F - v;
    float glow = up * up;
    if (style_.shafts > 0) {
        // Five soft shafts leaning a little, spreading as they fall, fading with depth.
        constexpr float centres[5] = {0.12F, 0.31F, 0.47F, 0.66F, 0.86F};
        constexpr float widths[5] = {0.035F, 0.05F, 0.03F, 0.06F, 0.04F};
        constexpr float strengths[5] = {0.7F, 1.0F, 0.6F, 0.9F, 0.65F};
        float shaft = 0;
        for (int k = 0; k < 5; ++k) {
            const float across = (u - centres[k] + 0.12F * v) / (widths[k] * (1.0F + 1.5F * v));
            shaft += strengths[k] * std::exp(-across * across);
        }
        glow += style_.shafts * shaft * std::pow(up, 1.6F);
    }
    const Vec3 water = ambient::add(water_color(style_, up), ambient::scale(style_.surface_glow, glow));
    const Rgb result = display_tank(water);
    return result;
}

ambient::LightFrame RiverscapeLook::light_frame() const {
    // Stillwater's light camera: orthographic, 24 x 28 units, looking down the light.
    const float c = 0.9138115F;
    const float s = 0.4061385F;
    ambient::LightFrame frame{};
    frame.right = {1, 0, 0};
    frame.up = {0, s, -c};
    frame.toward = {0, c, s};
    frame.origin = ambient::add(ambient::scale(frame.up, 5.0F), ambient::scale(frame.toward, 20.0F));
    frame.half_width = 12;
    frame.half_height = 14;
    return frame;
}

ambient::FixedShade RiverscapeLook::shade_fixed(const ambient::FixedSample& in, const ambient::SceneData&) const {
    Vec3 normal = in.normal;
    Vec3 albedo = in.albedo;
    const std::uint32_t material = in.material;
    const bool mapped = material == sand || material == rock || material == wood;
    if (mapped) {
        const std::size_t first = material == sand ? 0U : (material == rock ? 2U : 4U);
        const Vec3 color = ambient::sample(maps_[first], in.u, in.v, in.texture_lod);
        const Vec3 linear{srgb_to_linear_[static_cast<std::size_t>(color.x * 255.0F + 0.5F)],
                          srgb_to_linear_[static_cast<std::size_t>(color.y * 255.0F + 0.5F)],
                          srgb_to_linear_[static_cast<std::size_t>(color.z * 255.0F + 0.5F)]};
        const Vec3 tint = material == sand ? style_.sand_tint : (material == rock ? style_.rock_tint : Vec3{1, 1, 1});
        const Vec3 tinted = ambient::multiply(ambient::multiply(albedo, linear), tint);
        const Vec3 detail = ambient::sample(maps_[first + 1U], in.u, in.v, in.texture_lod);
        // Tangent-space normal map on the triangle's own frame.
        const float strength = material == sand ? 0.32F : 0.8F;
        const float divisor = std::max(ambient::dot(in.tangent, in.tangent), ambient::dot(in.bitangent, in.bitangent));
        if (divisor > 1e-12F) {
            const float k = 1.0F / std::sqrt(divisor);
            const Vec3 t = ambient::scale(in.tangent, k * (detail.x * 2 - 1) * strength);
            const Vec3 b = ambient::scale(in.bitangent, k * (detail.y * 2 - 1) * strength);
            normal = ambient::normalize(ambient::add(ambient::add(t, b), ambient::scale(normal, detail.z * 2 - 1)));
        }
        const float fine = value_noise(ambient::scale(in.world, 9)) * 0.6F + value_noise(ambient::scale(in.world, 27)) * 0.4F;
        const float moss = ambient::smoothstep(0.07F, 0.5F, in.moss + (fine - 0.5F) * 0.45F) * style_.moss;
        const Vec3 film = material == sand ? Vec3{0.10F, 0.10F, 0.02F} : style_.film;
        const Vec3 turf = style_.turf;
        const Vec3 moss_color =
            ambient::scale(ambient::mix(film, turf, ambient::smoothstep(0.15F, 0.85F, in.moss)), 0.6F + 0.8F * fine);
        albedo = ambient::mix(tinted, ambient::multiply(moss_color, albedo), moss);
    }
    const Vec3 hemisphere = ambient::mix(style_.hemisphere_low, style_.hemisphere_high, normal.y * 0.5F + 0.5F);
    const Vec3 fill_direction = ambient::normalize({1, 5, 10});
    const Vec3 base = ambient::add(ambient::multiply(albedo, hemisphere),
                                   ambient::scale(ambient::multiply(albedo, style_.fill),
                                                  std::max(0.0F, ambient::dot(normal, fill_direction))));
    const float direct = std::max(0.0F, ambient::dot(normal, light_));
    // The chest's iron and gold (treasure.hpp) are metals: they also catch the light
    // as a highlight, and the gold glints where the rippling light crosses it.
    const bool metal = material == iron || material == gold;
    // Caustics fall on everything that faces up into the light; the vertical faces
    // of stones catch a little where the light comes in slanting.
    const float caustic = std::clamp(normal.y * 0.85F + 0.15F, 0.0F, 1.0F);
    const Water water = water_at(style_, eye_, in.world);
    const Vec3 base_term = ambient::add(ambient::multiply(base, water.transmission),
                                        ambient::scale(water_color(style_, 0.5F), water.fog));
    const Vec3 surface = ambient::multiply(albedo, water.transmission);
    const float lit = in.light_visibility * illumination_;
    // The ripples gather part of the sunlight into moving lines: that part leaves the
    // even light and returns, concentrated, where the pattern is bright.
    const float share = style_.caustic_share * caustic;
    const Vec3 sunlit = ambient::multiply(surface, style_.sun);
    Vec3 direct_term = ambient::scale(sunlit, direct * lit * (1.0F - share));
    Vec3 focused = ambient::scale(ambient::multiply(sunlit, style_.caustic),
                                  std::max(direct, 0.3F) * lit * share / style_.caustic_mean);
    if (metal) {
        const bool golden = material == gold;
        const Vec3 view = ambient::normalize(ambient::subtract(eye_, in.world));
        const Vec3 half = ambient::normalize(ambient::add(light_, view));
        const float facing = std::max(ambient::dot(normal, half), 0.0F);
        const float shine = std::pow(facing, golden ? 30.0F : 14.0F) * (golden ? 1.1F : 0.35F);
        const Vec3 sheen = golden ? Vec3{1.0F, 0.66F, 0.18F} : Vec3{0.42F, 0.40F, 0.37F};
        direct_term = ambient::add(direct_term,
                                   ambient::scale(ambient::multiply(sheen, water.transmission), shine * lit));
        if (golden)
            focused = ambient::add(focused, ambient::scale(ambient::multiply(ambient::multiply(sheen, water.transmission),
                                                                             style_.caustic),
                                                           caustic * lit * 1.5F));
    }
    if (material == coral) {
        // Living coral and anemone flesh: a faint glow of its own and light let through.
        const Vec3 glow = ambient::scale(ambient::multiply(surface, style_.sun), 0.05F + 0.08F * (1.0F - direct));
        direct_term = ambient::add(direct_term, glow);
        focused = ambient::scale(focused, 1.3F);
    }
    ambient::FixedShade shade{};
    shade.color = display_aces(ambient::add(base_term, direct_term));
    if (caustic > 0 && lit > 0) {
        const Rgb bright = display_aces(ambient::add(ambient::add(base_term, direct_term), focused));
        shade.light_boost = {std::max(0.0F, bright.x - shade.color.x), std::max(0.0F, bright.y - shade.color.y),
                             std::max(0.0F, bright.z - shade.color.z)};
    }
    return shade;
}

float RiverscapeLook::animated_light(float x, float z, float time) const {
    // The caustic pattern just under the surface (the engine reads the field itself;
    // this is for anything asking about a point on the plane).
    const float result = caustics_.at({x, style_.caustics.surface, z}, time);
    return result;
}

ambient::SwayShade RiverscapeLook::shade_sway(const ambient::SwaySample& in) const {
    const Vec3 fill_direction = ambient::normalize({1, 5, 10});
    const Vec3 rim_direction = ambient::normalize({2, 10, -4});
    const float lit = in.light_visibility * illumination_;
    const Water water = water_at(style_, eye_, in.world);
    const Vec3 fogged = ambient::scale(water_color(style_, 0.5F), water.fog);
    ambient::SwayShade shade{};
    for (int side = 0; side < 2; ++side) {
        const Vec3 n = side == 0 ? in.normal : ambient::scale(in.normal, -1);
        const Vec3 albedo = side == 0 ? in.albedo : ambient::multiply(in.albedo, Vec3{0.82F, 0.76F, 0.66F});
        const Vec3 hemisphere = ambient::mix(style_.hemisphere_low, style_.hemisphere_high, n.y * 0.5F + 0.5F);
        const float direct = std::max(0.0F, ambient::dot(n, light_));
        Vec3 color = ambient::multiply(albedo, ambient::add(hemisphere, ambient::scale(style_.sun, direct * lit)));
        color = ambient::add(color, ambient::scale(ambient::multiply(albedo, style_.fill),
                                                   std::max(0.0F, ambient::dot(n, fill_direction))));
        const float back = std::max(0.0F, -ambient::dot(n, light_));
        color = ambient::add(color, ambient::scale(ambient::multiply(albedo, Vec3{0.55F, 0.85F, 0.30F}),
                                                   back * in.translucency * 0.9F * lit));
        color = ambient::add(color, ambient::scale(ambient::multiply(albedo, Vec3{0.13F, 0.20F, 0.075F}),
                                                   std::max(0.0F, ambient::dot(n, rim_direction))));
        const Vec3 shown = ambient::add(ambient::multiply(color, water.transmission), fogged);
        if (side == 0) {
            shade.front = display_aces(shown);
            // Caustic light on the leaves: strongest on faces turned up to it.
            const float upward = 0.35F + 0.65F * std::abs(n.y);
            const Vec3 focused = ambient::scale(ambient::multiply(ambient::multiply(albedo, style_.sun), style_.caustic),
                                                upward * lit * style_.caustic_share * 1.6F);
            const Rgb bright = display_aces(
                ambient::add(shown, ambient::multiply(focused, water.transmission)));
            shade.light_boost = {std::max(0.0F, bright.x - shade.front.x), std::max(0.0F, bright.y - shade.front.y),
                                 std::max(0.0F, bright.z - shade.front.z)};
        } else {
            shade.back = display_aces(shown);
        }
    }
    // Translucent leaf tips: thinner toward the leaf edge, as the shader's coverage.
    if (in.translucency >= 0.7F) {
        const float edge = std::pow(std::abs(in.u - 0.5F) * 2.0F, 5.0F);
        shade.alpha = edge > 0.45F ? 0.5F : 0.75F;
    }
    return shade;
}

void RiverscapeLook::deform_actor(std::uint32_t material, const ambient::ActorVertex& vertex, float creature,
                                  float time, Vec3& p, Vec3& n) const {
    if (material != fish_body && material != fish_fin)
        return;
    const float aft = std::max(0.0F, 0.20F - p.x);
    const float phase = time * 7.0F + creature * 1.7F - p.x * 6.0F;
    const float s = ambient::fast_sin(phase);
    const float bend = 0.19F * aft * aft * s;
    const float slope = -0.38F * aft * s - 1.14F * aft * aft * ambient::fast_cos(phase);
    p.z += bend;
    n = ambient::normalize({n.x - slope * n.z, n.y, n.z});
    if (material == fish_fin) {
        const float fin = vertex.fin / 255.0F;
        p.z += fin * 0.006F * ambient::fast_sin(time * 12.0F + static_cast<float>(vertex.part) * 1.4F);
    }
}

bool RiverscapeLook::closed_actor(std::uint32_t material) const {
    return material == fish_body;
}

bool RiverscapeLook::closed_fixed(std::uint32_t material) const {
    return material == rock;
}

ambient::Rgb RiverscapeLook::shade_actor(const ambient::ActorSample& in, float& alpha) const {
    const Vec3 n = in.normal;
    const Vec3 view = ambient::normalize(ambient::subtract(eye_, in.world));
    alpha = 1;
    if (in.material == fish_body || in.material == fish_fin) {
        const FishColors& colors = fish_colors(style_, in);
        const Vec3 albedo = fish_skin(colors, in);
        if (in.material == fish_fin)
            alpha = 0.55F + 0.35F * (1.0F - in.fin);
        const Vec3 hemisphere = ambient::mix(style_.hemisphere_low, style_.hemisphere_high, n.y * 0.5F + 0.5F);
        const float direct = std::max(0.0F, ambient::dot(n, light_));
        Vec3 color =
            ambient::multiply(albedo, ambient::add(hemisphere, ambient::scale(style_.sun, direct * illumination_)));
        color = ambient::add(color, ambient::scale(ambient::multiply(albedo, style_.fill),
                                                   std::max(0.0F, ambient::dot(n, ambient::normalize({1, 5, 10})))));
        if (in.material == fish_body) {
            const Vec3 incident = ambient::scale(view, -1);
            const Vec3 reflected = ambient::subtract(incident, ambient::scale(n, 2 * ambient::dot(n, incident)));
            const float reflector = ambient::smoothstep(0.07F, 0.24F, in.v) * (1 - ambient::smoothstep(0.58F, 0.92F, in.v));
            const float lift = (reflected.y - 0.6F) * 3.0F;
            const float strip = std::exp(-lift * lift);
            color = ambient::add(color, ambient::scale(albedo, (0.25F + 0.70F * strip) * reflector * colors.reflect));
            const Vec3 half = ambient::normalize(ambient::add(light_, view));
            const float shine = std::pow(std::max(ambient::dot(n, half), 0.0F), 70.0F);
            color = ambient::add(color, ambient::scale(Vec3{0.7F, 0.85F, 0.85F}, shine * illumination_));
        }
        // Caustic light rippling over the fish's back.
        const float upward = std::clamp(n.y * 0.7F + 0.3F, 0.0F, 1.0F);
        color = ambient::add(color, ambient::scale(ambient::multiply(ambient::multiply(albedo, style_.sun), style_.caustic),
                                                   in.light * upward * style_.caustic_share * 1.4F * illumination_));
        const Water water = water_at(style_, eye_, in.world);
        color = ambient::add(ambient::multiply(color, water.transmission), ambient::scale(water_color(style_, 0.5F), water.fog));
        return display_aces(color);
    }
    // Crabs: Stillwater's original tank material.
    const Vec3 albedo = in.tint;
    const float direct = std::max(0.0F, ambient::dot(n, light_));
    const float focused = in.light * std::max(n.y, 0.0F) * 0.8F;
    Vec3 color = ambient::multiply(albedo, ambient::add(Vec3{0.12F, 0.12F, 0.12F},
                                                        ambient::scale(Vec3{1.10F, 1.13F, 0.84F},
                                                                       (direct + focused) * illumination_)));
    const Vec3 half = ambient::normalize(ambient::add(light_, view));
    const float specular = std::pow(std::max(ambient::dot(n, half), 0.0F), 22.0F);
    color = ambient::add(color, Vec3{specular * 0.08F * illumination_, specular * 0.08F * illumination_,
                                     specular * 0.08F * illumination_});
    const float range = ambient::length(ambient::subtract(eye_, in.world));
    color = ambient::multiply(color, exp3(ambient::scale(style_.tank_absorption, -range)));
    const float fog = 1.0F - std::exp(-std::max(0.0F, range - 8.0F) * 0.090F);
    color = ambient::mix(color, water_color(style_, 1.0F - in.screen_v), fog);
    return display_tank(color);
}

ambient::Rgb RiverscapeLook::shade_riser(Vec3 world, Vec3 normal, float screen_v) const {
    const Vec3 view = ambient::normalize(ambient::subtract(eye_, world));
    const float rim = std::pow(1.0F - std::abs(ambient::dot(normal, view)), 3.0F);
    const Vec3 half = ambient::normalize(ambient::add(light_, view));
    const float specular = std::pow(std::max(ambient::dot(normal, half), 0.0F), 22.0F);
    Vec3 color = ambient::add(ambient::add(style_.bubble, ambient::scale(style_.bubble_rim, rim)),
                              Vec3{specular * 0.5F, specular * 0.5F, specular * 0.5F});
    const float range = ambient::length(ambient::subtract(eye_, world));
    color = ambient::multiply(color, exp3(ambient::scale(style_.tank_absorption, -range)));
    const float fog = 1.0F - std::exp(-std::max(0.0F, range - 8.0F) * 0.090F);
    color = ambient::mix(color, water_color(style_, 1.0F - screen_v), fog);
    return display_tank(color);
}

float RiverscapeLook::joint_lift(float joint, float joint_phase, float time) const {
    if (joint != 3.0F)
        return 0;
    const float result = 0.04F * std::sin(time * 4.0F + joint_phase);
    return result;
}

} // namespace sw
