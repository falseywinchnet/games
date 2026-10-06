#include "treasure.hpp"

#include "riverscape_look.hpp"

#include <algorithm>
#include <cmath>

namespace sw {
namespace {

using ambient::Mesh;
using ambient::StaticVertex;
using ambient::Vec3;

constexpr float pi = 3.14159265F;

// Where the chest sits: on the open sand left of the driftwood, in front of the
// grass, turned a little toward the glass.
constexpr float chest_x = 0.2F;
constexpr float chest_z = 0.95F;
constexpr float chest_yaw = 0.42F;     // radians about y
constexpr float chest_sink = 0.13F;    // how deep the sand has taken it
constexpr float chest_size = 1.15F;
constexpr float lid_open = 0.62F;      // radians the lid stands open

// Chest proportions, in its own units: width along x, depth along z, front +z.
constexpr float half_w = 0.5F;
constexpr float half_d = 0.31F;
constexpr float body_h = 0.42F;
constexpr float lid_rise = 0.17F;  // the barrel top's height above the body

std::uint8_t encode_albedo(float linear) {
    const float value = std::sqrt(std::clamp(linear, 0.0F, 1.0F)) * 255.0F + 0.5F;
    return static_cast<std::uint8_t>(value);
}

std::int8_t encode_normal(float n) {
    return static_cast<std::int8_t>(std::lround(std::clamp(n, -1.0F, 1.0F) * 127.0F));
}

struct Builder {
    Mesh<StaticVertex> mesh{};
    Vec3 albedo{0.5F, 0.5F, 0.5F};
    std::uint8_t moss{};

