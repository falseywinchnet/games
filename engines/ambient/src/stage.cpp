#include "stage.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace ambient {
namespace {

// ------------------------------------------------------------------ material maps

float channel(const Texture& level, int x, int y, int c) {
    const int wx = x & (level.width - 1);
    const int wy = y & (level.height - 1);
    const float result =
        static_cast<float>(level.rgb[(static_cast<std::size_t>(wy) * static_cast<std::size_t>(level.width) +
                                      static_cast<std::size_t>(wx)) * 3U + static_cast<std::size_t>(c)]) /
        255.0F;
    return result;
}

// ------------------------------------------------------------------ rasterizer policies

// Shadow map: orthographic depth, nearest to the light kept.
struct NearestPolicy {
    static constexpr bool perspective = false;
    int width{};
    float* depth{};
    bool test(int x, int y, float nearness) {
        float& stored = depth[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
        if (nearness <= stored)
            return false;
        stored = nearness;
        return true;
    }
    void cover(const Coverage&, bool) {}
};

// Fixed layer: a visibility buffer of nearest triangle and its weights.
struct VisibilityPolicy {
    static constexpr bool perspective = true;
    int width{};
    float* depth{};
    std::uint32_t* triangle{};
    float* weights{};  // two per sample: weight1, weight2
    std::uint8_t* front{};
    std::uint32_t current{};
    bool test(int x, int y, float inverse_depth) {
        const std::size_t index =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
        const bool result = inverse_depth > depth[index];
        return result;
    }
    void cover(const Coverage& c, bool is_front) {
        const std::size_t index =
            static_cast<std::size_t>(c.y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(c.x);
        depth[index] = c.inverse_depth;
        triangle[index] = current;
        weights[index * 2] = c.weight1;
        weights[index * 2 + 1] = c.weight2;
        front[index] = is_front ? 1 : 0;
    }
};

// Sway layer: Gouraud colour between precomputed vertex shades, which arrive as
// display levels already clamped to 0..255. Foliage triangles cover a few pixels
// at most, so screen-linear weights are indistinguishable from perspective-correct
// ones. Blending is integer arithmetic on packed channels (red with blue, green
// alone); each field's product stays below 2^16, so fields never carry into each other.
struct GouraudPolicy {
    static constexpr bool perspective = false;
    int width{};
    float* depth{};
    std::uint32_t* color{};
    std::uint8_t* lit{};
    const SwayShade* s0{};
    const SwayShade* s1{};
    const SwayShade* s2{};
    std::uint64_t pixels{};
    bool test(int x, int y, float inverse_depth) {
        const std::size_t index =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
        const bool result = inverse_depth > depth[index];
        return result;
    }
    void cover(const Coverage& c, bool is_front) {
        const std::size_t index =
            static_cast<std::size_t>(c.y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(c.x);
        const Rgb& a = is_front ? (*s0).front : (*s0).back;
        const Rgb& b = is_front ? (*s1).front : (*s1).back;
        const Rgb& d = is_front ? (*s2).front : (*s2).back;
        const std::uint32_t r =
            static_cast<std::uint32_t>(a.x * c.weight0 + b.x * c.weight1 + d.x * c.weight2 + 0.5F);
        const std::uint32_t g =
            static_cast<std::uint32_t>(a.y * c.weight0 + b.y * c.weight1 + d.y * c.weight2 + 0.5F);
        const std::uint32_t bl =
            static_cast<std::uint32_t>(a.z * c.weight0 + b.z * c.weight1 + d.z * c.weight2 + 0.5F);
        const float alpha = (*s0).alpha * c.weight0 + (*s1).alpha * c.weight1 + (*s2).alpha * c.weight2;
        depth[index] = c.inverse_depth;
        lit[index] = 0;
        ++pixels;
        const std::uint32_t source = (std::min(r, 255U) << 16U) | (std::min(g, 255U) << 8U) | std::min(bl, 255U);
        if (alpha >= 0.999F) {
            color[index] = source;
            return;
        }
        const std::uint32_t weight = static_cast<std::uint32_t>(alpha * 256.0F + 0.5F);
        const std::uint32_t keep = 256U - weight;
        const std::uint32_t under = color[index];
        const std::uint32_t red_blue = (((source & 0xFF00FFU) * weight + (under & 0xFF00FFU) * keep) >> 8U) & 0xFF00FFU;
        const std::uint32_t green = (((source & 0x00FF00U) * weight + (under & 0x00FF00U) * keep) >> 8U) & 0x00FF00U;
        color[index] = red_blue | green;
    }
};

// Settled foliage in the supersampled fixed layer: display colour per sample,
// blended for see-through leaf tips; leaves take no animated light.
struct RestFoliagePolicy {
    static constexpr bool perspective = false;
    int width{};
    float* depth{};
    std::uint32_t* color{};
    std::uint32_t* boost{};
    const SwayShade* s0{};
    const SwayShade* s1{};
    const SwayShade* s2{};
    bool test(int x, int y, float inverse_depth) {
        const std::size_t index =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
        const bool result = inverse_depth > depth[index];
        return result;
    }
    void cover(const Coverage& c, bool is_front) {
        const std::size_t index =
            static_cast<std::size_t>(c.y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(c.x);
        const Rgb a = is_front ? (*s0).front : (*s0).back;
        const Rgb b = is_front ? (*s1).front : (*s1).back;
        const Rgb d = is_front ? (*s2).front : (*s2).back;
        const Rgb shade = add(add(scale(a, c.weight0), scale(b, c.weight1)), scale(d, c.weight2));
        const float alpha = (*s0).alpha * c.weight0 + (*s1).alpha * c.weight1 + (*s2).alpha * c.weight2;
        depth[index] = c.inverse_depth;
        color[index] = pack_rgb(alpha >= 0.999F ? shade : mix(unpack_rgb(color[index]), shade, alpha));
        boost[index] = 0;
    }
};

// Creatures: nearest fragments listed for shading after scanning.
template <typename Fragment> struct FragmentPolicy {
    static constexpr bool perspective = true;
    int width{};
    float* depth{};
    std::vector<Fragment>* fragments{};
    std::uint32_t part{};
    std::uint32_t triangle{};
    bool test(int x, int y, float inverse_depth) {
        const std::size_t index =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
        const bool result = inverse_depth > depth[index];
        return result;
    }
    void cover(const Coverage& c, bool is_front) {
        const std::uint32_t index = static_cast<std::uint32_t>(c.y) * static_cast<std::uint32_t>(width) +
                                    static_cast<std::uint32_t>(c.x);
        depth[index] = c.inverse_depth;
        const Fragment fragment{index, part, triangle, c.weight0, c.weight1, c.weight2, is_front};
        (*fragments).push_back(fragment);
    }
};

// Orthographic scan (shadow maps): screen points are texel coordinates and the
// "depth" field carries nearness, interpolated linearly.
template <typename Policy> void rasterize_flat(Policy& policy, int width, int height, ScreenPoint a, ScreenPoint b,
                                               ScreenPoint c) {
    const ScreenPoint points[3] = {a, b, c};
    const detail::ClipVertex weights[3] = {{{}, 1, 0, 0}, {{}, 0, 1, 0}, {{}, 0, 0, 1}};
    detail::scan(policy, width, height, points, weights, true);
}

Vec3 decode_normal(const std::int8_t* n) {
    const Vec3 result = normalize({n[0] / 127.0F, n[1] / 127.0F, n[2] / 127.0F});
    return result;
}
Vec3 decode_albedo(const std::uint8_t* rgb) {
    const float r = rgb[0] / 255.0F;
    const float g = rgb[1] / 255.0F;
    const float b = rgb[2] / 255.0F;
    const Vec3 result{r * r, g * g, b * b};
    return result;
}
Vec3 position_of(const float* p) {
    const Vec3 result{p[0], p[1], p[2]};
    return result;
}
Affine affine_of(const float* m) {
    Affine result{};
    for (int k = 0; k < 12; ++k)
        result.m[k] = m[k];
    return result;
}

ScreenPoint light_texel(const LightFrame& frame, int size, Vec3 world) {
    const Vec3 d = subtract(world, frame.origin);
    const float u = dot(d, frame.right) / frame.half_width * 0.5F + 0.5F;
    const float v = 0.5F - dot(d, frame.up) / frame.half_height * 0.5F;
    // Nearness to the light: larger is nearer, as the policies expect.
    const ScreenPoint result{u * static_cast<float>(size), v * static_cast<float>(size), dot(d, frame.toward)};
    return result;
}

// Display colour 0..1 to clamped display levels 0..255, the Gouraud policy's units.
Rgb display_levels(Rgb c) {
    const Rgb result{std::clamp(c.x, 0.0F, 1.0F) * 255.0F, std::clamp(c.y, 0.0F, 1.0F) * 255.0F,
                     std::clamp(c.z, 0.0F, 1.0F) * 255.0F};
    return result;
}

bool sort_key_less(const SortKey& a, const SortKey& b) {
    if (a.depth != b.depth)
        return a.depth < b.depth;
    return a.triangle < b.triangle;
}

// Global index of a fixed triangle: its placement and triangle within the mesh.
struct FixedTriangle {
    std::uint32_t placement{};
    std::uint32_t triangle{};
};

} // namespace

// ------------------------------------------------------------------ material maps

MipChain build_mips(const Texture& texture) {
    MipChain chain{};
    chain.levels.push_back(texture);
    while (chain.levels.back().width > 1 && chain.levels.back().height > 1) {
        const Texture& source = chain.levels.back();
        Texture next{};
        next.width = source.width / 2;
        next.height = source.height / 2;
        next.normal_map = source.normal_map;
        next.rgb.resize(static_cast<std::size_t>(next.width) * static_cast<std::size_t>(next.height) * 3U);
        for (int y = 0; y < next.height; ++y) {
            for (int x = 0; x < next.width; ++x) {
                for (int c = 0; c < 3; ++c) {
                    const std::size_t w = static_cast<std::size_t>(source.width);
                    const std::size_t sx = static_cast<std::size_t>(x) * 2U;
                    const std::size_t sy = static_cast<std::size_t>(y) * 2U;
                    const std::size_t cc = static_cast<std::size_t>(c);
                    const unsigned sum = source.rgb[(sy * w + sx) * 3U + cc] + source.rgb[(sy * w + sx + 1U) * 3U + cc] +
                                         source.rgb[((sy + 1U) * w + sx) * 3U + cc] +
                                         source.rgb[((sy + 1U) * w + sx + 1U) * 3U + cc];
                    next.rgb[(static_cast<std::size_t>(y) * static_cast<std::size_t>(next.width) +
                              static_cast<std::size_t>(x)) * 3U + cc] = static_cast<std::uint8_t>((sum + 2U) / 4U);
                }
            }
        }
        chain.levels.push_back(std::move(next));
    }
    return chain;
}

Vec3 sample(const MipChain& chain, float u, float v, float lod) {
    const int count = static_cast<int>(chain.levels.size());
    const int level_index = std::clamp(static_cast<int>(std::floor(lod + 0.5F)), 0, count - 1);
    const Texture& level = chain.levels[static_cast<std::size_t>(level_index)];
    const float x = u * static_cast<float>(level.width) - 0.5F;
    const float y = v * static_cast<float>(level.height) - 0.5F;
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const float tx = x - fx;
    const float ty = y - fy;
    float out[3]{};
    for (int c = 0; c < 3; ++c) {
        const float top = mix(channel(level, ix, iy, c), channel(level, ix + 1, iy, c), tx);
        const float bottom = mix(channel(level, ix, iy + 1, c), channel(level, ix + 1, iy + 1, c), tx);
        out[c] = mix(top, bottom, ty);
    }
    const Vec3 result{out[0], out[1], out[2]};
    return result;
}

// ------------------------------------------------------------------ shadows

float ShadowMap::visibility(Vec3 world, float bias) const {
    if (size <= 0)
        return 1;
    const ScreenPoint texel = light_texel(frame, size, world);
    const int cx = static_cast<int>(std::floor(texel.x));
    const int cy = static_cast<int>(std::floor(texel.y));
    if (cx < 1 || cy < 1 || cx >= size - 1 || cy >= size - 1)
        return 1;
    const float receiver = texel.inverse_depth + bias;
    int lit = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        const std::size_t row = static_cast<std::size_t>(cy + dy) * static_cast<std::size_t>(size);
        for (int dx = -1; dx <= 1; ++dx) {
            const float occluder = depth[row + static_cast<std::size_t>(cx + dx)];
            if (occluder <= receiver)
                ++lit;
        }
    }
    const float result = static_cast<float>(lit) / 9.0F;
    return result;
}

ShadowMap build_shadow_map(const SceneData& scene, const Look& look, int size) {
    ShadowMap map{};
    map.frame = look.light_frame();
    map.size = size;
    map.depth.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size),
                     -std::numeric_limits<float>::infinity());
    NearestPolicy policy{size, map.depth.data()};
    std::vector<ScreenPoint> texels{};
    for (const StaticPlacementRecord& placement : scene.placements) {
        const Mesh<StaticVertex>& mesh = scene.static_meshes[placement.mesh];
        const Affine transform = affine_of(placement.transform);
        texels.resize(mesh.vertices.size());
        for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
            const Vec3 world = apply(transform, position_of(mesh.vertices[index].position));
            texels[index] = light_texel(map.frame, size, world);
        }
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
            rasterize_flat(policy, size, size, texels[mesh.indices[t]], texels[mesh.indices[t + 1]],
                           texels[mesh.indices[t + 2]]);
    }
    texels.resize(scene.sway.vertices.size());
    for (std::size_t index = 0; index < scene.sway.vertices.size(); ++index)
        texels[index] = light_texel(map.frame, size, position_of(scene.sway.vertices[index].position));
    for (std::size_t t = 0; t + 2 < scene.sway.indices.size(); t += 3)
        rasterize_flat(policy, size, size, texels[scene.sway.indices[t]], texels[scene.sway.indices[t + 1]],
                       texels[scene.sway.indices[t + 2]]);
    return map;
}

