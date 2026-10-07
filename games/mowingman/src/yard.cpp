#include "yard.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mm {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double light_angle = -2.356;  // the sun is up and to the left
constexpr double lay_angle = -2.443;    // the way short grass lies when laid towards the grass art's sun

struct Rgba {
    float r{};
    float g{};
    float b{};
    float a{};
};

// Premultiplied 0xAARRGGBB to channels in 0..1.
Rgba unpack(std::uint32_t p) {
    const Rgba result{static_cast<float>((p >> 16U) & 255U) / 255.0F, static_cast<float>((p >> 8U) & 255U) / 255.0F,
                      static_cast<float>(p & 255U) / 255.0F, static_cast<float>(p >> 24U) / 255.0F};
    return result;
}

// Premultiplied source over destination, scaled by an extra coverage.
Rgba over(Rgba top, Rgba under, float coverage) {
    const float a = top.a * coverage;
    const float keep = 1.0F - a;
    const Rgba result{top.r * coverage + under.r * keep, top.g * coverage + under.g * keep,
                      top.b * coverage + under.b * keep, a + under.a * keep};
    return result;
}

Rgba scaled(Rgba c, float k) {
    const Rgba result{c.r * k, c.g * k, c.b * k, c.a};
    return result;
}

Rgba blend(Rgba a, Rgba b, float t) {
    const Rgba result{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    return result;
}

// Fraction cut at a lawn point, bilinear over the fine cells (1 cut, 0 tall).
float cut_field(const std::vector<std::uint8_t>& cut, double x, double y) {
    const double gx = x / lawn_cell - 0.5;
    const double gy = y / lawn_cell - 0.5;
    const double fx = std::floor(gx);
    const double fy = std::floor(gy);
    const int x0 = std::clamp(static_cast<int>(fx), 0, lawn_cells_x - 1);
    const int y0 = std::clamp(static_cast<int>(fy), 0, lawn_cells_y - 1);
    const int x1 = std::min(x0 + 1, lawn_cells_x - 1);
    const int y1 = std::min(y0 + 1, lawn_cells_y - 1);
    const float tx = static_cast<float>(std::clamp(gx - fx, 0.0, 1.0));
    const float ty = static_cast<float>(std::clamp(gy - fy, 0.0, 1.0));
    const std::size_t w = static_cast<std::size_t>(lawn_cells_x);
    const float a = cut[static_cast<std::size_t>(y0) * w + static_cast<std::size_t>(x0)];
    const float b = cut[static_cast<std::size_t>(y0) * w + static_cast<std::size_t>(x1)];
    const float c = cut[static_cast<std::size_t>(y1) * w + static_cast<std::size_t>(x0)];
    const float d = cut[static_cast<std::size_t>(y1) * w + static_cast<std::size_t>(x1)];
    const float result = (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
    return result;
}

float density(const std::vector<std::uint8_t>& field, double x, double y) {
    const int cx = std::clamp(static_cast<int>(x / lawn_cell), 0, lawn_cells_x - 1);
    const int cy = std::clamp(static_cast<int>(y / lawn_cell), 0, lawn_cells_y - 1);
    const float result =
        static_cast<float>(field[static_cast<std::size_t>(cy) * static_cast<std::size_t>(lawn_cells_x) + static_cast<std::size_t>(cx)]) / 255.0F;
    return result;
}

float smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

void copy_pixels(const Canvas& from, Canvas& to) {
    to.px = from.px;
}

} // namespace

void Yard::build(int width, int height, const GrassArt& art, const Mowing& mowing) {
    width_ = std::max(16, width);
    height_ = std::max(16, height);
    // The lawn fills the view, seen a little from the front: the walls stand up behind
    // and beside it under a band of sky, and a little terrace shows in front.
    const double sky = 1.4;  // metres of sky above the back wall
    const double depth = lawn_height * frame_.tilt + (wall_height + sky) * frame_.rise;
    const double ppm = std::min(width_ / lawn_width, height_ / depth);
    frame_.ppm = ppm;
    frame_.ox = (width_ - lawn_width * ppm) * 0.5;
    // The lawn's near edge is the window's bottom edge; any spare height goes to the sky.
    frame_.oy = height_ - lawn_height * frame_.tilt * ppm;
    surround_.resize(width_, height_);
    draw_surround(surround_, frame_);
    beds_.resize(width_, height_);
    beds_.clear(rgb(0, 0, 0, 0));
    for (const Bed& bed : mowing.garden().beds)
        draw_bed(beds_, frame_, bed);
    base_.resize(width_, height_);
    copy_pixels(surround_, base_);
    picture_.resize(width_, height_);
    crowns_.clear();
    for (std::size_t index = 0; index < art.crowns.size() && index < mowing.garden().trees.size(); ++index) {
        const Cutout& cut = art.crowns[index];
        const Tree& tree = mowing.garden().trees[index];
        // The canopy sits up on its trunk; it is drawn already seen from the garden's angle.
        const double grow = frame_.ppm / cut.ppm;
        const double grow_y = grow;
        Crown crown{};
        crown.width = std::max(1, static_cast<int>(std::ceil(cut.image.width * grow)));
        crown.height = std::max(1, static_cast<int>(std::ceil(cut.image.height * grow_y)));
        crown.x = static_cast<int>(std::lround(frame_.px(tree.x) - cut.root_x * grow));
        crown.y = static_cast<int>(std::lround(frame_.py(tree.y) - cut.root_y * grow_y - frame_.up(trunk_height)));
        crown.px.resize(static_cast<std::size_t>(crown.width) * static_cast<std::size_t>(crown.height));
        for (int py = 0; py < crown.height; ++py) {
            for (int px = 0; px < crown.width; ++px)
                crown.px[static_cast<std::size_t>(py) * static_cast<std::size_t>(crown.width) + static_cast<std::size_t>(px)] =
                    sample_clamped(cut.image, (px + 0.5) / grow - 0.5, (py + 0.5) / grow_y - 0.5);
        }
        crowns_.push_back(std::move(crown));
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(frame_.px(0))));
    const int y0 = std::max(0, static_cast<int>(std::floor(frame_.py(0))));
    const int x1 = std::min(width_ - 1, static_cast<int>(std::ceil(frame_.px(lawn_width))));
    const int y1 = std::min(height_ - 1, static_cast<int>(std::ceil(frame_.py(lawn_height))));
    shade(art, mowing, x0, y0, x1, y1);
    wall_shadows(x0, y0, x1, y1);
}

