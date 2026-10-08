#include "art.hpp"
#include "models.hpp"
#include "model3d.hpp"
#include "stones.hpp"

#include <algorithm>
#include <cmath>

namespace mm {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double shadow_x = 0.22;  // metres a standing thing's shadow falls, per metre of height
constexpr double shadow_y = 0.30;

// Local drawing in metres about a ground point, rotated to a heading, at a height.
// The ground's tilt is applied in screen space, so a circle drawn here is the
// squashed ellipse a circle on the ground looks like.
void enter(Canvas& canvas, const Frame& frame, double x, double y, double heading, double height = 0) {
    canvas.save();
    canvas.translate(frame.px(x), frame.py(y) - frame.up(height));
    canvas.scale(1, frame.tilt);
    canvas.rotate(heading);
    canvas.scale(frame.ppm, frame.ppm);
}

void rounded(Canvas& canvas, double x, double y, double w, double h, double r, Col color) {
    canvas.begin();
    canvas.rrect(x, y, w, h, r);
    canvas.fill(color);
}

// How many layers a stack from h0 to h1 needs so its sides are solid.
int layers_for(const Frame& frame, double h0, double h1) {
    return std::max(2, static_cast<int>(std::ceil(frame.up(h1 - h0) / 1.2)) + 1);
}

// The side of a thing: its ground shape repeated from the bottom up, darker low down and
// on the shaded side. `fade` darkens the lowest layer; the top layer is left to the caller.
Col side_tone(Col color, double t, double fade) {
    return shade(color, static_cast<float>(1 - fade * (1 - t)));
}

// An ellipse standing from h0 to h1, its size scaled from `low` at the bottom to 1 at the top.
void stack_ellipse(Canvas& canvas, const Frame& frame, double x, double y, double heading, double lx, double ly, double rx, double ry, double h0,
                   double h1, double low, Col side, const Paint& top) {
    const int layers = layers_for(frame, h0, h1);
    for (int k = 0; k < layers; ++k) {
        const double t = static_cast<double>(k) / (layers - 1);
        const double s = low + (1 - low) * t;
        enter(canvas, frame, x, y, heading, h0 + (h1 - h0) * t);
        canvas.fill_ellipse(lx, ly, rx * s, ry * s, side_tone(side, t, 0.5));
        canvas.restore();
    }
    enter(canvas, frame, x, y, heading, h1);
    canvas.begin();
    canvas.ellipse(lx, ly, rx, ry);
    canvas.fill(top);
    canvas.restore();
}

void stack_ellipse(Canvas& canvas, const Frame& frame, double x, double y, double heading, double lx, double ly, double rx, double ry, double h0,
                   double h1, double low, Col side, Col top) {
    Paint paint{};
    paint.color = top;
    stack_ellipse(canvas, frame, x, y, heading, lx, ly, rx, ry, h0, h1, low, side, paint);
}

// A rounded top with a soft light from the upper left.
Paint domed(double lx, double ly, double r, Col color) {
    return Paint::rad(lx - r * 0.35, ly - r * 0.35, r * 1.5, {{0, shade(color, 1.22f)}, {0.6f, color}, {1, shade(color, 0.72f)}});
}

// The shadow a thing of some height throws on the ground: its outline slid down and right.
void ground_shadow_ellipse(Canvas& canvas, const Frame& frame, double x, double y, double rx, double ry, double height, float alpha) {
    enter(canvas, frame, x + shadow_x * height, y + shadow_y * height, 0);
    canvas.fill_ellipse(0, 0, rx, ry, rgb(6, 14, 4, alpha));
    canvas.restore();
}

// Brick courses over a screen rectangle: a face of a wall.
void brick_face(Canvas& canvas, double x0, double y0, double x1, double y1, double course, float light) {
    canvas.fill_rect(x0, y0, x1 - x0, y1 - y0, shade(hex(0x9A4E36), light));
    int row = 0;
    for (double y = y0; y < y1; y += course, ++row) {
        canvas.fill_rect(x0, y, x1 - x0, std::max(1.0, course * 0.09), shade(hex(0xC9B9A6), light));
        const double shift = row % 2 == 0 ? 0 : course * 1.1;
        for (double x = x0 - shift; x < x1; x += course * 2.2)
            canvas.fill_rect(std::max(x0, x), y, std::max(1.0, course * 0.08), std::min(course, y1 - y), shade(hex(0xC9B9A6), light));
    }
}

} // namespace