// ------------------------------------------------------------------ the fixed layer

FixedLayer build_fixed_layer(const SceneData& scene, const Look& look, const ShadowMap& shadows,
                             const Foliage& foliage, int width, int height, int supersample,
                             float settle_threshold_pixels) {
    FixedLayer layer{};
    if (width <= 0 || height <= 0)
        return layer;
    const int ss = std::clamp(supersample, 1, 4);
    const int big_w = width * ss;
    const int big_h = height * ss;
    const std::size_t samples = static_cast<std::size_t>(big_w) * static_cast<std::size_t>(big_h);
    const Projection projection = make_projection(scene.camera, big_w, big_h);

    std::vector<float> depth(samples, 0.0F);
    std::vector<std::uint32_t> triangle(samples, 0);
    std::vector<float> weights(samples * 2U, 0.0F);
    std::vector<std::uint8_t> front(samples, 0);
    std::vector<FixedTriangle> triangles{};
    triangles.reserve(400000);
    VisibilityPolicy policy{big_w, depth.data(), triangle.data(), weights.data(), front.data(), 0};
    std::vector<ViewPoint> view{};
    for (std::size_t p = 0; p < scene.placements.size(); ++p) {
        const StaticPlacementRecord& placement = scene.placements[p];
        const Mesh<StaticVertex>& mesh = scene.static_meshes[placement.mesh];
        const Affine transform = affine_of(placement.transform);
        const Cull cull = look.closed_fixed(placement.material) ? Cull::back : Cull::none;
        view.resize(mesh.vertices.size());
        for (std::size_t index = 0; index < mesh.vertices.size(); ++index)
            view[index] = to_view(projection, apply(transform, position_of(mesh.vertices[index].position)));
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const FixedTriangle record{static_cast<std::uint32_t>(p), static_cast<std::uint32_t>(t / 3)};
            triangles.push_back(record);
            policy.current = static_cast<std::uint32_t>(triangles.size());  // 0 means none
            rasterize(policy, projection, view[mesh.indices[t]], view[mesh.indices[t + 1]], view[mesh.indices[t + 2]],
                      cull);
        }
    }

    // Shade every visible sample once. Colours are kept as display levels (packed),
    // which bounds the build's transient memory; four samples average to one pixel.
    std::vector<std::uint32_t> color(samples, 0);
    std::vector<std::uint32_t> boost(samples, 0);
    std::vector<float> light_xz(samples * 2U, 0.0F);
    for (int y = 0; y < big_h; ++y) {
        for (int x = 0; x < big_w; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(big_w) +
                                      static_cast<std::size_t>(x);
            const std::uint32_t id = triangle[index];
            if (id == 0) {
                color[index] = pack_rgb(look.background((static_cast<float>(x) + 0.5F) / static_cast<float>(big_w),
                                                        (static_cast<float>(y) + 0.5F) / static_cast<float>(big_h)));
                continue;
            }
            const FixedTriangle record = triangles[id - 1];
            const StaticPlacementRecord& placement = scene.placements[record.placement];
            const Mesh<StaticVertex>& mesh = scene.static_meshes[placement.mesh];
            const Affine transform = affine_of(placement.transform);
            const StaticVertex& v0 = mesh.vertices[mesh.indices[record.triangle * 3U]];
            const StaticVertex& v1 = mesh.vertices[mesh.indices[record.triangle * 3U + 1U]];
            const StaticVertex& v2 = mesh.vertices[mesh.indices[record.triangle * 3U + 2U]];
            const float w1 = weights[index * 2];
            const float w2 = weights[index * 2 + 1];
            const float w0 = 1.0F - w1 - w2;
            const Vec3 p0 = apply(transform, position_of(v0.position));
            const Vec3 p1 = apply(transform, position_of(v1.position));
            const Vec3 p2 = apply(transform, position_of(v2.position));
            FixedSample s{};
            s.world = add(add(scale(p0, w0), scale(p1, w1)), scale(p2, w2));
            const Vec3 n0 = decode_normal(v0.normal);
            const Vec3 n1 = decode_normal(v1.normal);
            const Vec3 n2 = decode_normal(v2.normal);
            const Vec3 local_normal = add(add(scale(n0, w0), scale(n1, w1)), scale(n2, w2));
            s.front = front[index] != 0;
            Vec3 normal = apply_normal(transform, local_normal);
            if (!s.front)
                normal = scale(normal, -1);
            s.normal = normal;
            const float us = placement.uv_scale[0];
            const float vs = placement.uv_scale[1];
            const float u0 = v0.uv[0] * us;
            const float u1 = v1.uv[0] * us;
            const float u2 = v2.uv[0] * us;
            const float t0 = v0.uv[1] * vs;
            const float t1 = v1.uv[1] * vs;
            const float t2 = v2.uv[1] * vs;
            s.u = u0 * w0 + u1 * w1 + u2 * w2;
            s.v = t0 * w0 + t1 * w1 + t2 * w2;
            // Tangent frame and texture footprint from the whole triangle.
            const Vec3 e1 = subtract(p1, p0);
            const Vec3 e2 = subtract(p2, p0);
            const float du1 = u1 - u0;
            const float dv1 = t1 - t0;
            const float du2 = u2 - u0;
            const float dv2 = t2 - t0;
            const float determinant = du1 * dv2 - du2 * dv1;
            if (std::abs(determinant) > 1e-12F) {
                const float r = 1.0F / determinant;
                s.tangent = scale(subtract(scale(e1, dv2), scale(e2, dv1)), r);
                s.bitangent = scale(subtract(scale(e2, du1), scale(e1, du2)), r);
            }
            const ScreenPoint q0 = to_screen(projection, to_view(projection, p0));
            const ScreenPoint q1 = to_screen(projection, to_view(projection, p1));
            const ScreenPoint q2 = to_screen(projection, to_view(projection, p2));
            const float screen_area =
                std::abs((q1.x - q0.x) * (q2.y - q0.y) - (q1.y - q0.y) * (q2.x - q0.x)) * 0.5F;
            const float uv_area = std::abs(determinant) * 0.5F * 256.0F * 256.0F;
            s.texture_lod = screen_area > 1e-6F ? 0.5F * std::log2(std::max(uv_area / screen_area, 1e-6F)) : 8.0F;
            const Vec3 a0 = decode_albedo(v0.albedo);
            const Vec3 a1 = decode_albedo(v1.albedo);
            const Vec3 a2 = decode_albedo(v2.albedo);
            const Vec3 tint{placement.tint[0], placement.tint[1], placement.tint[2]};
            s.albedo = multiply(add(add(scale(a0, w0), scale(a1, w1)), scale(a2, w2)), tint);
            s.moss = (v0.moss * w0 + v1.moss * w1 + v2.moss * w2) / 255.0F;
            s.material = placement.material;
            s.light_visibility = shadows.visibility(s.world, 0.12F);
            const FixedShade shade = look.shade_fixed(s, scene);
            color[index] = pack_rgb(shade.color);
            boost[index] = pack_rgb(shade.light_boost);
            light_xz[index * 2] = s.world.x;
            light_xz[index * 2 + 1] = s.world.z;
        }
    }

    // Foliage that cannot move a visible fraction of a pixel at this size is drawn
    // here once, at rest, with its rest shading.
    const Projection display_projection = make_projection(scene.camera, width, height);
    std::vector<std::uint8_t> settled =
        settle_foliage(scene, foliage, display_projection, settle_threshold_pixels);
    {
        const std::vector<SwayVertex>& vertices = scene.sway.vertices;
        std::vector<ViewPoint> rest_view(vertices.size());
        for (std::size_t index = 0; index < vertices.size(); ++index)
            rest_view[index] = to_view(projection, position_of(vertices[index].position));
        RestFoliagePolicy rest{big_w, depth.data(), color.data(), boost.data(), nullptr, nullptr, nullptr};
        const std::vector<std::uint32_t>& indices = scene.sway.indices;
        for (const std::uint32_t triangle : foliage.order) {
            if (settled[triangle] == 0)
                continue;
            const std::size_t t = static_cast<std::size_t>(triangle) * 3U;
            rest.s0 = &foliage.rest[indices[t]];
            rest.s1 = &foliage.rest[indices[t + 1]];
            rest.s2 = &foliage.rest[indices[t + 2]];
            rasterize(rest, projection, rest_view[indices[t]], rest_view[indices[t + 1]], rest_view[indices[t + 2]],
                      Cull::none);
        }
    }

    // Resolve the supersamples: averaged colour, nearest depth.
    layer.width = width;
    layer.height = height;
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    layer.color.assign(pixels, 0);
    layer.depth.assign(pixels, 0.0F);
    layer.boost.assign(pixels, 0);
    layer.light_position.assign(pixels * 2U, 0.0F);
    const float inverse_count = 1.0F / static_cast<float>(ss * ss);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Rgb sum{};
            Rgb boost_sum{};
            float nearest = 0;
            float lx = 0;
            float lz = 0;
            int lit = 0;
            for (int sy = 0; sy < ss; ++sy) {
                for (int sx = 0; sx < ss; ++sx) {
                    const std::size_t index = static_cast<std::size_t>(y * ss + sy) * static_cast<std::size_t>(big_w) +
                                              static_cast<std::size_t>(x * ss + sx);
                    sum = add(sum, unpack_rgb(color[index]));
                    boost_sum = add(boost_sum, unpack_rgb(boost[index]));
                    nearest = std::max(nearest, depth[index]);
                    if (boost[index] != 0) {
                        lx += light_xz[index * 2];
                        lz += light_xz[index * 2 + 1];
                        ++lit;
                    }
                }
            }
            const std::size_t out = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                    static_cast<std::size_t>(x);
            layer.color[out] = pack_rgb(scale(sum, inverse_count));
            layer.depth[out] = nearest;
            const std::uint32_t packed_boost = pack_rgb(scale(boost_sum, inverse_count));
            layer.boost[out] = lit > 0 ? packed_boost : 0;
            if (lit > 0) {
                layer.light_position[out * 2] = lx / static_cast<float>(lit);
                layer.light_position[out * 2 + 1] = lz / static_cast<float>(lit);
            }
        }
    }
    layer.settled = std::move(settled);
    return layer;
}