    std::uint32_t vertex(Vec3 p, Vec3 n, float u, float v) {
        StaticVertex out{};
        out.position[0] = p.x;
        out.position[1] = p.y;
        out.position[2] = p.z;
        const Vec3 unit = ambient::normalize(n);
        out.normal[0] = encode_normal(unit.x);
        out.normal[1] = encode_normal(unit.y);
        out.normal[2] = encode_normal(unit.z);
        out.moss = moss;
        out.uv[0] = u;
        out.uv[1] = v;
        out.albedo[0] = encode_albedo(albedo.x);
        out.albedo[1] = encode_albedo(albedo.y);
        out.albedo[2] = encode_albedo(albedo.z);
        mesh.vertices.push_back(out);
        return static_cast<std::uint32_t>(mesh.vertices.size() - 1);
    }
    void triangle(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        mesh.indices.push_back(a);
        mesh.indices.push_back(b);
        mesh.indices.push_back(c);
    }
    // A flat quad a b c d (counter-clockwise seen from outside).
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, float uv_scale) {
        const Vec3 n = ambient::cross(ambient::subtract(b, a), ambient::subtract(d, a));
        const float width = ambient::length(ambient::subtract(b, a)) * uv_scale;
        const float height = ambient::length(ambient::subtract(d, a)) * uv_scale;
        const std::uint32_t i0 = vertex(a, n, 0, 0);
        const std::uint32_t i1 = vertex(b, n, width, 0);
        const std::uint32_t i2 = vertex(c, n, width, height);
        const std::uint32_t i3 = vertex(d, n, 0, height);
        triangle(i0, i1, i2);
        triangle(i0, i2, i3);
    }
    // An axis-aligned box from `low` to `high`; `bottom` false leaves it open below
    // (buried faces are never seen).
    void box(Vec3 low, Vec3 high, float uv_scale, bool bottom) {
        const Vec3 p000{low.x, low.y, low.z};
        const Vec3 p100{high.x, low.y, low.z};
        const Vec3 p010{low.x, high.y, low.z};
        const Vec3 p110{high.x, high.y, low.z};
        const Vec3 p001{low.x, low.y, high.z};
        const Vec3 p101{high.x, low.y, high.z};
        const Vec3 p011{low.x, high.y, high.z};
        const Vec3 p111{high.x, high.y, high.z};
        quad(p001, p101, p111, p011, uv_scale);  // front
        quad(p100, p000, p010, p110, uv_scale);  // back
        quad(p000, p001, p011, p010, uv_scale);  // left
        quad(p101, p100, p110, p111, uv_scale);  // right
        quad(p011, p111, p110, p010, uv_scale);  // top
        if (bottom)
            quad(p000, p100, p101, p001, uv_scale);
    }
};

// The lid's surface: a flattened half barrel over the body, hinged at the back top
// edge and swung open by `lid_open`. `outset` grows it (for the iron straps).
Vec3 lid_point(float x, float angle, float outset) {
    // angle 0 at the front edge, pi at the back (the hinge).
    const float z = std::cos(angle) * (half_d + outset);
    const float y = body_h + std::sin(angle) * (lid_rise + outset);
    // Rotate about the hinge line (y = body_h, z = -half_d), lifting the front.
    const float rz = z + half_d;
    const float ry = y - body_h;
    const float c = std::cos(lid_open);
    const float s = std::sin(lid_open);
    const Vec3 result{x, body_h + ry * c + rz * s, -half_d + rz * c - ry * s};
    return result;
}

Vec3 lid_normal(float angle) {
    const Vec3 closed{0, std::sin(angle) / lid_rise, std::cos(angle) / half_d};
    const float c = std::cos(lid_open);
    const float s = std::sin(lid_open);
    const Vec3 result = ambient::normalize({0, closed.y * c + closed.z * s, closed.z * c - closed.y * s});
    return result;
}

void lid_shell(Builder& builder, float x0, float x1, float outset, int segments, float uv_scale) {
    for (int k = 0; k < segments; ++k) {
        const float a0 = pi * static_cast<float>(k) / static_cast<float>(segments);
        const float a1 = pi * static_cast<float>(k + 1) / static_cast<float>(segments);
        const Vec3 n0 = lid_normal(a0);
        const Vec3 n1 = lid_normal(a1);
        const float v0 = a0 * (half_d + lid_rise) * 0.6F * uv_scale;
        const float v1 = a1 * (half_d + lid_rise) * 0.6F * uv_scale;
        const std::uint32_t i0 = builder.vertex(lid_point(x0, a0, outset), n0, x0 * uv_scale, v0);
        const std::uint32_t i1 = builder.vertex(lid_point(x1, a0, outset), n0, x1 * uv_scale, v0);
        const std::uint32_t i2 = builder.vertex(lid_point(x1, a1, outset), n1, x1 * uv_scale, v1);
        const std::uint32_t i3 = builder.vertex(lid_point(x0, a1, outset), n1, x0 * uv_scale, v1);
        builder.triangle(i0, i1, i2);
        builder.triangle(i0, i2, i3);
    }
}

// The lid's two end caps: half discs at x = +-half_w.
void lid_ends(Builder& builder, int segments, float uv_scale) {
    for (int side = 0; side < 2; ++side) {
        const float x = side == 0 ? -half_w : half_w;
        const Vec3 n{side == 0 ? -1.0F : 1.0F, 0, 0};
        const Vec3 hub = lid_point(x, pi * 0.5F, -lid_rise);  // the middle of the lid's lower edge
        const std::uint32_t centre = builder.vertex(hub, n, 0, 0);
        for (int k = 0; k < segments; ++k) {
            const float a0 = pi * static_cast<float>(k) / static_cast<float>(segments);
            const float a1 = pi * static_cast<float>(k + 1) / static_cast<float>(segments);
            const Vec3 p0 = lid_point(x, a0, 0);
            const Vec3 p1 = lid_point(x, a1, 0);
            const std::uint32_t i0 = builder.vertex(p0, n, p0.z * uv_scale, p0.y * uv_scale);
            const std::uint32_t i1 = builder.vertex(p1, n, p1.z * uv_scale, p1.y * uv_scale);
            if (side == 0)
                builder.triangle(centre, i1, i0);
            else
                builder.triangle(centre, i0, i1);
        }
    }
}

// The heap of coins inside: a lumpy mound over the chest's opening.
float heap_height(float x, float z) {
    const float edge_x = 1.0F - std::pow(std::abs(x) / (half_w - 0.03F), 4.0F);
    const float edge_z = 1.0F - std::pow(std::abs(z) / (half_d - 0.03F), 4.0F);
    const float dome = std::max(0.0F, edge_x) * std::max(0.0F, edge_z);
    // Coin-sized lumps over a few larger heaps.
    const float lumps = 0.010F * std::sin(x * 71.0F) * std::sin(z * 67.0F + 1.3F) +
                        0.012F * std::sin(x * 23.0F + z * 29.0F) + 0.02F * std::sin(x * 9.0F - 0.5F);
    const float result = body_h - 0.05F + 0.11F * dome + lumps * dome;
    return result;
}

void gold_heap(Builder& builder) {
    constexpr int nx = 24;
    constexpr int nz = 15;
    const float x0 = -(half_w - 0.03F);
    const float z0 = -(half_d - 0.03F);
    const float sx = 2 * (half_w - 0.03F) / nx;
    const float sz = 2 * (half_d - 0.03F) / nz;
    const std::uint32_t first = static_cast<std::uint32_t>(builder.mesh.vertices.size());
    const Vec3 bright = builder.albedo;
    for (int j = 0; j <= nz; ++j) {
        for (int i = 0; i <= nx; ++i) {
            const float x = x0 + sx * static_cast<float>(i);
            const float z = z0 + sz * static_cast<float>(j);
            const float e = 0.01F;
            const float dx = (heap_height(x + e, z) - heap_height(x - e, z)) / (2 * e);
            const float dz = (heap_height(x, z + e) - heap_height(x, z - e)) / (2 * e);
            // Coins catch the light differently: some bright, some tarnished.
            const std::uint32_t h = (static_cast<std::uint32_t>(i) * 73856093U) ^ (static_cast<std::uint32_t>(j) * 19349663U);
            const float shade = 0.55F + 0.45F * static_cast<float>((h >> 4U) % 97U) / 96.0F;
            builder.albedo = {bright.x * shade, bright.y * shade * (0.9F + 0.1F * shade), bright.z * shade};
            static_cast<void>(builder.vertex({x, heap_height(x, z), z}, {-dx * 1.6F, 1, -dz * 1.6F}, x, z));
        }
    }
    builder.albedo = bright;
    for (int j = 0; j < nz; ++j) {
        for (int i = 0; i < nx; ++i) {
            const std::uint32_t a = first + static_cast<std::uint32_t>(j * (nx + 1) + i);
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + static_cast<std::uint32_t>(nx + 1);
            const std::uint32_t d = c + 1;
            builder.triangle(a, c, d);
            builder.triangle(a, d, b);
        }
    }
}

// A coin lying at (x, z) in the chest's frame, tilted a little: a flat eight-sided disc.
void coin(Builder& builder, Vec3 centre, float radius, float tilt_x, float tilt_z) {
    const Vec3 up = ambient::normalize({tilt_x, 1, tilt_z});
    const Vec3 across = ambient::normalize(ambient::cross(up, {0, 0, 1}));
    const Vec3 along = ambient::cross(across, up);
    const std::uint32_t hub = builder.vertex(centre, up, 0, 0);
    std::uint32_t rim[8]{};
    for (int k = 0; k < 8; ++k) {
        const float a = 2 * pi * static_cast<float>(k) / 8.0F;
        const Vec3 p = ambient::add(centre, ambient::add(ambient::scale(across, std::cos(a) * radius),
                                                         ambient::scale(along, std::sin(a) * radius)));
        rim[k] = builder.vertex(p, up, std::cos(a), std::sin(a));
    }
    for (int k = 0; k < 8; ++k)
        builder.triangle(hub, rim[(k + 1) % 8], rim[k]);
}

void place(ambient::SceneData& scene, Builder& builder, std::uint32_t material, const float transform[12],
           Vec3 tint) {
    ambient::StaticPlacementRecord record{};
    record.mesh = static_cast<std::uint32_t>(scene.static_meshes.size());
    for (int k = 0; k < 12; ++k)
        record.transform[k] = transform[k];
    record.tint[0] = tint.x;
    record.tint[1] = tint.y;
    record.tint[2] = tint.z;
    record.uv_scale[0] = 1;
    record.uv_scale[1] = 1;
    record.material = material;
    scene.static_meshes.push_back(std::move(builder.mesh));
    scene.placements.push_back(record);
}

} // namespace