namespace {

double sky_smooth(double a, double b, double x) {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

} // namespace

// The world outside the lawn: a summer sky with fair-weather cumulus, a far line of
// trees in the haze, and the brick wall along the back. The lawn runs to the window's
// other edges.
void draw_surround(Canvas& canvas, const Frame& frame) {
    const double wall_top = frame.py(0) - frame.up(wall_height);
    const int sky_rows = std::clamp(static_cast<int>(std::ceil(wall_top)) + 1, 0, canvas.h);
    const double h = std::max(1.0, wall_top);
    // the sun: up and to the left, out of the picture
    const double sun_x = -0.12 * canvas.w;
    const double sun_y = -0.9 * h;
    for (int y = 0; y < sky_rows; ++y) {
        // 0 at the top of the picture, 1 at the horizon behind the wall
        const double t = (y + 0.5) / h;
        // Rayleigh blue overhead paling to a milky horizon
        const double up = std::pow(1 - std::min(1.0, t), 1.6);
        double r = 0.74 + (0.27 - 0.74) * up;
        double g = 0.84 + (0.50 - 0.84) * up;
        double b = 0.93 + (0.86 - 0.93) * up;
        std::uint8_t* row = canvas.px.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(canvas.w) * 4U;
        // clouds lie on a plane overhead: towards the horizon they crowd together and flatten
        const double depth = 1.0 / std::max(0.08, 1.15 - t);
        for (int x = 0; x < canvas.w; ++x) {
            double cr = r;
            double cg = g;
            double cb = b;
            // the glow about the sun
            const double d = std::hypot(x - sun_x, (y - sun_y) * 1.6) / canvas.w;
            const double glow = std::exp(-d * 2.6) * 0.32 + std::exp(-d * 9.0) * 0.25;
            cr += glow * 1.0;
            cg += glow * 0.86;
            cb += glow * 0.62;
            // cumulus: domain-warped noise on the cloud plane, thresholded into heaps
            // (on the plane, distance grows with depth both across and away, so heaps keep their shape)
            const double u = (x - canvas.w * 0.5) / canvas.w * 5.0 * depth + 11.3;
            const double v = depth * 0.55 * canvas.w / std::max(1.0, h * 4.0);
            const V3 q{u + 0.5 * fbm3(V3{u * 0.6, v * 0.6, 3.1}, 3), v + 0.5 * fbm3(V3{u * 0.6 + 5.2, v * 0.6, 1.7}, 3), 0.5};
            const double n = fbm3(V3{q.x, q.y, 0.5}, 5);
            const double cover = sky_smooth(0.56, 0.68, n);
            if (cover > 0.001) {
                // lit from the sun above and to the left; grey and blue in the bases and away from it
                const double top = fbm3(V3{q.x - 0.06, q.y + 0.06, 0.5}, 5);
                const double lit = std::clamp(0.6 + (n - top) * 8.0, 0.0, 1.0);
                const double thick = sky_smooth(0.6, 0.85, n);
                const double base = 0.84 + 0.18 * lit - 0.12 * thick;
                const double cloud_r = base * 1.00 + 0.10 * glow;
                const double cloud_g = base * 0.99 + 0.06 * glow;
                const double cloud_b = base * 1.02;
                // distant clouds fade into the haze
                const double haze = sky_smooth(0.6, 1.0, t) * 0.55;
                const double a = cover * (1 - haze);
                cr = cr + (cloud_r - cr) * a;
                cg = cg + (cloud_g - cg) * a;
                cb = cb + (cloud_b - cb) * a;
            }
            std::uint8_t* px = row + static_cast<std::size_t>(x) * 4U;
            px[0] = static_cast<std::uint8_t>(std::lround(255 * std::clamp(cb, 0.0, 1.0)));
            px[1] = static_cast<std::uint8_t>(std::lround(255 * std::clamp(cg, 0.0, 1.0)));
            px[2] = static_cast<std::uint8_t>(std::lround(255 * std::clamp(cr, 0.0, 1.0)));
            px[3] = 255;
        }
    }
    // A far line of trees over the wall, blue-green in the haze.
    for (int x = 0; x < canvas.w; ++x) {
        const double u = static_cast<double>(x) / canvas.w;
        const double crown = fbm3(V3{u * 9.0, 0.3, 0.7}, 4);
        const double bumps = fbm3(V3{u * 60.0, 1.3, 0.2}, 2);
        const double rise = frame.ppm * (0.25 + 0.9 * crown * crown + 0.18 * bumps);
        const double y0 = wall_top - rise;
        for (int y = std::max(0, static_cast<int>(std::floor(y0))); y < std::min(canvas.h, static_cast<int>(std::ceil(wall_top)) + 1); ++y) {
            const double cover = std::clamp(y + 1 - y0, 0.0, 1.0);
            const double shade = 0.85 + 0.25 * fbm3(V3{u * 140.0, y * 0.12, 4.0}, 2);
            std::uint8_t* px = canvas.px.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(canvas.w) + static_cast<std::size_t>(x)) * 4U;
            // greener and darker low down, paler and bluer in the haze at the tops
            const double low = std::clamp((y - y0) / std::max(1.0, rise), 0.0, 1.0);
            const double tr = (0.36 - 0.10 * low) * shade;
            const double tg = (0.48 - 0.08 * low) * shade;
            const double tb = (0.46 - 0.14 * low) * shade;
            px[2] = static_cast<std::uint8_t>(std::lround(px[2] + (255 * tr - px[2]) * cover));
            px[1] = static_cast<std::uint8_t>(std::lround(px[1] + (255 * tg - px[1]) * cover));
            px[0] = static_cast<std::uint8_t>(std::lround(px[0] + (255 * tb - px[0]) * cover));
        }
    }
    // Beyond the lawn's sides (only on a window wider than the garden): a clipped hedge.
    if (frame.px(0) > 0)
        canvas.fill_rect(0, wall_top, frame.px(0), canvas.h - wall_top, hex(0x23401C));
    if (frame.px(lawn_width) < canvas.w)
        canvas.fill_rect(frame.px(lawn_width), wall_top, canvas.w - frame.px(lawn_width), canvas.h - wall_top, hex(0x23401C));
    // The brick wall along the back, with its coping.
    const double thick = 0.3 * frame.ppm;
    const double course = std::max(3.0, frame.up(0.075) * 1.3);
    brick_face(canvas, frame.px(0) - thick, wall_top + thick * frame.tilt, frame.px(lawn_width) + thick, frame.py(0), course, 0.92f);
    canvas.fill_rect(frame.px(0) - thick, wall_top, lawn_width * frame.ppm + thick * 2, thick * frame.tilt, hex(0xD8CDBC));
    canvas.fill_rect(frame.px(0) - thick, wall_top + thick * frame.tilt - 1, lawn_width * frame.ppm + thick * 2, 1, hex(0x8C8072));
}