// ------------------------------------------------------------------ projection

Projection make_projection(const CameraSetup& camera, int width, int height) {
    Projection projection{};
    projection.width = width;
    projection.height = height;
    projection.eye = camera.eye;
    projection.forward = normalize(subtract(camera.target, camera.eye));
    projection.right = normalize(cross(projection.forward, {0, 1, 0}));
    projection.up = cross(projection.right, projection.forward);
    const float half_angle = camera.field_of_view_degrees * 3.14159265F / 360.0F;
    projection.focal = static_cast<float>(height) * 0.5F / std::tan(half_angle);
    projection.near_plane = camera.near_plane;
    return projection;
}

// ------------------------------------------------------------------ the stage

// ------------------------------------------------------------------ foliage

Foliage prepare_foliage(const SceneData& scene, const Look& look, const ShadowMap& shadows) {
    Foliage foliage{};
    prepare_current(scene, foliage.phases, foliage.current);
    const std::vector<SwayVertex>& vertices = scene.sway.vertices;
    // Lighting, linearized in the bend. The current tilts each normal by `slope`
    // along the ribbon; the shade is evaluated at rest and at a small tilt, and each
    // update extrapolates linearly. The shadow map holds the foliage at rest, so each
    // vertex's direct-light visibility is taken once, at rest. The bend's effect on
    // fog distance (a few hundredths of a unit) is ignored.
    constexpr float probe = 0.2F;
    foliage.rest.resize(vertices.size());
    foliage.gradient.resize(vertices.size());
    foliage.reach.resize(vertices.size());
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        const SwayVertex& source = vertices[index];
        const Vec3 bend{source.bend[0] / 127.0F, source.bend[1] / 127.0F, source.bend[2] / 127.0F};
        const Vec3 along{source.along[0] / 127.0F, source.along[1] / 127.0F, source.along[2] / 127.0F};
        const Vec3 rest_normal = decode_normal(source.normal);
        SwaySample sample{};
        sample.world = position_of(source.position);
        sample.normal = rest_normal;
        sample.albedo = decode_albedo(source.albedo);
        sample.translucency = source.translucency / 255.0F;
        sample.u = source.uv[0] / 65535.0F;
        sample.light_visibility = shadows.visibility(sample.world, 0.12F);
        const SwayShade rest = look.shade_sway(sample);
        sample.normal = normalize(subtract(rest_normal, scale(along, probe * dot(bend, rest_normal))));
        const SwayShade tilted = look.shade_sway(sample);
        foliage.rest[index] = rest;
        SwayShade gradient{};
        gradient.front = scale(subtract(tilted.front, rest.front), 1.0F / probe);
        gradient.back = scale(subtract(tilted.back, rest.back), 1.0F / probe);
        gradient.alpha = 0;
        foliage.gradient[index] = gradient;
        foliage.reach[index] = current_reach(foliage.current[index]);
    }

    // Draw order. Each plant (root) is ordered by its mean depth and keeps its own
    // triangles in archive order, which preserves memory locality. Plants with
    // see-through leaf tips (most of Riverscape's grass) are drawn farthest first so
    // their blending layers correctly; fully opaque plants are drawn first, nearest
    // first, so what they hide fails the depth test early. Sorted once at rest; the
    // sway is too small to change the order meaningfully.
    const std::vector<std::uint32_t>& indices = scene.sway.indices;
    const std::size_t count = indices.size() / 3;
    const std::size_t roots = scene.sway_roots.size();
    const Vec3 forward = normalize(subtract(scene.camera.target, scene.camera.eye));
    std::vector<double> depth_sum(roots, 0.0);
    std::vector<std::uint32_t> triangles(roots, 0);
    std::vector<std::uint8_t> see_through(roots, 0);
    for (std::size_t t = 0; t < count; ++t) {
        const std::uint32_t root = vertices[indices[t * 3]].root;
        for (std::size_t k = 0; k < 3; ++k) {
            const std::uint32_t vertex = indices[t * 3 + k];
            depth_sum[root] += dot(subtract(position_of(vertices[vertex].position), scene.camera.eye), forward);
            if (foliage.rest[vertex].alpha < 0.999F)
                see_through[root] = 1;
        }
        ++triangles[root];
    }
    std::vector<SortKey> opaque{};
    std::vector<SortKey> translucent{};
    for (std::size_t root = 0; root < roots; ++root) {
        if (triangles[root] == 0)
            continue;
        const float depth = static_cast<float>(depth_sum[root] / (3.0 * triangles[root]));
        if (see_through[root] != 0)
            translucent.push_back({-depth, static_cast<std::uint32_t>(root)});
        else
            opaque.push_back({depth, static_cast<std::uint32_t>(root)});
    }
    std::sort(opaque.begin(), opaque.end(), sort_key_less);
    std::sort(translucent.begin(), translucent.end(), sort_key_less);
    // Bucket triangles by root in archive order, then emit roots in sorted order.
    std::vector<std::uint32_t> first(roots + 1, 0);
    for (std::size_t root = 0; root < roots; ++root)
        first[root + 1] = first[root] + triangles[root];
    std::vector<std::uint32_t> cursor(first.begin(), first.end() - 1);
    std::vector<std::uint32_t> by_root(count, 0);
    for (std::size_t t = 0; t < count; ++t) {
        const std::uint32_t root = vertices[indices[t * 3]].root;
        by_root[cursor[root]] = static_cast<std::uint32_t>(t);
        ++cursor[root];
    }
    foliage.order.reserve(count);
    for (const SortKey& key : opaque)
        for (std::uint32_t k = first[key.triangle]; k < first[key.triangle + 1]; ++k)
            foliage.order.push_back(by_root[k]);
    for (const SortKey& key : translucent)
        for (std::uint32_t k = first[key.triangle]; k < first[key.triangle + 1]; ++k)
            foliage.order.push_back(by_root[k]);
    return foliage;
}