float ground_height(float x, float z) {
    const float center = 0.3F - 0.25F * z;
    const float half_width = std::max(0.45F, 1.45F + 0.25F * z);
    const float offset = (x - center) / half_width;
    const float channel = std::exp(-offset * offset);
    const float result = 0.12F + 0.055F * std::sin(x * 1.8F + z) + 0.045F * std::sin(z * 2.3F - x * 0.7F) +
                         0.34F * std::max(0.0F, -z / 5) +
                         0.14F * std::exp(-((x + 5) * (x + 5) / 5 + (z + 1) * (z + 1) / 4)) - 0.2F * channel;
    return result;
}

void add_treasure(ambient::SceneData& scene) {
    // The chest's frame: turned by the yaw, tipped forward and to one side as it
    // settled, sunk into the sand.
    const float roll = 0.07F;
    const float pitch = -0.05F;
    const float cy = std::cos(chest_yaw);
    const float sy = std::sin(chest_yaw);
    const float cr = std::cos(roll);
    const float sr = std::sin(roll);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    // R = Ry(yaw) * Rx(pitch) * Rz(roll)
    const float rz[9] = {cr, -sr, 0, sr, cr, 0, 0, 0, 1};
    const float rx[9] = {1, 0, 0, 0, cp, -sp, 0, sp, cp};
    const float ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
    float xz[9]{};
    float r[9]{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                xz[i * 3 + j] += rx[i * 3 + k] * rz[k * 3 + j];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                r[i * 3 + j] += ry[i * 3 + k] * xz[k * 3 + j];
    const float base = ground_height(chest_x, chest_z) - chest_sink;
    const float transform[12] = {r[0] * chest_size, r[1] * chest_size, r[2] * chest_size, chest_x,
                                 r[3] * chest_size, r[4] * chest_size, r[5] * chest_size, base,
                                 r[6] * chest_size, r[7] * chest_size, r[8] * chest_size, chest_z};
    const float uv = 1.6F;

    // Planks: old oak, darker between the boards; a little green on the lid's crown.
    Builder planks{};
    planks.albedo = {0.62F, 0.48F, 0.36F};
    planks.moss = 18;
    planks.box({-half_w, -0.2F, -half_d}, {half_w, body_h - 0.005F, half_d}, uv, false);
    planks.albedo = {0.16F, 0.11F, 0.08F};
    for (int seam = 1; seam <= 2; ++seam) {
        const float y = body_h * static_cast<float>(seam) / 3.0F;
        planks.box({-half_w - 0.004F, y - 0.007F, -half_d - 0.004F}, {half_w + 0.004F, y + 0.007F, half_d + 0.004F}, uv,
                 false);
    }
    planks.albedo = {0.62F, 0.48F, 0.36F};
    planks.moss = 70;
    lid_shell(planks, -half_w, half_w, 0, 10, uv);
    planks.moss = 18;
    lid_ends(planks, 10, uv);
    place(scene, planks, wood, transform, {1, 1, 1});

    // Iron: two straps round the body and over the lid, a rim, the lock plate, hinges.
    Builder straps{};
    straps.albedo = {0.075F, 0.058F, 0.045F};
    const float strap = 0.045F;
    for (int side = -1; side <= 1; side += 2) {
        const float x = 0.3F * static_cast<float>(side);
        straps.box({x - strap, -0.2F, -half_d - 0.012F}, {x + strap, body_h, half_d + 0.012F}, uv, false);
        lid_shell(straps, x - strap, x + strap, 0.012F, 10, uv);
    }
    straps.box({-half_w - 0.012F, body_h - 0.05F, -half_d - 0.012F}, {half_w + 0.012F, body_h, half_d + 0.012F}, uv,
             false);
    straps.box({-0.06F, body_h - 0.17F, half_d}, {0.06F, body_h - 0.03F, half_d + 0.022F}, uv, true);
    place(scene, straps, iron, transform, {1, 1, 1});

    // The gold: the heap under the lid, a few coins on the rim and spilled on the sand.
    Builder heap{};
    heap.albedo = {0.40F, 0.20F, 0.03F};
    gold_heap(heap);
    // a ruby among them
    heap.albedo = {0.55F, 0.02F, 0.03F};
    coin(heap, {0.02F, heap_height(0.02F, 0.12F) + 0.015F, 0.12F}, 0.05F, 0.2F, 0.3F);
    heap.albedo = {0.40F, 0.20F, 0.03F};
    const float spill[5][3] = {{0.22F, 0.0F, 0.52F}, {0.36F, 0.0F, 0.62F}, {0.05F, 0.0F, 0.66F},
                               {0.48F, 0.0F, 0.47F}, {-0.2F, 0.0F, 0.57F}};
    place(scene, heap, gold, transform, {1, 1, 1});
    Builder sand_gold{};
    sand_gold.albedo = {0.40F, 0.20F, 0.03F};
    for (const float* at : spill) {
        // Coins on the sand are placed in world space, each on the ground beneath it.
        const float lx = at[0];
        const float lz = at[2];
        const float wx = chest_x + r[0] * lx + r[2] * lz;
        const float wz = chest_z + r[6] * lx + r[8] * lz;
        const float wy = ground_height(wx, wz) + 0.03F;
        coin(sand_gold, {wx, wy, wz}, 0.06F, 0.25F * std::sin(lx * 9), 0.2F * std::cos(lz * 7));
    }
    const float identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    place(scene, sand_gold, gold, identity, {1, 1, 1});

    // Now and then a bubble escapes from under the lid: long cycles, so most of the
    // time there is none.
    const float escape[3][2] = {{0.12F, 0.28F}, {-0.2F, 0.3F}, {0.3F, 0.22F}};
    for (int k = 0; k < 3; ++k) {
        const float lx = escape[k][0];
        const float lz = escape[k][1];
        ambient::RiserRecord bubble{};
        bubble.base[0] = chest_x + r[0] * lx + r[1] * body_h + r[2] * lz;
        bubble.base[1] = base + r[3] * lx + r[4] * body_h + r[5] * lz;
        bubble.base[2] = chest_z + r[6] * lx + r[7] * body_h + r[8] * lz;
        bubble.size = 0.03F + 0.008F * static_cast<float>(k);
        bubble.phase = 7.0F + 9.0F * static_cast<float>(k);
        bubble.speed = 0.75F + 0.12F * static_cast<float>(k);
        bubble.height = 23.0F + 5.0F * static_cast<float>(k);
        scene.risers.push_back(bubble);
    }
}

} // namespace sw