// A bed's edging: stones from Rock Stack's generator set round the outline, each nudged
// in or out, along and round a little, and sunk into the soil, so the ring looks laid by
// hand. The mulch and the flowers inside are the lawn art's.
void draw_bed(Canvas& canvas, const Frame& frame, const Bed& bed) {
    std::uint64_t random = bed.seed | 1U;
    Mesh mesh{};
    const double level = detail_for(frame.ppm);
    // The outline, finely sampled, with the distance along it; then a stone at each
    // step of about its own size along that distance.
    std::vector<double> xs{};
    std::vector<double> ys{};
    std::vector<double> along{};
    const int samples = 720;
    for (int k = 0; k <= samples; ++k) {
        const double t = 2 * pi * k / samples;
        double c = std::cos(t);
        double s = std::sin(t);
        if (bed.shape == BedShape::rounded) {
            c = std::copysign(std::pow(std::abs(c), 0.5), c);
            s = std::copysign(std::pow(std::abs(s), 0.5), s);
        }
        xs.push_back(bed.x + c * bed.rx);
        ys.push_back(bed.y + s * bed.ry);
        along.push_back(k == 0 ? 0.0 : along.back() + std::hypot(xs.back() - xs[xs.size() - 2], ys.back() - ys[ys.size() - 2]));
    }
    const double perimeter = along.back();
    double at = random_range(random, 0, 0.1);
    std::size_t index = 0;
    while (at < perimeter - 0.06) {
        const double size = random_range(random, 0.09, 0.15);
        while (index + 1 < along.size() && along[index + 1] < at)
            ++index;
        const std::size_t next = std::min(index + 1, along.size() - 1);
        const double span = std::max(1e-9, along[next] - along[index]);
        const double u = std::clamp((at - along[index]) / span, 0.0, 1.0);
        const double px = xs[index] + (xs[next] - xs[index]) * u;
        const double py = ys[index] + (ys[next] - ys[index]) * u;
        // outward: across the outline's direction there
        const V3 tangent = normalized(V3{xs[next] - xs[index], ys[next] - ys[index], 0});
        const V3 normal{tangent.y, -tangent.x, 0};
        const double out = random_range(random, -0.035, 0.035);
        const double slide = random_range(random, -0.015, 0.015);
        const double x = px + normal.x * out + tangent.x * slide;
        const double y = py + normal.y * out + tangent.y * slide;
        const double turn = random_range(random, 0, 2 * pi);
        const double tilt_x = random_range(random, -0.18, 0.18);
        const double tilt_y = random_range(random, -0.18, 0.18);
        const Xform place = translation(V3{x, y, size * random_range(random, 0.08, 0.2)}) * rotation_z(turn) * rotation_x(tilt_x) * rotation_y(tilt_y);
        build_stone(mesh, static_cast<std::uint32_t>(next_random(random)), size, level, place);
        at += size * random_range(random, 0.78, 0.95);
    }
    bake_occlusion(mesh, 24);
    const std::vector<Material> materials{paint_material(0xFFFFFF, 0.32F, 0.035F)};
    std::vector<Part> parts{};
    parts.push_back(Part{&mesh, &materials, Xform{}, false, nullptr});
    Shot shot{};
    ShotOptions options{};
    options.shadow_strength = 0.45F;
    options.clip_ground = true;
    shoot(frame, parts, options, shot);
    paint_shadow(canvas, shot);
    paint(canvas, shot);
}