std::vector<std::uint8_t> settle_foliage(const SceneData& scene, const Foliage& foliage, const Projection& projection,
                                         float threshold_pixels) {
    const std::vector<SwayVertex>& vertices = scene.sway.vertices;
    std::vector<std::uint8_t> still(vertices.size(), 0);
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        const ViewPoint view = to_view(projection, position_of(vertices[index].position));
        if (view.depth <= projection.near_plane)
            continue;
        const float pixels = foliage.reach[index] * projection.focal / view.depth;
        still[index] = pixels < threshold_pixels ? 1 : 0;
    }
    const std::vector<std::uint32_t>& indices = scene.sway.indices;
    std::vector<std::uint8_t> settled(indices.size() / 3, 0);
    for (std::size_t t = 0; t < settled.size(); ++t)
        settled[t] = (still[indices[t * 3]] & still[indices[t * 3 + 1]] & still[indices[t * 3 + 2]]);
    return settled;
}

// ------------------------------------------------------------------ the stage

Stage::Stage(const SceneData& scene, const Look& look, const Foliage& foliage)
    : scene_(scene), look_(look), foliage_(foliage), roots_(foliage.phases) {
    sway_view_.resize(scene_.sway.vertices.size());
    sway_screen_.resize(scene_.sway.vertices.size());
    sway_in_front_.resize(scene_.sway.vertices.size());
    sway_shade_.resize(scene_.sway.vertices.size());
    std::size_t total = 0;
    part_vertex_base_.resize(scene_.parts.size());
    for (std::size_t index = 0; index < scene_.parts.size(); ++index) {
        part_vertex_base_[index] = static_cast<std::uint32_t>(total);
        total += scene_.actor_meshes[scene_.parts[index].mesh].vertices.size();
    }
    actor_vertices_.resize(total);
    fragments_.reserve(65536);
    // Per mesh: normals decoded once. Per part: the normal transform (the linear part
    // with each column divided by its squared length, as the original shader did).
    mesh_normals_.resize(scene_.actor_meshes.size());
    for (std::size_t m = 0; m < scene_.actor_meshes.size(); ++m) {
        const std::vector<ActorVertex>& vertices = scene_.actor_meshes[m].vertices;
        mesh_normals_[m].resize(vertices.size());
        for (std::size_t index = 0; index < vertices.size(); ++index)
            mesh_normals_[m][index] = decode_normal(vertices[index].normal);
    }
    part_normal_transform_.resize(scene_.parts.size());
    creature_radius_.assign(scene_.actors.size(), 0.0F);
    for (std::size_t p = 0; p < scene_.parts.size(); ++p) {
        const ActorPartRecord& part = scene_.parts[p];
        const Affine transform = affine_of(part.transform);
        Affine normal_transform = transform;
        for (int column = 0; column < 3; ++column) {
            const float squared = transform.m[column] * transform.m[column] + transform.m[4 + column] * transform.m[4 + column] +
                                  transform.m[8 + column] * transform.m[8 + column];
            const float inverse = 1.0F / std::max(squared, 1e-5F);
            normal_transform.m[column] *= inverse;
            normal_transform.m[4 + column] *= inverse;
            normal_transform.m[8 + column] *= inverse;
        }
        part_normal_transform_[p] = normal_transform;
        // Bounding radius of the creature in its own space, with room for deformation.
        float reach = 0;
        for (const ActorVertex& vertex : scene_.actor_meshes[part.mesh].vertices)
            reach = std::max(reach, length(apply(transform, position_of(vertex.position))));
        float& radius = creature_radius_[static_cast<std::size_t>(part.actor)];
        radius = std::max(radius, reach + 0.15F);
    }
}

