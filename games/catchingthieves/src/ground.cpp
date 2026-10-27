#include "ground.hpp"

#include "garden.hpp"
#include "soil.hpp"

#include <algorithm>
#include <cmath>

namespace ct {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMetresPerCell = 0.5;   // a square of the garden: a pumpkin nearly fills it
constexpr double kHole = 0.29;           // the burrow's mouth, cells (the raccoon's head is 0.26 wide)
constexpr double kPitDepth = 0.55;
constexpr int kRing = 24;                // segments round a burrow (a multiple of 8, to meet the corners)
constexpr std::uint16_t kShade = translucent | unlit | no_depth_write | double_sided;

std::uint32_t mix(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}
double unit(std::uint32_t key) { return static_cast<double>(mix(key) & 0xFFFFu) / 65535.0; }

Tex texture_of(const soil::Image& image) {
    Tex texture;
    texture.make(image.width, image.height);
    const std::size_t count = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    for (std::size_t texel = 0; texel < count; ++texel) {
        const std::uint8_t* p = &image.rgba[texel * 4];
        const std::uint32_t red = p[0];
        const std::uint32_t green = p[1];
        const std::uint32_t blue = p[2];
        const std::uint32_t alpha = p[3];
        texture.px[texel] = alpha << 24 | red << 16 | green << 8 | blue;
    }
    texture.build_mips();
    return texture;
}

int power_of_two_at_least(double value, int smallest, int largest) {
    int side = smallest;
    while (side < value && side < largest) side *= 2;
    return side;
}

// The beds in each season. Light comes from the upper left of the picture, the
// back left of the garden, as the hedges' shadows on the soil say.
soil::Parameters bed_parameters(Season season, double texels_per_metre) {
    soil::Parameters p = soil::preset(soil::Preset::tilled_loam);
    p.texels_per_metre = texels_per_metre;
    p.furrow_spacing = 0.21;      // rows dug across the plot
    p.furrow_depth = 0.035;
    p.furrow_angle = 0.0;
    p.tilth = 0.5;
    p.pebbles = 0.18;
    p.grit = 0.2;
    p.seed = 17u;
    switch (season) {
        case Season::spring:
            p.colour = soil::Colour{0.56, 0.44, 0.32};
            p.moisture = 0.5;
            p.straw = 0.12;
            break;
        case Season::summer:
            p.colour = soil::Colour{0.60, 0.49, 0.37};
            p.moisture = 0.15;
            p.moisture_patches = 0.45;
            p.cracks = 0.22;
            p.straw = 0.3;
            break;
        case Season::autumn:
            p.colour = soil::Colour{0.55, 0.42, 0.31};
            p.moisture = 0.45;
            p.leaves = 0.3;
            p.leaf_colour = soil::Colour{0.86, 0.46, 0.16};
            p.twigs = 0.25;
            break;
        case Season::winter:
        case Season::night:
            p.colour = soil::Colour{0.49, 0.40, 0.33};
            p.moisture = 0.5;
            p.frost = 0.4;
            p.snow = 0.16;
            p.furrow_depth = 0.025;  // slumped over the winter, so the snow lies in drifts, not stripes
            break;
    }
    return p;
}

soil::Parameters spoil_parameters(Season season, double texels_per_metre, std::uint32_t seed) {
    soil::Parameters p = soil::preset(soil::Preset::fresh_spoil);
    p.texels_per_metre = texels_per_metre;
    p.seed = seed;
    p.colour = soil::Colour{0.42, 0.31, 0.22};
    p.moisture = 0.65;  // fresh from below: damp and dark even in a dry summer
    p.relief = 2.0;     // loose and lumpy
    if (season == Season::winter || season == Season::night) {
        p.frost = 0.25;
        p.snow = 0.12;  // newly thrown up: less snow on it than on the beds
    }
    return p;
}

void quad(R3D& r, const Vtx corners[4], const Tex* tex, std::uint16_t material) {
    const Vtx triangles[6] = {corners[0], corners[1], corners[2], corners[0], corners[2], corners[3]};
    r.draw(triangles, 6, tex, material);
}

// A thin strand facing the camera, from a to b: a root in the pit wall.
void strand(R3D& r, V3 a, V3 b, double width, Col colour) {
    const V3 side = norm(cross(b - a, r.fwd())) * (width * 0.5);
    const V3 facing = r.fwd() * -1.0;
    const Vtx corners[4] = {{a - side, facing, 0, 0, colour}, {a + side, facing, 0, 0, colour},
                            {b + side, facing, 0, 0, colour}, {b - side, facing, 0, 0, colour}};
    quad(r, corners, nullptr, static_cast<std::uint16_t>(unlit | double_sided));
}

// The lip of spoil heaped round the mouth, in burrow space (z up, the mouth at the
// origin): a lumpy ring from the edge of the hole out over the bed, trodden low
// toward `toward` (radians), where the raccoons come and go. Rows of `kRing` quads.
Mesh rim_mesh(double toward, std::uint32_t key) {
    const int across = 4;
    const double inner = 0.27;   // tucked just inside the hole's edge, so no gap shows
    const double outer = 0.43;
    const double height = 0.06;
    Mesh mesh;
    // Heights on a (kRing + 1) x (across + 1) grid, then normals from the grid.
    V3 grid[kRing + 1][5];
    for (int k = 0; k <= kRing; ++k) {
        const double angle = k * 2.0 * kPi / kRing;
        const double trodden = std::pow(std::max(0.0, std::cos(angle - toward)), 2.0);
        const double lump = 0.75 + 0.5 * unit(key + static_cast<std::uint32_t>(k % kRing) * 13u);
        const double swell = 0.92 + 0.16 * unit(key + 500u + static_cast<std::uint32_t>(k % kRing) * 7u);
        for (int j = 0; j <= across; ++j) {
            const double t = static_cast<double>(j) / across;
            const double radius = inner + (outer - inner) * t * swell;
            const double profile = std::pow(std::sin(kPi * std::pow(t, 0.7)), 0.8);
            const double z = height * profile * lump * (1.0 - 0.6 * trodden);
            grid[k][j] = V3{std::cos(angle) * radius, std::sin(angle) * radius, j == 0 ? -0.01 : z};
        }
    }
    for (int k = 0; k < kRing; ++k) {
        for (int j = 0; j < across; ++j) {
            const V3 a = grid[k][j];
            const V3 b = grid[k][j + 1];
            const V3 c = grid[k + 1][j + 1];
            const V3 d = grid[k + 1][j];
            V3 normal = norm(cross(b - a, d - a));
            if (normal.z < 0) normal = normal * -1.0;
            const double s0 = k * 4.0 / kRing;
            const double s1 = (k + 1) * 4.0 / kRing;
            const double t0 = j * 0.5 / across;
            const double t1 = (j + 1) * 0.5 / across;
            const Col white{1, 1, 1, 1};
            mesh.push_back(Vtx{a, normal, s0, t0, white});
            mesh.push_back(Vtx{b, normal, s0, t1, white});
            mesh.push_back(Vtx{c, normal, s1, t1, white});
            mesh.push_back(Vtx{a, normal, s0, t0, white});
            mesh.push_back(Vtx{c, normal, s1, t1, white});
            mesh.push_back(Vtx{d, normal, s1, t0, white});
        }
    }
    return mesh;
}

Col scaled(Col colour, float k) {
    const Col result{colour.r * k, colour.g * k, colour.b * k, colour.a};
    return result;
}
Col times(Col a, Col b) {
    const Col result{a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a};
    return result;
}

}  // namespace