void Yard::refresh(const GrassArt& art, const Mowing& mowing, const Dirty& dirty) {
    if (!built() || dirty.empty())
        return;
    const int x0 = std::max(0, static_cast<int>(std::floor(frame_.px(std::max(0.0, dirty.x0)))));
    const int y0 = std::max(0, static_cast<int>(std::floor(frame_.py(std::max(0.0, dirty.y0)))));
    const int x1 = std::min(width_ - 1, static_cast<int>(std::ceil(frame_.px(std::min(lawn_width, dirty.x1)))));
    const int y1 = std::min(height_ - 1, static_cast<int>(std::ceil(frame_.py(std::min(lawn_height, dirty.y1)))));
    if (x1 < x0 || y1 < y0)
        return;
    shade(art, mowing, x0, y0, x1, y1);
    wall_shadows(x0, y0, x1, y1);
}

// The walls' shadows on the grass along the back and the left, over a rectangle just shaded.
void Yard::wall_shadows(int x0, int y0, int x1, int y1) {
    const int lawn_y0 = std::max(0, static_cast<int>(std::floor(frame_.py(0))));
    const double fall = wall_height * 0.3 * frame_.ppm * frame_.tilt;
    for (int yy = y0; yy <= y1; ++yy) {
        std::uint8_t* row = base_.px.data() + static_cast<std::size_t>(yy) * static_cast<std::size_t>(width_) * 4U;
        for (int xx = x0; xx <= x1; ++xx) {
            const double wx = (xx + 0.5 - frame_.ox) / frame_.ppm;
            const double wy = (yy + 0.5 - frame_.oy) / (frame_.ppm * frame_.tilt);
            if (wx < 0 || wy < 0 || wx >= lawn_width || wy >= lawn_height)
                continue;
            float dark = 1.0F;
            if (yy - lawn_y0 < fall)
                dark -= 0.35F * static_cast<float>(1 - (yy - lawn_y0) / fall);

            if (dark >= 0.999F)
                continue;
            for (int c = 0; c < 3; ++c)
                row[xx * 4 + c] = static_cast<std::uint8_t>(row[xx * 4 + c] * std::max(0.3F, dark));
        }
    }
}