void Stage::adopt(FixedLayer layer) {
    fixed_ = std::move(layer);
    projection_ = make_projection(scene_.camera, fixed_.width, fixed_.height);
    const std::size_t pixels = static_cast<std::size_t>(fixed_.width) * static_cast<std::size_t>(fixed_.height);
    sway_color_.assign(pixels, 0);
    sway_depth_.assign(pixels, 0.0F);
    frame_color_.assign(pixels, 0);
    frame_depth_.assign(pixels, 0.0F);
    lit_pixels_.clear();
    lit_pixels_.reserve(pixels);
    // Only foliage the fixed layer did not settle is redrawn on the sway cadence.
    const std::vector<std::uint32_t>& indices = scene_.sway.indices;
    moving_order_.clear();
    std::vector<std::uint8_t> used(scene_.sway.vertices.size(), 0);
    for (const std::uint32_t triangle : foliage_.order) {
        if (triangle < fixed_.settled.size() && fixed_.settled[triangle] != 0)
            continue;
        moving_order_.push_back(triangle);
        for (std::size_t k = 0; k < 3; ++k)
            used[indices[static_cast<std::size_t>(triangle) * 3U + k]] = 1;
    }
    moving_vertices_.clear();
    for (std::size_t index = 0; index < used.size(); ++index) {
        if (used[index] != 0)
            moving_vertices_.push_back(static_cast<std::uint32_t>(index));
    }
    ++counters_.fixed_builds;
}