V3 Ground::centre(const Level& level, int cell) const {
    // As Garden::cell_pos: the level's middle at the origin, rows running toward -y.
    const int x = cell % std::max(1, level.w);
    const int y = cell / std::max(1, level.w);
    const V3 result{x - (level.w - 1) / 2.0, (level.h - 1) / 2.0 - y, 0};
    return result;
}

void Ground::prepare(Season season, double pixels_per_cell) {
    // Texels per cell follow the screen, in steps of about an eighth, so a resize
    // does not remake the pictures for every pixel.
    const double wanted = std::clamp(pixels_per_cell, 8.0, 96.0);
    const int density = static_cast<int>(std::lround(std::pow(2.0, std::round(std::log2(wanted) * 6.0) / 6.0)));
    const int season_index = static_cast<int>(season);
    if (ready_ && season_index == season_ && density == density_) return;
    season_ = season_index;
    density_ = density;
    const double texels_per_metre = density / kMetresPerCell;

    // The beds: one picture five to ten squares across, repeating.
    const int side = power_of_two_at_least(density * 5.0, 64, 512);
    repeat_ = static_cast<double>(side) / density;
    soil::Image image;
    if (soil::generate_surface(bed_parameters(season, texels_per_metre), side, side, image)) bed_ = texture_of(image);

    // The pit wall: the same fresh spoil, a small repeating picture.
    soil::Parameters wall = spoil_parameters(season, texels_per_metre * 2.0, 5u);
    wall.frost = 0.0;
    wall.snow = 0.0;
    if (soil::generate_surface(wall, 32, 32, image)) wall_ = texture_of(image);

    // A fan for each way the spoil can be thrown, each two squares across.
    const int fan_side = power_of_two_at_least(density * 2.0, 32, 256);
    const double fan_texels_per_metre = fan_side / (2.0 * kMetresPerCell);
    for (int direction = 0; direction < 4; ++direction) {
        soil::Spoil spoil;
        spoil.hole_radius = kHole * kMetresPerCell;
        spoil.rim_width = 0.085;
        spoil.rim_height = 0.075;
        spoil.fan_direction = std::atan2(static_cast<double>(kDY[direction]), static_cast<double>(kDX[direction]));
        spoil.fan_spread = 0.95;
        spoil.fan_reach = 0.24;
        spoil.fan_height = 0.05;
        spoil.threshold = 0.6;
        const soil::Parameters p = spoil_parameters(season, fan_texels_per_metre, 40u + static_cast<std::uint32_t>(direction));
        if (soil::generate_spoil(p, spoil, fan_side, fan_side, image)) {
            fans_[direction].picture = texture_of(image);
            fans_[direction].side = 2.0;
        }
    }

    for (int direction = 0; direction < 4; ++direction) {
        const double toward = std::atan2(-static_cast<double>(kDY[direction]), static_cast<double>(kDX[direction]));
        rims_[direction] = rim_mesh(toward, 300u + static_cast<std::uint32_t>(direction) * 101u);
    }
    if (clods_.empty()) {
        for (int k = 0; k < 4; ++k) clods_.push_back(rock_mesh(91u + static_cast<std::uint64_t>(k), 7, 5, 0.28));
    }
    tint_ = season == Season::night ? Col{0.5f, 0.56f, 0.8f, 1} : Col{1, 1, 1, 1};
    spoil_colour_ = season == Season::winter || season == Season::night ? hex(0x6A5446) : hex(0x5A4030);
    ready_ = true;
}