namespace {

// Buildings stand against a wall; their long side runs along it. `hx` is half the length, `hy` half the depth.
void draw_greenhouse(Canvas& canvas, const Frame& frame, const Prop& prop, double heading, double hx, double hy) {
    std::uint64_t random = prop.seed | 1U;
    const double eaves = 1.7;
    const double ridge = 2.4;
    ground_shadow_ellipse(canvas, frame, prop.x, prop.y, hx * 1.1, hy * 1.1, eaves * 0.6, 0.22f);
    // What grows inside, on its staging.
    enter(canvas, frame, prop.x, prop.y, heading, 0.02);
    rounded(canvas, -hx, -hy, hx * 2, hy * 2, 0.02, hex(0x4A4034));
    canvas.restore();
    enter(canvas, frame, prop.x, prop.y, heading, 0.8);
    canvas.fill_rect(-hx + 0.1, -hy + 0.12, hx * 2 - 0.2, 0.42, hex(0x6B4A2E));
    canvas.fill_rect(-hx + 0.1, hy - 0.54, hx * 2 - 0.2, 0.42, hex(0x6B4A2E));
    for (int k = 0; k < 46; ++k) {
        const bool upper = k % 2 == 0;
        const double x = random_range(random, -hx + 0.18, hx - 0.18);
        const double y = (upper ? -hy + 0.33 : hy - 0.33) + random_range(random, -0.14, 0.14);
        const double r = random_range(random, 0.07, 0.13);
        canvas.fill_circle(x, y, r, mix(hex(0x2F6B2A), hex(0x6BA548), static_cast<float>(random_unit(random))));
        if (random_unit(random) < 0.3)
            canvas.fill_circle(x + r * 0.2, y - r * 0.2, r * 0.35, mix(hex(0xD8342B), hex(0xF2C230), static_cast<float>(random_unit(random))));
    }
    canvas.restore();
    // Glass walls, then the glass roof, with white bars.
    const int layers = layers_for(frame, 0, eaves);
    for (int k = 0; k < layers; ++k) {
        const double t = static_cast<double>(k) / (layers - 1);
        enter(canvas, frame, prop.x, prop.y, heading, eaves * t);
        canvas.begin();
        canvas.rrect(-hx, -hy, hx * 2, hy * 2, 0.02);
        canvas.stroke(rgb(200, 226, 236, static_cast<float>(0.35 + 0.25 * t)), 0.05);
        canvas.restore();
    }
    const int roof_layers = layers_for(frame, eaves, ridge);
    for (int k = 0; k < roof_layers; ++k) {
        const double t = static_cast<double>(k) / (roof_layers - 1);
        enter(canvas, frame, prop.x, prop.y, heading, eaves + (ridge - eaves) * t);
        const double d = hy * (1 - t) + 0.04;
        rounded(canvas, -hx, -d, hx * 2, d * 2, 0.02, rgb(214, 236, 240, static_cast<float>(0.16 + 0.1 * t)));
        canvas.restore();
    }
    const Col bar = hex(0xECEFF0);
    for (int k = 0; k <= 6; ++k) {
        const double lx = -hx + hx * 2 * k / 6;
        for (int rise = 0; rise < 2; ++rise) {
            enter(canvas, frame, prop.x, prop.y, heading, rise == 0 ? 0 : eaves);
            canvas.fill_rect(lx - 0.015, -hy, 0.03, hy * 2, bar);
            canvas.restore();
        }
    }
    enter(canvas, frame, prop.x, prop.y, heading, eaves);
    canvas.begin();
    canvas.rrect(-hx, -hy, hx * 2, hy * 2, 0.02);
    canvas.stroke(bar, 0.045);
    canvas.restore();
    enter(canvas, frame, prop.x, prop.y, heading, ridge);
    canvas.fill_rect(-hx, -0.03, hx * 2, 0.06, bar);
    canvas.restore();
}

void draw_pool(Canvas& canvas, const Frame& frame, const Prop& prop) {
    std::uint64_t random = prop.seed | 1U;
    const double r = prop.rx;
    const unsigned rims[3] = {0x2E86D0, 0xE8503A, 0x36A46A};
    const Col rim = hex(rims[next_random(random) % 3U]);
    ground_shadow_ellipse(canvas, frame, prop.x, prop.y, r, r, 0.4, 0.3f);
    // Three inflated rings: the stack's shading gives their rounded sides.
    for (int ring = 0; ring < 3; ++ring)
        stack_ellipse(canvas, frame, prop.x, prop.y, 0, 0, 0, r, r, ring * 0.16, ring * 0.16 + 0.16, 0.985, shade(rim, 0.78f), ring == 2 ? domed(0, 0, r, rim) : Paint::rad(0, 0, r, {{0, shade(rim, 0.85f)}, {1, shade(rim, 0.8f)}}));
    enter(canvas, frame, prop.x, prop.y, 0, 0.45);
    canvas.begin();
    canvas.circle(0, 0, r * 0.82);
    canvas.fill(Paint::rad(r * 0.2, r * 0.25, r * 0.9, {{0, hex(0x8FE0EA)}, {1, hex(0x49B5CF)}}));
    for (int k = 0; k < 5; ++k) {
        const double a = random_range(random, 0, 2 * pi);
        const double d = random_range(random, 0.1, 0.55) * r;
        canvas.fill_ellipse(std::cos(a) * d, std::sin(a) * d, 0.06, 0.03, rgb(255, 150, 60, 0.45f));
    }
    for (int k = 0; k < 9; ++k) {
        const double a = random_range(random, 0, 2 * pi);
        const double d = random_range(random, 0.05, 0.65) * r;
        canvas.fill_ellipse(std::cos(a) * d, std::sin(a) * d, random_range(random, 0.05, 0.12), 0.012, rgb(255, 255, 255, 0.4f));
    }
    canvas.restore();
    const double dx = r * 0.3;
    const double dy = -r * 0.25;
    stack_ellipse(canvas, frame, prop.x, prop.y, 0, dx, dy, 0.075, 0.06, 0.45, 0.52, 0.8, hex(0xC9A816), hex(0xF7D21E));
    stack_ellipse(canvas, frame, prop.x, prop.y, 0, dx + 0.055, dy - 0.01, 0.04, 0.04, 0.5, 0.6, 0.8, hex(0xD8B820), hex(0xFBE04A));
    enter(canvas, frame, prop.x, prop.y, 0, 0.6);
    canvas.fill_ellipse(dx + 0.10, dy - 0.01, 0.022, 0.013, hex(0xF08A24));
    canvas.restore();
}

} // namespace