void Stage::update_sway(double time) {
    if (!ready())
        return;
    const std::size_t pixels = fixed_.color.size();
    std::memcpy(sway_color_.data(), fixed_.color.data(), pixels * sizeof(std::uint32_t));
    std::memcpy(sway_depth_.data(), fixed_.depth.data(), pixels * sizeof(float));
    std::vector<std::uint8_t>& lit = lit_mask_;
    lit.resize(pixels);
    for (std::size_t index = 0; index < pixels; ++index)
        lit[index] = fixed_.boost[index] != 0 ? 1 : 0;

    update_current_roots(scene_.sway_roots, time, roots_);
    const std::vector<SwayVertex>& vertices = scene_.sway.vertices;
    for (const std::uint32_t index : moving_vertices_) {
        const SwayVertex& source = vertices[index];
        const Sway sway = current_sway(roots_[source.root], foliage_.current[index]);
        const Vec3 bend{source.bend[0] / 127.0F, source.bend[1] / 127.0F, source.bend[2] / 127.0F};
        const Vec3 world = add(position_of(source.position), scale(bend, sway.amount));
        const ViewPoint view = to_view(projection_, world);
        sway_view_[index] = view;
        sway_in_front_[index] = view.depth >= projection_.near_plane ? 1 : 0;
        if (sway_in_front_[index] != 0)
            sway_screen_[index] = to_screen(projection_, view);
        const SwayShade& rest = foliage_.rest[index];
        const SwayShade& gradient = foliage_.gradient[index];
        SwayShade& shade = sway_shade_[index];
        shade.front = display_levels(add(rest.front, scale(gradient.front, sway.slope)));
        shade.back = display_levels(add(rest.back, scale(gradient.back, sway.slope)));
        shade.alpha = rest.alpha;
    }
    GouraudPolicy policy{};
    policy.width = fixed_.width;
    policy.depth = sway_depth_.data();
    policy.color = sway_color_.data();
    policy.lit = lit.data();
    const std::vector<std::uint32_t>& indices = scene_.sway.indices;
    for (const std::uint32_t triangle : moving_order_) {
        const std::size_t t = static_cast<std::size_t>(triangle) * 3U;
        const std::uint32_t i0 = indices[t];
        const std::uint32_t i1 = indices[t + 1];
        const std::uint32_t i2 = indices[t + 2];
        policy.s0 = &sway_shade_[i0];
        policy.s1 = &sway_shade_[i1];
        policy.s2 = &sway_shade_[i2];
        if ((sway_in_front_[i0] & sway_in_front_[i1] & sway_in_front_[i2]) != 0)
            rasterize_projected(policy, projection_, sway_screen_[i0], sway_screen_[i1], sway_screen_[i2], Cull::none);
        else
            rasterize(policy, projection_, sway_view_[i0], sway_view_[i1], sway_view_[i2], Cull::none);
    }
    lit_pixels_.clear();
    for (std::size_t index = 0; index < pixels; ++index) {
        if (lit[index] != 0)
            lit_pixels_.push_back(static_cast<std::uint32_t>(index));
    }
    counters_.sway_triangles += moving_order_.size();
    ++counters_.sway_updates;
}