void Ground::draw_beds(R3D& r, const Level& level, std::uint32_t key) const {
    if (!ready_) return;
    // Each garden shows its own patch of the soil.
    const double shift_s = unit(key * 2u + 1u) * repeat_;
    const double shift_t = unit(key * 2u + 2u) * repeat_;
    const double to_texture = 1.0 / repeat_;
    const V3 up{0, 0, 1};
    for (int cell = 0; cell < level.w * level.h; ++cell) {
        if (!level.floor(cell)) continue;
        const V3 c = centre(level, cell);
        // Alternate squares a shade apart, as before, so the grid can still be counted.
        const bool odd = ((cell % level.w) + (cell / level.w)) % 2 != 0;
        const Col shade = odd ? scaled(tint_, 0.93f) : tint_;
        if (!level.goal[static_cast<std::size_t>(cell)]) {
            Vtx corners[4];
            const double dx[4] = {-0.5, 0.5, 0.5, -0.5};
            const double dy[4] = {-0.5, -0.5, 0.5, 0.5};
            for (int k = 0; k < 4; ++k) {
                const V3 p = c + V3{dx[k], dy[k], 0};
                corners[k] = Vtx{p, up, (p.x + shift_s) * to_texture, (shift_t - p.y) * to_texture, shade};
            }
            quad(r, corners, &bed_, unlit);
            continue;
        }
        // A burrow's square: the bed with the mouth of the pit cut out of it.
        for (int k = 0; k < kRing; ++k) {
            Vtx corners[4];
            for (int e = 0; e < 2; ++e) {
                const double angle = (k + e) * 2.0 * kPi / kRing;
                const double ca = std::cos(angle);
                const double sa = std::sin(angle);
                const double edge = 0.5 / std::max(std::fabs(ca), std::fabs(sa));
                const V3 inner = c + V3{ca * kHole, sa * kHole, 0};
                const V3 outer = c + V3{ca * edge, sa * edge, 0};
                corners[e == 0 ? 0 : 3] = Vtx{inner, up, (inner.x + shift_s) * to_texture, (shift_t - inner.y) * to_texture, shade};
                corners[e == 0 ? 1 : 2] = Vtx{outer, up, (outer.x + shift_s) * to_texture, (shift_t - outer.y) * to_texture, shade};
            }
            quad(r, corners, &bed_, static_cast<std::uint16_t>(unlit | double_sided));
        }
    }
}