void draw_prop(Canvas& canvas, const Frame& frame, const Prop& prop) {
    const bool turned = std::abs(prop.angle) > 0.1;
    const double heading = turned ? pi / 2 : 0;
    switch (prop.kind) {
    case PropKind::shed:
        draw_shed_model(canvas, frame, prop, heading, turned ? prop.ry : prop.rx, turned ? prop.rx : prop.ry);
        break;
    case PropKind::greenhouse:
        draw_greenhouse(canvas, frame, prop, heading, turned ? prop.ry : prop.rx, turned ? prop.rx : prop.ry);
        break;
    case PropKind::fountain:
        draw_fountain_model(canvas, frame, prop);
        break;
    case PropKind::birdbath:
        draw_birdbath_model(canvas, frame, prop);
        break;
    case PropKind::grill:
        draw_grill_model(canvas, frame, prop, heading);
        break;
    case PropKind::sandbox:
        draw_sandbox_model(canvas, frame, prop, heading);
        break;
    case PropKind::pool:
        draw_pool(canvas, frame, prop);
        break;
    case PropKind::chair:
        if (!draw_chair_model(canvas, frame, prop, heading)) {
            draw_lounger_model(canvas, frame, prop, heading);
            if (prop_style(prop) == 2)
                draw_parasol_model(canvas, frame, prop, heading);
        }
        break;
    }
}