void Stage::compose(double time, double light_time, const std::vector<Creature>& creatures) {
    if (!ready())
        return;
    const std::size_t pixels = sway_color_.size();
    std::memcpy(frame_color_.data(), sway_color_.data(), pixels * sizeof(std::uint32_t));
    std::memcpy(frame_depth_.data(), sway_depth_.data(), pixels * sizeof(float));
    const float t = static_cast<float>(light_time);
    for (const std::uint32_t index : lit_pixels_) {
        const float strength = look_.animated_light(fixed_.light_position[index * 2U],
                                                    fixed_.light_position[index * 2U + 1U], t);
        if (strength <= 0.002F)
            continue;
        const std::uint32_t base = frame_color_[index];
        const std::uint32_t add_light = fixed_.boost[index];
        std::uint32_t out = 0;
        for (std::uint32_t shift = 0; shift <= 16; shift += 8) {
            const float channel_value = static_cast<float>((base >> shift) & 255U) +
                                        static_cast<float>((add_light >> shift) & 255U) * strength;
            const std::uint32_t c = static_cast<std::uint32_t>(std::min(255.0F, channel_value + 0.5F));
            out |= c << shift;
        }
        frame_color_[index] = out;
    }
    draw_actors(time, creatures);
    draw_risers(time);
    for (std::uint32_t& pixel : frame_color_)
        pixel |= 0xFF000000U;
    ++counters_.frames;
}