// The lawn, pixel by pixel: long grass (its flowers grown into it) where uncut, short
// grass laid one way or the other where mown, the long grass's shadow at every cut
// edge, and mulch inside the beds. The beds' stones and plants are laid over the result.
void Yard::shade(const GrassArt& art, const Mowing& mowing, int x0, int y0, int x1, int y1) {
    const std::vector<std::uint8_t>& cut = mowing.mower().cut();
    const std::vector<std::uint8_t>& stripes = mowing.stripes();
    const std::vector<std::uint8_t>& mulch = mowing.garden().mulch;
    const std::vector<std::uint8_t>& trampled = mowing.trampled();
    const double inverse = 1.0 / frame_.ppm;
    const double inverse_y = 1.0 / (frame_.ppm * frame_.tilt);
    const bool have_art = !art.empty();
    // Shadows fall away from the light: look toward it for the grass casting them.
    const double toward_x = std::cos(light_angle) * 0.07;
    const double toward_y = std::sin(light_angle) * 0.07;
    for (int y = y0; y <= y1; ++y) {
        std::uint8_t* row = base_.px.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) * 4U;
        const std::uint8_t* surround = surround_.px.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) * 4U;
        const std::uint8_t* bed = beds_.px.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) * 4U;
        for (int x = x0; x <= x1; ++x) {
            const double wx = (x + 0.5 - frame_.ox) * inverse;
            const double wy = (y + 0.5 - frame_.oy) * inverse_y;
            std::uint8_t* out = row + static_cast<std::size_t>(x) * 4U;
            if (wx < 0 || wy < 0 || wx >= lawn_width || wy >= lawn_height) {
                std::memcpy(out, surround + static_cast<std::size_t>(x) * 4U, 4);
                continue;
            }
            Rgba color{};
            const float c = cut_field(cut, wx, wy);
            const float mown = smoothstep(0.35F, 0.65F, c);
            if (!have_art) {
                // Before the grass art is ready: a plain lawn, so the first frame is the garden.
                color = blend(Rgba{0.10F, 0.22F, 0.06F, 1}, Rgba{0.22F, 0.40F, 0.12F, 1}, mown);
            } else {
                const double ax = wx * art.ppm - 0.5;
                const double ay = wy * art.ppm - 0.5;
                if (density(mulch, wx, wy) > 0) {
                    // Where the deck has been over a bed, only churned mulch is left.
                    const float churned = smoothstep(0.3F, 0.7F, cut_field(trampled, wx, wy));
                    color = unpack(sample_clamped(art.beds, ax, ay));
                    if (churned > 0.001F)
                        color = blend(color, scaled(unpack(sample_clamped(art.mulch, ax, ay)), 0.82F), churned);
                } else {
                    Rgba tall{};
                    if (mown < 0.999F)
                        tall = unpack(sample_clamped(art.tall, ax, ay));
                    Rgba turf{};
                    if (mown > 0.001F) {
                        // Short grass lies the way the mower went. The art has it laid four ways, a
                        // quarter turn apart from towards the sun; each cell blends the two either side
                        // of its own mower heading, so every curve of a pass shades as it turns.
                        const int cx = std::clamp(static_cast<int>(wx / lawn_cell), 0, lawn_cells_x - 1);
                        const int cy = std::clamp(static_cast<int>(wy / lawn_cell), 0, lawn_cells_y - 1);
                        const std::uint8_t code =
                            stripes[static_cast<std::size_t>(cy) * static_cast<std::size_t>(lawn_cells_x) + static_cast<std::size_t>(cx)];
                        if (code != 0 && code != 255) {
                            const double heading = (code - 1) / 255.0 * 2 * pi;
                            const Layer* lays[4] = {&art.mown_dark, &art.mown_quarter, &art.mown_light, &art.mown_three_quarter};
                            double turn = (heading - lay_angle) / (pi / 2);
                            turn -= 4 * std::floor(turn / 4);
                            const int first = static_cast<int>(turn) % 4;
                            const float share = smoothstep(0.12F, 0.88F, static_cast<float>(turn - std::floor(turn)));
                            const Rgba a = unpack(sample_clamped(*lays[first], ax, ay));
                            turf = share < 0.001F ? a : blend(a, unpack(sample_clamped(*lays[(first + 1) % 4], ax, ay)), share);
                        } else {
                            turf = blend(unpack(sample_clamped(art.mown_light, ax, ay)), unpack(sample_clamped(art.mown_dark, ax, ay)), 0.5F);
                        }
                        // The cut edge: the long grass still standing towards the sun shades the turf.
                        const float upwind = 1.0F - cut_field(cut, wx + toward_x, wy + toward_y);
                        if (upwind > 0.01F)
                            turf = scaled(turf, 1.0F - 0.45F * upwind);
                    }
                    color = blend(tall, turf, mown);
                }
            }
            const Rgba overlay = unpack(static_cast<std::uint32_t>(bed[x * 4]) | (static_cast<std::uint32_t>(bed[x * 4 + 1]) << 8U) |
                                        (static_cast<std::uint32_t>(bed[x * 4 + 2]) << 16U) |
                                        (static_cast<std::uint32_t>(bed[x * 4 + 3]) << 24U));
            color = over(overlay, color, 1.0F);
            out[0] = static_cast<std::uint8_t>(std::clamp(color.b, 0.0F, 1.0F) * 255.0F + 0.5F);
            out[1] = static_cast<std::uint8_t>(std::clamp(color.g, 0.0F, 1.0F) * 255.0F + 0.5F);
            out[2] = static_cast<std::uint8_t>(std::clamp(color.r, 0.0F, 1.0F) * 255.0F + 0.5F);
            out[3] = 255;
        }
    }
}