// What moves on a prop: a fountain's water.
void draw_prop_live(Canvas& canvas, const Frame& frame, const Prop& prop, double time) {
    if (prop.kind == PropKind::fountain)
        draw_fountain_water(canvas, frame, prop, time);
    else if (prop.kind == PropKind::birdbath)
        draw_birdbath_water(canvas, frame, prop, time);
}

// A bee: striped body with a blur of wings, up at its height with a shadow left on the grass.
void draw_bee(Canvas& canvas, const Frame& frame, const Bee& bee, double time) {
    const double x = frame.px(bee.x);
    const double y = frame.py(bee.y) - frame.up(bee.z);
    const double s = frame.ppm * 0.028;
    ground_shadow_ellipse(canvas, frame, bee.x, bee.y, 0.025, 0.018, bee.z, 0.22f);
    const double heading = std::atan2(bee.vy, bee.vx);
    canvas.save();
    canvas.translate(x, y);
    canvas.scale(1, frame.tilt);
    canvas.rotate(bee.state == BeeState::visiting ? bee.wobble * 0.1 : heading);
    canvas.scale(s, s);
    const double beat = std::sin(time * 90 + bee.wobble);
    canvas.fill_ellipse(0.1, -0.75 - 0.15 * beat, 0.7, 0.32, rgb(230, 240, 255, 0.55f));
    canvas.fill_ellipse(0.1, 0.75 + 0.15 * beat, 0.7, 0.32, rgb(230, 240, 255, 0.55f));
    canvas.fill_ellipse(0, 0, 1.0, 0.6, hex(0xF2B51C));
    canvas.fill_rect(-0.35, -0.5, 0.25, 1.0, hex(0x1F1A12));
    canvas.fill_rect(0.15, -0.55, 0.25, 1.1, hex(0x1F1A12));
    canvas.fill_circle(0.95, 0, 0.3, hex(0x1F1A12));
    canvas.restore();
}