int Ground::fan_direction(const Level& level, int cell) const {
    // Thrown onto open soil, never into a hedge, and away from other burrows if it can be.
    int choices[4] = {0, 0, 0, 0};
    int count = 0;
    for (int pass = 0; pass < 2 && count == 0; ++pass) {
        for (int direction = 0; direction < 4; ++direction) {
            const int next = level.step(cell, direction);
            if (!level.floor(next)) continue;
            if (pass == 0 && level.goal[static_cast<std::size_t>(next)]) continue;
            choices[count] = direction;
            ++count;
        }
    }
    if (count == 0) return kDown;
    const int pick = static_cast<int>(mix(static_cast<std::uint32_t>(cell) * 2654435761u) % static_cast<std::uint32_t>(count));
    return choices[pick];
}

void Ground::draw_spoil(R3D& r, const Level& level) const {
    if (!ready_) return;
    const V3 up{0, 0, 1};
    for (int cell = 0; cell < level.w * level.h; ++cell) {
        if (!level.goal[static_cast<std::size_t>(cell)] || !level.floor(cell)) continue;
        const Fan& fan = fans_[fan_direction(level, cell)];
        const V3 c = centre(level, cell) + V3{0, 0, 0.004};
        // Picture rows run toward the camera (-y), as on the beds. In pieces half a
        // square across: depth and texture are interpolated without perspective.
        const int pieces = 4;
        const double step = fan.side / pieces;
        for (int row = 0; row < pieces; ++row) {
            for (int column = 0; column < pieces; ++column) {
                const double x0 = -fan.side * 0.5 + column * step;
                const double y0 = fan.side * 0.5 - row * step;
                const double s0 = static_cast<double>(column) / pieces;
                const double t0 = static_cast<double>(row) / pieces;
                const double s1 = static_cast<double>(column + 1) / pieces;
                const double t1 = static_cast<double>(row + 1) / pieces;
                const Vtx corners[4] = {{c + V3{x0, y0, 0}, up, s0, t0, tint_}, {c + V3{x0 + step, y0, 0}, up, s1, t0, tint_},
                                        {c + V3{x0 + step, y0 - step, 0}, up, s1, t1, tint_}, {c + V3{x0, y0 - step, 0}, up, s0, t1, tint_}};
                quad(r, corners, &fan.picture, kShade);
            }
        }
    }
}