namespace {

// Everything that stands on the lawn, drawn back to front so the nearer hides the further.
struct Standing {
    double key{};
    int kind{};  // 0 prop, 1 tree, 2 mower, 3 gnome, 4 guard, 5 granny, 6 perched bird
    std::size_t index{};
};

bool nearer(const Standing& a, const Standing& b) {
    return a.key < b.key;
}

} // namespace

const std::vector<std::uint8_t>& Yard::compose(const Mowing& mowing, const MowerPose& pose, double time) {
    copy_pixels(base_, picture_);
    const Garden& garden = mowing.garden();
    std::vector<Standing> standing{};
    for (std::size_t index = 0; index < garden.props.size(); ++index)
        standing.push_back(Standing{garden.props[index].y + garden.props[index].ry * 0.5, 0, index});
    for (std::size_t index = 0; index < garden.trees.size(); ++index)
        standing.push_back(Standing{garden.trees[index].y + garden.trees[index].crown * 0.4, 1, index});
    standing.push_back(Standing{pose.y + 0.3, 2, 0});
    if (mowing.gnome().state != GnomeState::hidden)
        standing.push_back(Standing{mowing.gnome().y, 3, 0});
    if (mowing.guard().state != GnomeState::hidden)
        standing.push_back(Standing{mowing.guard().y, 4, 0});
    if (mowing.granny().state != GrannyState::away)
        standing.push_back(Standing{mowing.granny().y, 5, 0});
    for (std::size_t index = 0; index < mowing.birds().size(); ++index) {
        const Bird& bird = mowing.birds()[index];
        if (bird.state == BirdState::perched)
            standing.push_back(Standing{bird.y + (bird.z > 1.5 ? 0.6 : 0.0), 6, index});
    }
    std::sort(standing.begin(), standing.end(), nearer);
    for (const Standing& thing : standing) {
        switch (thing.kind) {
        case 0:
            draw_prop(picture_, frame_, garden.props[thing.index]);
            draw_prop_live(picture_, frame_, garden.props[thing.index], time);
            break;
        case 1: {
            draw_tree_foot(picture_, frame_, garden.trees[thing.index]);
            if (thing.index >= crowns_.size())
                break;
            // The canopy stirs a little in the air; struck, it shudders.
            const Crown& crown = crowns_[thing.index];
            const double shake = mowing.tree_shake(thing.index);
            const double shudder = shake * shake * 0.09 * std::sin(time * 38 + static_cast<double>(thing.index));
            const int ox = crown.x + static_cast<int>(std::lround(frame_.ppm * (0.012 * std::sin(time * 0.9 + static_cast<double>(thing.index) * 1.7) + shudder)));
            const int oy = crown.y + static_cast<int>(std::lround(frame_.ppm * (0.008 * std::sin(time * 0.7 + static_cast<double>(thing.index) * 2.9) + shudder * 0.6)));
            const int x0 = std::max(0, -ox);
            const int x1 = std::min(crown.width, width_ - ox);
            for (int py = std::max(0, -oy); py < std::min(crown.height, height_ - oy); ++py) {
                const std::uint32_t* from = crown.px.data() + static_cast<std::size_t>(py) * static_cast<std::size_t>(crown.width);
                std::uint8_t* to = picture_.px.data() + (static_cast<std::size_t>(oy + py) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(ox + x0)) * 4U;
                for (int px = x0; px < x1; ++px, to += 4) {
                    const std::uint32_t source = from[px];
                    const std::uint32_t keep = 255U - (source >> 24U);
                    if (keep == 255U)
                        continue;
                    to[0] = static_cast<std::uint8_t>((source & 255U) + (to[0] * keep + 127U) / 255U);
                    to[1] = static_cast<std::uint8_t>(((source >> 8U) & 255U) + (to[1] * keep + 127U) / 255U);
                    to[2] = static_cast<std::uint8_t>(((source >> 16U) & 255U) + (to[2] * keep + 127U) / 255U);
                }
            }
            break;
        }
        case 2:
            draw_mower(picture_, frame_, pose, mowing.livery(), time);
            break;
        case 3:
            draw_gnome(picture_, frame_, mowing.gnome(), time);
            break;
        case 4:
            draw_gnome(picture_, frame_, mowing.guard(), time);
            break;
        case 5:
            draw_granny(picture_, frame_, mowing.granny());
            break;
        default:
            draw_bird(picture_, frame_, mowing.birds()[thing.index]);
            break;
        }
    }
    draw_particles(picture_, frame_, mowing.particles());
    for (const Bird& bird : mowing.birds()) {
        if (bird.state != BirdState::perched)
            draw_bird(picture_, frame_, bird);
    }
    for (const Bee& bee : mowing.bees())
        draw_bee(picture_, frame_, bee, time);
    draw_chase_timer(picture_, frame_, mowing.granny(), time);
    return picture_.px;
}

} // namespace mm