// A small bird: perched, a plump body with a tail; flying, wings out and beating.
void draw_bird(Canvas& canvas, const Frame& frame, const Bird& bird) {
    const bool flying = bird.state != BirdState::perched;
    const unsigned colours[4][3] = {{0x6B4F3A, 0xE8743B, 0xD9CFC1}, {0x3F7BC9, 0xF2D43B, 0xF6F3EA}, {0x1C1A18, 0x1C1A18, 0xF2B51C}, {0x7A5A3C, 0xB9A58A, 0x3A2A1C}};
    const Col back = hex(colours[bird.kind][0]);
    const Col front = hex(colours[bird.kind][1]);
    const Col mark = hex(colours[bird.kind][2]);
    const double x = frame.px(bird.x);
    const double y = frame.py(bird.y) - frame.up(bird.z);
    const double s = frame.ppm * (bird.kind == 2 ? 0.075 : 0.058);
    if (flying || bird.z < 1.5)
        ground_shadow_ellipse(canvas, frame, bird.x, bird.y, flying ? 0.09 : 0.05, 0.035, bird.z, static_cast<float>(flying ? 0.14 : 0.3));
    canvas.save();
    canvas.translate(x, y);
    canvas.scale(1, frame.tilt);
    canvas.rotate(bird.heading);
    canvas.scale(s, s);
    if (flying) {
        const double beat = std::sin(bird.flap * 2 * pi);
        const double span = 1.9 - 0.5 * std::abs(beat);
        canvas.begin();
        canvas.move(0.1, 0);
        canvas.quad(-0.2, -span * 0.6, -0.5, -span);
        canvas.quad(-0.9, -span * 0.4, -0.4, 0);
        canvas.quad(-0.9, span * 0.4, -0.5, span);
        canvas.quad(-0.2, span * 0.6, 0.1, 0);
        canvas.close();
        canvas.fill(shade(back, static_cast<float>(0.85 + 0.25 * beat)));
    }
    canvas.begin();
    canvas.move(-1.4, -0.22);
    canvas.line(-0.6, -0.1);
    canvas.line(-0.6, 0.1);
    canvas.line(-1.4, 0.22);
    canvas.close();
    canvas.fill(shade(back, 0.8f));
    canvas.begin();
    canvas.ellipse(0, 0, 0.9, 0.55);
    canvas.fill(Paint::rad(0.2, -0.15, 0.9, {{0, shade(back, 1.15f)}, {1, shade(back, 0.75f)}}));
    if (bird.kind != 2)
        canvas.fill_ellipse(0.35, 0, 0.35, 0.3, front);
    canvas.fill_circle(0.75, 0, 0.36, shade(back, 1.05f));
    if (bird.kind == 1)
        canvas.fill_circle(0.75, 0, 0.22, mark);
    canvas.begin();
    canvas.move(1.05, -0.1);
    canvas.line(1.4, 0);
    canvas.line(1.05, 0.1);
    canvas.close();
    canvas.fill(bird.kind == 2 ? mark : hex(0x4A3A28));
    canvas.fill_circle(0.9, -0.17, 0.07, hex(0x111111));
    canvas.fill_circle(0.9, 0.17, 0.07, hex(0x111111));
    canvas.restore();
    if (!flying) {
        // legs down to the perch
        canvas.stroke_line(x - s * 0.2, y + s * 0.4, x - s * 0.2, y + s * 0.85, hex(0x4A3A28), std::max(1.0, s * 0.08));
        canvas.stroke_line(x + s * 0.2, y + s * 0.4, x + s * 0.2, y + s * 0.85, hex(0x4A3A28), std::max(1.0, s * 0.08));
    }
}