void Ground::draw_pit(R3D& r, const Level& level, int cell) const {
    if (!ready_) return;
    const V3 c = centre(level, cell);
    const std::uint32_t key = static_cast<std::uint32_t>(cell) * 7919u;
    // The wall narrows a little as it goes down, and darkens to nothing.
    const double depths[4] = {0.0, -0.1, -0.26, -kPitDepth};
    const double radii[4] = {kHole, kHole * 0.95, kHole * 0.86, kHole * 0.72};
    const float light[4] = {0.95f, 0.55f, 0.24f, 0.05f};
    const Col wall = times(spoil_colour_, tint_);
    const Col wall_tint = times(Col{1.6f, 1.6f, 1.6f, 1}, tint_);
    for (int band = 0; band < 3; ++band) {
        for (int k = 0; k < kRing; ++k) {
            Vtx corners[4];
            for (int e = 0; e < 2; ++e) {
                const double angle = (k + e) * 2.0 * kPi / kRing;
                const double ca = std::cos(angle);
                const double sa = std::sin(angle);
                const V3 inward{-ca, -sa, 0};
                const double s = (k + e) * 3.0 / kRing;
                const V3 top = c + V3{ca * radii[band], sa * radii[band], depths[band]};
                const V3 bottom = c + V3{ca * radii[band + 1], sa * radii[band + 1], depths[band + 1]};
                corners[e == 0 ? 0 : 1] = Vtx{top, inward, s, -depths[band] * 3.0, scaled(wall_tint, light[band])};
                corners[e == 0 ? 3 : 2] = Vtx{bottom, inward, s, -depths[band + 1] * 3.0, scaled(wall_tint, light[band + 1])};
            }
            quad(r, corners, &wall_, static_cast<std::uint16_t>(unlit | double_sided));
        }
    }
    draw_mesh(r, disc_mesh(kRing), M34::translate(c.x, c.y, -kPitDepth) * M34::scale(radii[3], radii[3], 1), nullptr,
              scaled(wall, 0.06f), unlit);
    // Roots showing in the far wall, pale where the digging cut them.
    const Col root = times(hex(0xC8A27A), tint_);
    const int roots = 1 + static_cast<int>(unit(key + 3u) * 2.5);
    for (int k = 0; k < roots; ++k) {
        const double angle = kPi * (0.3 + 0.4 * unit(key + 10u + static_cast<std::uint32_t>(k)));
        const double length = 0.08 + 0.1 * unit(key + 20u + static_cast<std::uint32_t>(k));
        const double ca = std::cos(angle);
        const double sa = std::sin(angle);
        const V3 a = c + V3{ca * kHole * 1.02, sa * kHole * 1.02, 0.005};
        const V3 b = c + V3{ca * (kHole * 0.9), sa * (kHole * 0.9), -length * 0.6};
        const V3 tip = c + V3{ca * (kHole * 0.86) + 0.03 * (unit(key + 30u + static_cast<std::uint32_t>(k)) - 0.5), sa * (kHole * 0.86), -length};
        strand(r, a, b, 0.022, root);
        strand(r, b, tip, 0.014, scaled(root, 0.7f));
    }
    // The heaped lip, its crest dried paler than the bed so the burrow reads at any size,
    // then a few clods and a stone on the heap.
    const M34 place = M34::translate(c.x, c.y, 0.0);
    draw_mesh(r, rims_[fan_direction(level, cell)], place, &wall_, times(Col{1.75f, 1.65f, 1.55f, 1}, tint_),
              static_cast<std::uint16_t>(toon | double_sided));
    const Col clod = times(spoil_colour_, tint_);
    for (int k = 0; k < 3; ++k) {
        const std::uint32_t here = key + 40u + static_cast<std::uint32_t>(k) * 5u;
        const double angle = 2.0 * kPi * unit(here);
        const double distance = kHole + 0.1 + 0.1 * unit(here + 1u);
        const double size = 0.045 + 0.03 * unit(here + 2u);
        const V3 at = c + V3{std::cos(angle) * distance, std::sin(angle) * distance, size * 0.35};
        const M34 model = M34::translate(at.x, at.y, at.z) * M34::rot_z(angle * 3.0) * M34::scale(size * 1.2, size, size * 0.7);
        const Col shade = k == 2 ? times(hex(0xA8A49C), tint_) : scaled(clod, 0.9f + 0.25f * static_cast<float>(unit(here + 3u)));
        draw_mesh(r, clods_[static_cast<std::size_t>(k + (cell & 1))], model, nullptr, shade, toon);
    }
}

void Ground::shade_pit(R3D& r, const Level& level, int cell) const {
    if (!ready_) return;
    // Layers of shadow down the pit: whatever is deeper passes under more of them.
    const V3 c = centre(level, cell);
    const double depths[4] = {-0.03, -0.1, -0.2, -0.32};
    const float strength[4] = {0.22f, 0.3f, 0.35f, 0.45f};
    for (int k = 0; k < 4; ++k) {
        const double t = -depths[k] / kPitDepth;
        const double radius = kHole * (1.0 - 0.28 * t);
        draw_mesh(r, disc_mesh(kRing), M34::translate(c.x, c.y, depths[k]) * M34::scale(radius, radius, 1), nullptr,
                  Col{0.02f, 0.015f, 0.01f, strength[k]}, kShade);
    }
}

}  // namespace ct