void Stage::draw_actors(double time, const std::vector<Creature>& creatures) {
    const float t = static_cast<float>(time);
    // Which creatures are in view at all: a bounding sphere around each pose. A
    // creature wholly outside the view costs nothing more this frame.
    creature_visible_.assign(creatures.size(), 0);
    poses_.resize(creatures.size());
    const float width = static_cast<float>(projection_.width);
    const float height = static_cast<float>(projection_.height);
    for (std::size_t index = 0; index < creatures.size(); ++index) {
        poses_[index] = creature_pose(creatures[index], time);
        if (index >= creature_radius_.size())
            continue;
        const Pose& pose = poses_[index];
        const float radius = creature_radius_[index] * static_cast<float>(creatures[index].scale);
        const ViewPoint view = to_view(projection_, {static_cast<float>(pose.position.x),
                                                     static_cast<float>(pose.position.y),
                                                     static_cast<float>(pose.position.z)});
        if (view.depth + radius <= projection_.near_plane)
            continue;
        if (view.depth - radius <= projection_.near_plane) {
            creature_visible_[index] = 1;
            continue;
        }
        const ScreenPoint centre = to_screen(projection_, view);
        const float reach = radius * projection_.focal / (view.depth - radius);
        const bool outside = centre.x + reach < 0 || centre.x - reach > width || centre.y + reach < 0 ||
                             centre.y - reach > height;
        creature_visible_[index] = outside ? 0 : 1;
    }
    // Pose every visible part's vertices in world and camera space.
    for (std::size_t p = 0; p < scene_.parts.size(); ++p) {
        const ActorPartRecord& part = scene_.parts[p];
        const Mesh<ActorVertex>& mesh = scene_.actor_meshes[part.mesh];
        const std::size_t actor = static_cast<std::size_t>(part.actor);
        if (actor >= creatures.size() || creature_visible_[actor] == 0)
            continue;
        const Pose& pose = poses_[actor];
        const float c = static_cast<float>(std::cos(pose.heading));
        const float s = static_cast<float>(std::sin(pose.heading));
        const float size = static_cast<float>(creatures[actor].scale);
        const Vec3 position{static_cast<float>(pose.position.x), static_cast<float>(pose.position.y),
                            static_cast<float>(pose.position.z)};
        const Affine transform = affine_of(part.transform);
        const Affine& normal_transform = part_normal_transform_[p];
        const std::vector<Vec3>& normals = mesh_normals_[part.mesh];
        const float lift = look_.joint_lift(part.joint, part.joint_phase, t);
        const std::uint32_t base = part_vertex_base_[p];
        for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
            const ActorVertex& vertex = mesh.vertices[index];
            Vec3 local = position_of(vertex.position);
            Vec3 normal = normals[index];
            look_.deform_actor(part.material, vertex, static_cast<float>(part.actor), t, local, normal);
            Vec3 placed = apply(transform, local);
            placed.y += lift;
            const Vec3 world = add(rotate_y(scale(placed, size), c, s), position);
            ActorVertexOut& out = actor_vertices_[base + index];
            out.world = world;
            out.normal = rotate_y(normalize(apply_linear(normal_transform, normal)), c, s);
            out.local = local;
            out.view = to_view(projection_, world);
        }
    }
    fragments_.clear();
    FragmentPolicy<Fragment> policy{};
    policy.width = projection_.width;
    policy.depth = frame_depth_.data();
    policy.fragments = &fragments_;
    for (std::size_t p = 0; p < scene_.parts.size(); ++p) {
        const ActorPartRecord& part = scene_.parts[p];
        const std::size_t actor = static_cast<std::size_t>(part.actor);
        if (actor >= creatures.size() || creature_visible_[actor] == 0)
            continue;
        const Mesh<ActorVertex>& mesh = scene_.actor_meshes[part.mesh];
        const std::uint32_t base = part_vertex_base_[p];
        const Cull cull = look_.closed_actor(part.material) ? Cull::back : Cull::none;
        policy.part = static_cast<std::uint32_t>(p);
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            policy.triangle = static_cast<std::uint32_t>(t / 3);
            rasterize(policy, projection_, actor_vertices_[base + mesh.indices[t]].view,
                      actor_vertices_[base + mesh.indices[t + 1]].view,
                      actor_vertices_[base + mesh.indices[t + 2]].view, cull);
        }
    }
    // Shade in scan order: a nearer fragment always comes after what it covers.
    const float inverse_height = 1.0F / static_cast<float>(projection_.height);
    for (const Fragment& fragment : fragments_) {
        const ActorPartRecord& part = scene_.parts[fragment.part];
        const Mesh<ActorVertex>& mesh = scene_.actor_meshes[part.mesh];
        const std::uint32_t base = part_vertex_base_[fragment.part];
        const std::uint32_t i0 = mesh.indices[fragment.triangle * 3U];
        const std::uint32_t i1 = mesh.indices[fragment.triangle * 3U + 1U];
        const std::uint32_t i2 = mesh.indices[fragment.triangle * 3U + 2U];
        const ActorVertexOut& a = actor_vertices_[base + i0];
        const ActorVertexOut& b = actor_vertices_[base + i1];
        const ActorVertexOut& d = actor_vertices_[base + i2];
        const float w0 = fragment.weight0;
        const float w1 = fragment.weight1;
        const float w2 = fragment.weight2;
        ActorSample sample{};
        sample.world = add(add(scale(a.world, w0), scale(b.world, w1)), scale(d.world, w2));
        Vec3 normal = normalize(add(add(scale(a.normal, w0), scale(b.normal, w1)), scale(d.normal, w2)));
        if (!fragment.front)
            normal = scale(normal, -1);
        sample.normal = normal;
        sample.local = add(add(scale(a.local, w0), scale(b.local, w1)), scale(d.local, w2));
        const ActorVertex& va = mesh.vertices[i0];
        const ActorVertex& vb = mesh.vertices[i1];
        const ActorVertex& vd = mesh.vertices[i2];
        sample.u = (va.uv[0] * w0 + vb.uv[0] * w1 + vd.uv[0] * w2) / 65535.0F;
        sample.v = (va.uv[1] * w0 + vb.uv[1] * w1 + vd.uv[1] * w2) / 65535.0F;
        sample.part = static_cast<float>(va.part) * w0 + static_cast<float>(vb.part) * w1 +
                      static_cast<float>(vd.part) * w2;
        sample.fin = (va.fin * w0 + vb.fin * w1 + vd.fin * w2) / 255.0F;
        sample.tint = {part.tint[0], part.tint[1], part.tint[2]};
        sample.material = part.material;
        sample.screen_v = static_cast<float>(fragment.pixel / static_cast<std::uint32_t>(projection_.width)) *
                          inverse_height;
        float alpha = 1;
        const Rgb shade = look_.shade_actor(sample, alpha);
        if (alpha >= 0.999F) {
            frame_color_[fragment.pixel] = pack_rgb(shade);
        } else {
            const Rgb under = unpack_rgb(frame_color_[fragment.pixel]);
            frame_color_[fragment.pixel] = pack_rgb(mix(under, shade, alpha));
        }
    }
    counters_.actor_fragments += fragments_.size();
}

void Stage::draw_risers(double time) {
    const int width = projection_.width;
    const int height = projection_.height;
    for (const RiserRecord& riser : scene_.risers) {
        const double cycle = time * riser.speed + riser.phase;
        const double h = cycle - std::floor(cycle / riser.height) * riser.height;
        const Vec3 world{riser.base[0] + static_cast<float>(0.10 * std::sin(h * 2 + riser.phase)),
                         riser.base[1] + static_cast<float>(h), riser.base[2]};
        const ViewPoint view = to_view(projection_, world);
        if (view.depth <= projection_.near_plane)
            continue;
        const ScreenPoint center = to_screen(projection_, view);
        const float radius = riser.size * projection_.focal / view.depth;
        if (radius < 0.35F)
            continue;
        const int x0 = std::max(0, static_cast<int>(std::floor(center.x - radius)));
        const int x1 = std::min(width - 1, static_cast<int>(std::ceil(center.x + radius)));
        const int y0 = std::max(0, static_cast<int>(std::floor(center.y - radius)));
        const int y1 = std::min(height - 1, static_cast<int>(std::ceil(center.y + radius)));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float dx = (static_cast<float>(x) + 0.5F - center.x) / radius;
                const float dy = (static_cast<float>(y) + 0.5F - center.y) / radius;
                const float r2 = dx * dx + dy * dy;
                if (r2 >= 1)
                    continue;
                const std::size_t index =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                if (center.inverse_depth <= frame_depth_[index])
                    continue;
                const float dz = std::sqrt(1 - r2);
                const Vec3 normal = normalize(add(add(scale(projection_.right, dx), scale(projection_.up, -dy)),
                                                  scale(projection_.forward, -dz)));
                const Rgb shade = look_.shade_riser(world, normal, static_cast<float>(y) / static_cast<float>(height));
                // A soft rim keeps one- and two-pixel bubbles from flickering.
                const float alpha = std::clamp((1 - r2) * 2.5F, 0.0F, 1.0F);
                const Rgb under = unpack_rgb(frame_color_[index]);
                frame_color_[index] = pack_rgb(mix(under, shade, alpha));
            }
        }
    }
}

} // namespace ambient