namespace {

// A digit in seven strokes, in a box `size` tall at (x, y) top-left.
void digit(Canvas& canvas, int value, double x, double y, double size, Col color) {
    const unsigned segments[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    const unsigned on = segments[std::clamp(value, 0, 9)];
    const double w = size * 0.5;
    const double half = size * 0.5;
    const double width = std::max(1.5, size * 0.13);
    const double ends[7][4] = {{0, 0, w, 0}, {w, 0, w, half}, {w, half, w, size}, {0, size, w, size}, {0, half, 0, size}, {0, 0, 0, half}, {0, half, w, half}};
    for (int k = 0; k < 7; ++k)
        if ((on >> static_cast<unsigned>(k)) & 1U)
            canvas.stroke_line(x + ends[k][0], y + ends[k][1], x + ends[k][2], y + ends[k][3], color, width);
}

} // namespace

void draw_chase_timer(Canvas& canvas, const Frame& frame, const Granny& granny, double time) {
    if (granny.state != GrannyState::walking && granny.state != GrannyState::turning)
        return;
    const double left = std::max(0.0, granny_patience - granny.chase);
    const double share = left / granny_patience;
    const bool urgent = left < 5.0;
    const double pulse = urgent ? 0.5 + 0.5 * std::sin(time * 12) : 0.0;
    // the stopwatch at the top middle of the lawn
    const double size = std::max(16.0, frame.ppm * 0.9);
    const double cx = frame.px(lawn_width * 0.5);
    const double cy = frame.py(0) + size * 0.95;
    const double box_w = size * 3.3;
    canvas.begin();
    canvas.rrect(cx - box_w * 0.5, cy - size * 0.75, box_w, size * 1.5, size * 0.75);
    canvas.fill(rgb(20, 24, 18, 0.72f));
    // its dial, emptying as time runs out
    const double dial_x = cx - box_w * 0.5 + size * 0.75;
    const double r = size * 0.48;
    canvas.fill_circle(dial_x, cy, r, rgb(240, 236, 226));
    canvas.begin();
    canvas.move(dial_x, cy);
    const int steps = 40;
    for (int k = 0; k <= steps; ++k) {
        const double a = -1.5707963 + 6.2831853 * share * k / steps;
        canvas.line(dial_x + std::cos(a) * r * 0.86, cy + std::sin(a) * r * 0.86);
    }
    canvas.close();
    canvas.fill(urgent ? mix(hex(0xE2402F), hex(0xFFB020), static_cast<float>(pulse)) : hex(0x3C9A48));
    canvas.fill_rect(dial_x - r * 0.18, cy - r * 1.35, r * 0.36, r * 0.3, rgb(240, 236, 226));
    // the seconds left
    const int whole = static_cast<int>(std::ceil(left));
    const Col ink = urgent ? mix(hex(0xFFFFFF), hex(0xFF6A50), static_cast<float>(pulse)) : hex(0xFFFFFF);
    const double dx = dial_x + r + size * 0.35;
    digit(canvas, whole / 10, dx, cy - size * 0.42, size * 0.84, ink);
    digit(canvas, whole % 10, dx + size * 0.62, cy - size * 0.42, size * 0.84, ink);
    // and a small clock over her head
    const double hx = frame.px(granny.x);
    const double hy = frame.py(granny.y) - frame.up(2.3);
    const double hr = std::max(5.0, frame.ppm * 0.22);
    canvas.fill_circle(hx, hy, hr * 1.15, rgb(20, 24, 18, 0.6f));
    canvas.begin();
    canvas.move(hx, hy);
    for (int k = 0; k <= steps; ++k) {
        const double a = -1.5707963 + 6.2831853 * share * k / steps;
        canvas.line(hx + std::cos(a) * hr, hy + std::sin(a) * hr);
    }
    canvas.close();
    canvas.fill(urgent ? hex(0xE2402F) : hex(0xF2C230));
}

void draw_particles(Canvas& canvas, const Frame& frame, const std::vector<Particle>& particles) {
    for (const Particle& p : particles) {
        const double fade = std::clamp((p.life - p.age) / 0.3, 0.0, 1.0);
        const Col tint = hex(p.tint, static_cast<float>(fade));
        const double x = frame.px(p.x);
        const double y = frame.py(p.y) - frame.up(p.z);
        const double r = std::max(0.8, p.size * frame.ppm);
        if (p.z > 0.02)
            canvas.fill_ellipse(frame.px(p.x + shadow_x * p.z), frame.py(p.y + shadow_y * p.z), r, r * frame.tilt, rgb(6, 14, 4, static_cast<float>(0.25 * fade)));
        if (p.kind == ParticleKind::leaf) {
            const double a = p.spin * p.age;
            canvas.fill_ellipse(x, y, r * 1.3, r * 0.7, tint);
            canvas.stroke_line(x - std::cos(a) * r, y - std::sin(a) * r, x + std::cos(a) * r, y + std::sin(a) * r, alpha(tint, 0.6f), std::max(0.5, r * 0.2));
        } else if (p.kind == ParticleKind::shard || p.kind == ParticleKind::clipping || p.kind == ParticleKind::mulch) {
            const double a = p.spin * p.age;
            canvas.begin();
            canvas.move(x + std::cos(a) * r * 1.4, y + std::sin(a) * r * 1.4);
            canvas.line(x + std::cos(a + 2.3) * r, y + std::sin(a + 2.3) * r);
            canvas.line(x + std::cos(a + 4.0) * r * 0.8, y + std::sin(a + 4.0) * r * 0.8);
            canvas.close();
            canvas.fill(tint);
        } else if (p.kind == ParticleKind::hat) {
            canvas.begin();
            canvas.circle(x, y, r * 1.2);
            canvas.fill(Paint::rad(x - r * 0.4, y - r * 0.4, r * 1.6, {{0, hex(0xE24A40, static_cast<float>(fade))}, {1, hex(0x9E1F18, static_cast<float>(fade))}}));
        } else if (p.kind == ParticleKind::seed) {
            canvas.fill_circle(x, y, r * 0.5, tint);
            canvas.stroke_line(x, y, x + r * 1.2, y + r * 1.2, alpha(tint, 0.5f), std::max(0.5, r * 0.15));
        } else {
            canvas.fill_circle(x, y, r, tint);
        }
    }
}

} // namespace mm
