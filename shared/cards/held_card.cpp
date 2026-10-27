#include "held_card.hpp"

#include "table_art.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace games {
namespace {

struct Vec {
    double x;
    double y;
    double z;
};
Vec plus(const Vec& a, const Vec& b) {
    const Vec sum{a.x + b.x, a.y + b.y, a.z + b.z};
    return sum;
}
Vec times(const Vec& a, double s) {
    const Vec product{a.x * s, a.y * s, a.z * s};
    return product;
}
double dot(const Vec& a, const Vec& b) {
    const double sum = a.x * b.x + a.y * b.y + a.z * b.z;
    return sum;
}
Vec unit(const Vec& a) {
    const double length = std::sqrt(dot(a, a));
    const Vec result = length > 0 ? times(a, 1 / length) : Vec{0, 0, 1};
    return result;
}

// A turn as a matrix: rows of the rotation taking the card's own axes to the screen's.
struct Matrix {
    double m[3][3];
};
Matrix matrix_of(const Turn& turn) {
    const double length = std::sqrt(turn.w * turn.w + turn.x * turn.x + turn.y * turn.y + turn.z * turn.z);
    const double w = length > 0 ? turn.w / length : 1, x = length > 0 ? turn.x / length : 0;
    const double y = length > 0 ? turn.y / length : 0, z = length > 0 ? turn.z / length : 0;
    const Matrix result{{{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                         {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                         {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}}};
    return result;
}
// The card's own axes to the screen's, and back.
Vec onto_screen(const Matrix& r, const Vec& v) {
    const Vec result{r.m[0][0] * v.x + r.m[0][1] * v.y + r.m[0][2] * v.z,
                     r.m[1][0] * v.x + r.m[1][1] * v.y + r.m[1][2] * v.z,
                     r.m[2][0] * v.x + r.m[2][1] * v.y + r.m[2][2] * v.z};
    return result;
}
Vec onto_card(const Matrix& r, const Vec& v) {
    const Vec result{r.m[0][0] * v.x + r.m[1][0] * v.y + r.m[2][0] * v.z,
                     r.m[0][1] * v.x + r.m[1][1] * v.y + r.m[2][1] * v.z,
                     r.m[0][2] * v.x + r.m[1][2] * v.y + r.m[2][2] * v.z};
    return result;
}

// The room: a lamp above and to the left, in front of the page, as the card table's; a dim
// window to the right; and the light that comes from everywhere.
const Vec kLamp{-.45, -.6, .66};
const Vec kWindow{.62, -.25, .74};
constexpr double kAmbient = .40;
constexpr double kLampLight = .82;
constexpr double kWindowLight = .20;
constexpr double kKnee = .82;  // linear light above which highlights roll off toward white

// Linear light to an sRGB level (0..255), by table: 4096 steps, interpolated.
constexpr int kEncodeSteps = 4096;
std::vector<double> make_encode_table() {
    std::vector<double> table(kEncodeSteps + 2);
    for (int i = 0; i <= kEncodeSteps + 1; ++i) {
        const double x = std::min(1.0, static_cast<double>(i) / kEncodeSteps);
        const double v = x <= .0031308 ? x * 12.92 : 1.055 * std::pow(x, 1 / 2.4) - .055;
        table[static_cast<std::size_t>(i)] = 255 * v;
    }
    return table;
}
const std::vector<double>& encode_table() {
    static const std::vector<double> table = make_encode_table();
    return table;
}
double encode(const std::vector<double>& table, double x) {
    double rolled = x;
    if (rolled > kKnee)
        rolled = kKnee + (1 - kKnee) * std::tanh((rolled - kKnee) / (1 - kKnee));
    const double at = std::clamp(rolled, 0.0, 1.0) * kEncodeSteps;
    const int i = static_cast<int>(at);
    const double f = at - i;
    const double v = table[static_cast<std::size_t>(i)] * (1 - f) + table[static_cast<std::size_t>(i) + 1] * f;
    return v;
}

// Packing: what a kept texel's fixed steps are worth.
constexpr double kColourSteps = 65535;
constexpr double kUnitSteps = 255;
constexpr double kSlopeSteps = 4096;   // slopes to +-8
constexpr double kFacetSteps = 254;    // facet tilts to +-.5
PackedTexel pack(const CardTexel& t) {
    PackedTexel p;
    p.red = static_cast<std::uint16_t>(std::lround(std::clamp(static_cast<double>(t.red), 0.0, 1.0) * kColourSteps));
    p.green = static_cast<std::uint16_t>(std::lround(std::clamp(static_cast<double>(t.green), 0.0, 1.0) * kColourSteps));
    p.blue = static_cast<std::uint16_t>(std::lround(std::clamp(static_cast<double>(t.blue), 0.0, 1.0) * kColourSteps));
    p.ink = static_cast<std::uint8_t>(std::lround(std::clamp(static_cast<double>(t.ink), 0.0, 1.0) * kUnitSteps));
    p.foil = static_cast<std::uint8_t>(std::lround(std::clamp(static_cast<double>(t.foil), 0.0, 1.0) * kUnitSteps));
    p.slope_x = static_cast<std::int16_t>(std::lround(std::clamp(t.slope_x * kSlopeSteps, -32767.0, 32767.0)));
    p.slope_y = static_cast<std::int16_t>(std::lround(std::clamp(t.slope_y * kSlopeSteps, -32767.0, 32767.0)));
    p.facet_x = static_cast<std::int8_t>(std::lround(std::clamp(t.facet_x * kFacetSteps, -127.0, 127.0)));
    p.facet_y = static_cast<std::int8_t>(std::lround(std::clamp(t.facet_y * kFacetSteps, -127.0, 127.0)));
    p.facet = static_cast<std::uint8_t>(std::lround(std::clamp(static_cast<double>(t.facet), 0.0, 1.0) * kUnitSteps));
    return p;
}

// A texel read back, as the lighting wants it.
struct Texel {
    double red, green, blue, ink, foil, slope_x, slope_y, facet_x, facet_y, facet;
};
// The face at (u, v), 0..1 each, bilinearly.
Texel sample(const CardFaceLevel& level, double u, double v) {
    const double tx = std::clamp(u * level.width - .5, 0.0, level.width - 1.0);
    const double ty = std::clamp(v * level.height - .5, 0.0, level.height - 1.0);
    const int x0 = static_cast<int>(tx), y0 = static_cast<int>(ty);
    const int x1 = std::min(x0 + 1, level.width - 1), y1 = std::min(y0 + 1, level.height - 1);
    const double fx = tx - x0, fy = ty - y0;
    const std::size_t w = static_cast<std::size_t>(level.width);
    const PackedTexel& a = level.texels[static_cast<std::size_t>(y0) * w + static_cast<std::size_t>(x0)];
    const PackedTexel& b = level.texels[static_cast<std::size_t>(y0) * w + static_cast<std::size_t>(x1)];
    const PackedTexel& c = level.texels[static_cast<std::size_t>(y1) * w + static_cast<std::size_t>(x0)];
    const PackedTexel& d = level.texels[static_cast<std::size_t>(y1) * w + static_cast<std::size_t>(x1)];
    const double wa = (1 - fx) * (1 - fy), wb = fx * (1 - fy), wc = (1 - fx) * fy, wd = fx * fy;
    Texel t;
    t.red = (a.red * wa + b.red * wb + c.red * wc + d.red * wd) / kColourSteps;
    t.green = (a.green * wa + b.green * wb + c.green * wc + d.green * wd) / kColourSteps;
    t.blue = (a.blue * wa + b.blue * wb + c.blue * wc + d.blue * wd) / kColourSteps;
    t.ink = (a.ink * wa + b.ink * wb + c.ink * wc + d.ink * wd) / kUnitSteps;
    t.foil = (a.foil * wa + b.foil * wb + c.foil * wc + d.foil * wd) / kUnitSteps;
    t.slope_x = (a.slope_x * wa + b.slope_x * wb + c.slope_x * wc + d.slope_x * wd) / kSlopeSteps;
    t.slope_y = (a.slope_y * wa + b.slope_y * wb + c.slope_y * wc + d.slope_y * wd) / kSlopeSteps;
    t.facet_x = (a.facet_x * wa + b.facet_x * wb + c.facet_x * wc + d.facet_x * wd) / kFacetSteps;
    t.facet_y = (a.facet_y * wa + b.facet_y * wb + c.facet_y * wc + d.facet_y * wd) / kFacetSteps;
    t.facet = (a.facet * wa + b.facet * wb + c.facet * wc + d.facet * wd) / kUnitSteps;
    return t;
}

CardTexel blend(const CardTexel& a, const CardTexel& b, float f) {
    const float g = 1 - f;
    CardTexel t;
    t.red = a.red * g + b.red * f;
    t.green = a.green * g + b.green * f;
    t.blue = a.blue * g + b.blue * f;
    t.ink = a.ink * g + b.ink * f;
    t.foil = a.foil * g + b.foil * f;
    t.slope_x = a.slope_x * g + b.slope_x * f;
    t.slope_y = a.slope_y * g + b.slope_y * f;
    t.facet_x = a.facet_x * g + b.facet_x * f;
    t.facet_y = a.facet_y * g + b.facet_y * f;
    t.facet = a.facet * g + b.facet * f;
    return t;
}
CardFaceLevel packed(const CardFaceMap& map) {
    CardFaceLevel level;
    level.width = map.width;
    level.height = map.height;
    level.texels.resize(map.texels.size());
    for (std::size_t i = 0; i < map.texels.size(); ++i)
        level.texels[i] = pack(map.texels[i]);
    return level;
}

std::uint32_t mix(std::uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}
double unit_of(std::uint32_t h) {
    const double u = (h & 0xffffffU) / 16777216.0;
    return u;
}

// The convex hull of `count` points (at most eight), as indices into them in order round it:
// its count (fewer than three for points in a line or together). Walked from the leftmost point;
// every point lies on the same side of each edge.
int convex_hull(const double* xs, const double* ys, int count, int* out) {
    int start = 0;
    for (int i = 1; i < count; ++i)
        if (xs[i] < xs[start] || (xs[i] == xs[start] && ys[i] < ys[start]))
            start = i;
    int sides = 0;
    int at = start;
    do {
        out[sides++] = at;
        int next = (at + 1) % count;
        for (int i = 0; i < count; ++i) {
            const double turn = (xs[next] - xs[at]) * (ys[i] - ys[at]) - (ys[next] - ys[at]) * (xs[i] - xs[at]);
            const bool farther = std::hypot(xs[i] - xs[at], ys[i] - ys[at]) > std::hypot(xs[next] - xs[at], ys[next] - ys[at]);
            if (turn > 0 || (turn == 0 && farther))
                next = i;
        }
        at = next;
    } while (at != start && sides < count);
    return sides;
}
// A convex outline as half-planes: for each edge, its outward normal and offset, so that
// n . p - offset is a point's distance outside it. Returns the edges kept (0 for none).
int outline_of(const double* xs, const double* ys, int count, double* nx, double* ny, double* off) {
    int hull[8] = {};
    const int sides = convex_hull(xs, ys, count, hull);
    if (sides < 3)
        return 0;
    for (int i = 0; i < sides; ++i) {
        const int a = hull[i], b = hull[(i + 1) % sides];
        const double ex = xs[b] - xs[a], ey = ys[b] - ys[a];
        const double length = std::hypot(ex, ey);
        if (length < 1e-6)
            return 0;
        nx[i] = -ey / length;
        ny[i] = ex / length;
        off[i] = nx[i] * xs[a] + ny[i] * ys[a];
    }
    return sides;
}

// Everything a frame's pixels read, fixed before the first: the eye, the rays and the room in
// the card's own axes, the slab's outline on the picture and the shadow's on the page.
struct Frame {
    const CardFace* front;
    const CardFace* back;
    const CardMaterial* material;
    const std::vector<double>* encoding;
    CardPose pose;
    CardPicture picture;
    PixelBox box;
    Vec half;                 // the slab's half-sizes, points
    Vec eye;                  // the eye, from the card's middle, card axes
    Vec ray_origin_pixel;     // the ray toward pixel (0, 0)'s centre, card axes
    Vec ray_across;           // and its change a pixel across
    Vec ray_down;             // and a pixel down
    Vec lamp;                 // toward the lamp, card axes
    Vec window;               // toward the window
    Vec up;                   // the room's up
    int outline_sides;        // the slab's outline in the picture, device pixels: up to 8 half-planes
    double outline_nx[8];
    double outline_ny[8];
    double outline_off[8];
    int shadow_sides;         // the shadow's outline on the page, points: 0, 3 or 4 half-planes
    double shadow_nx[4];
    double shadow_ny[4];
    double shadow_off[4];
    double shadow_blur;       // points
    double shadow_strength;
    double page_scale;        // a picture point to the page point beneath it, about the middle
};

struct Lit {
    double red;
    double green;
    double blue;
};
// Gold, lit: little of its own colour, mostly the room and the lamp mirrored in it.
double gold_light(const Frame& f, double light, const Vec& mirror, double aim) {
    const double room = .20 + .50 * std::max(0.0, dot(mirror, f.up));
    const double gold = .12 * light + room + 2.2 * std::exp(30 * (aim - 1)) + .9 * std::exp(3 * (aim - 1));
    return gold;
}
// The colour a face shows at a point, lit: `n` its normal, `toward_eye` the way to the eye
// (both card axes, unit), `facet_normal` the glint facet's.
Lit light_face(const Frame& f, const Texel& t, const Vec& n, const Vec& toward_eye, const Vec& facet_normal) {
    const CardMaterial& m = *f.material;
    const double lamp = std::max(0.0, dot(n, f.lamp)), window = std::max(0.0, dot(n, f.window));
    const double light = kAmbient + kLampLight * lamp + kWindowLight * window;
    const double facing = std::max(1e-3, dot(n, toward_eye));
    const Vec mirror = plus(times(n, 2 * facing), times(toward_eye, -1));
    const double aim = dot(mirror, f.lamp);  // 1 where the lamp is seen mirrored
    const double away = 1 - facing;
    const double grazing = away * away * away * away * away;
    // The stock: a soft sheen and the lamp's glare in its varnish, brighter as it is turned away.
    const double sheen = m.sheen * (.06 * std::exp(6 * (aim - 1)) + .30 * std::exp(45 * (aim - 1))) * (1 + 3 * grazing);
    double sparkle = 0;
    if (t.facet > 0 && m.sparkle > 0) {
        const Vec halfway = unit(plus(f.lamp, toward_eye));
        const double caught = dot(facet_normal, halfway);
        if (caught > .97)
            sparkle = m.sparkle * t.facet * 6 * std::exp(500 * (caught - 1));
    }
    const double ink = std::clamp(t.ink, 0.0, 1.0);
    const double paper_add = sheen + sparkle * (1 - ink);
    Lit out{t.red * light + paper_add, t.green * light + paper_add, t.blue * light + paper_add};
    if (ink <= 0)
        return out;
    // The ink: raised and glossy.
    const double gloss = .06 * std::exp(8 * (aim - 1)) + .9 * std::exp(150 * (aim - 1));
    double inked[3] = {m.ink[0] * light + gloss, m.ink[1] * light + gloss, m.ink[2] * light + gloss};
    const double foil = std::clamp(t.foil * f.pose.foil, 0.0, 1.0);
    if (foil > 0) {
        const double gold = gold_light(f, light, mirror, aim);
        for (int c = 0; c < 3; ++c)
            inked[c] = inked[c] * (1 - foil) + m.foil[c] * gold * foil;
    }
    out.red = out.red * (1 - ink) + inked[0] * ink;
    out.green = out.green * (1 - ink) + inked[1] * ink;
    out.blue = out.blue * (1 - ink) + inked[2] * ink;
    return out;
}
// The cut edge at `at` (card axes, points): the two plies' stock and the line where they meet,
// or gilt.
Lit light_edge(const Frame& f, const Vec& n, const Vec& toward_eye, const Vec& at) {
    const CardMaterial& m = *f.material;
    const double lamp = std::max(0.0, dot(n, f.lamp)), window = std::max(0.0, dot(n, f.window));
    const double light = kAmbient + kLampLight * lamp + kWindowLight * window;
    const double facing = std::max(1e-3, dot(n, toward_eye));
    const Vec mirror = plus(times(n, 2 * facing), times(toward_eye, -1));
    const double aim = dot(mirror, f.lamp);
    if (m.gilt_edge) {
        const double gold = gold_light(f, light, mirror, aim);
        return {m.foil[0] * gold, m.foil[1] * gold, m.foil[2] * gold};
    }
    const double across = f.half.z > 0 ? at.z / (f.half.z * .22) : 0;
    const double seam = 1 - m.seam * std::exp(-across * across);
    const double sheen = .04 * std::exp(6 * (aim - 1));
    return {m.edge[0] * light * seam + sheen, m.edge[1] * light * seam + sheen, m.edge[2] * light * seam + sheen};
}

// Where the eye's ray along `d` (card axes) meets the slab: the slab's entry, ...
struct Strike {
    bool hit;
    int axis;
    double side;
    double t;
};
Strike strike_box(const Vec& origin, const Vec& direction, const Vec& half) {
    const double o[3] = {origin.x, origin.y, origin.z};
    const double d[3] = {direction.x, direction.y, direction.z};
    const double h[3] = {half.x, half.y, half.z};
    double nearest = -std::numeric_limits<double>::infinity();
    double farthest = std::numeric_limits<double>::infinity();
    int axis = -1;
    double side = 0;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-12) {
            if (std::abs(o[i]) > h[i])
                return {false, -1, 0, 0};
            continue;
        }
        const double a = (-h[i] - o[i]) / d[i], b = (h[i] - o[i]) / d[i];
        const double entry = std::min(a, b), exit = std::max(a, b);
        if (entry > nearest) {
            nearest = entry;
            axis = i;
            side = d[i] > 0 ? -1.0 : 1.0;
        }
        farthest = std::min(farthest, exit);
    }
    if (axis < 0 || nearest > farthest || farthest < 0)
        return {false, -1, 0, 0};
    return {true, axis, side, nearest};
}

// ... and what is seen there: the lit colour, and whether the point is well inside its face
// (more than a pixel and a half from every edge), so that the pixel's other rays would see the
// same face.
struct Seen {
    Lit colour;
    bool inside;
};
Seen look(const Frame& f, const Vec& d, const Strike& strike) {
    const Vec at = plus(f.eye, times(d, strike.t));
    const Vec toward_eye = unit(times(d, -1));
    if (strike.axis != 2) {
        const Vec n = strike.axis == 0 ? Vec{strike.side, 0, 0} : Vec{0, strike.side, 0};
        return {light_edge(f, n, toward_eye, at), false};
    }
    const bool front = strike.side > 0;
    const CardFace& face = front ? *f.front : *f.back;
    // How far the next pixel's rays meet the same face from this one: the pixel's footprint.
    const double plane = strike.side * f.half.z;
    double reach = 0;
    const Vec neighbours[2] = {plus(d, f.ray_across), plus(d, f.ray_down)};
    for (const Vec& next : neighbours) {
        if (std::abs(next.z) < 1e-12) {
            reach = std::numeric_limits<double>::infinity();
            continue;
        }
        const double t = (plane - f.eye.z) / next.z;
        const Vec there = plus(f.eye, times(next, t));
        reach = std::max({reach, std::abs(there.x - at.x), std::abs(there.y - at.y)});
    }
    const double margin = 1.5 * reach;
    const bool inside = std::abs(at.x) < f.half.x - margin && std::abs(at.y) < f.half.y - margin;
    if (face.levels.empty())
        return {light_edge(f, Vec{0, 0, strike.side}, toward_eye, at), inside};
    const double u = front ? at.x / f.pose.width + .5 : .5 - at.x / f.pose.width;
    const double v = at.y / f.pose.height + .5;
    const CardFaceLevel& full = face.levels.front();
    const double footprint = reach * std::max(full.width / f.pose.width, full.height / f.pose.height);
    std::size_t level = 0;
    for (double span = footprint; span >= 2 && level + 1 < face.levels.size(); span *= .5)
        ++level;
    const Texel t = sample(face.levels[level], u, v);
    const double relief = (*f.material).relief;
    const double sx = t.slope_x * relief, sy = t.slope_y * relief;
    const double fx = sx + t.facet_x, fy = sy + t.facet_y;
    // As seen from its own side, the back's `u` runs against the card's x.
    const Vec n = front ? unit(Vec{-sx, -sy, 1}) : unit(Vec{sx, -sy, -1});
    const Vec facet = front ? unit(Vec{-fx, -fy, 1}) : unit(Vec{fx, -fy, -1});
    return {light_face(f, t, n, toward_eye, facet), inside};
}

// The page's shadow at the picture's point (x, y), points: 0..1 of its strength.
double shadow_at(const Frame& f, double x, double y) {
    if (f.shadow_sides == 0)
        return 0;
    const double gx = f.pose.centre_x + (x - f.pose.centre_x) * f.page_scale;
    const double gy = f.pose.centre_y + (y - f.pose.centre_y) * f.page_scale;
    double outside = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < f.shadow_sides; ++i)
        outside = std::max(outside, gx * f.shadow_nx[i] + gy * f.shadow_ny[i] - f.shadow_off[i]);
    const double s = std::clamp(.5 - outside / (2 * f.shadow_blur), 0.0, 1.0);
    const double smooth = s * s * (3 - 2 * s);
    return smooth * f.shadow_strength;
}
// How far the picture's pixel centre (x, y), device pixels, lies outside the slab's outline.
double outside_outline(const Frame& f, double x, double y) {
    double outside = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < f.outline_sides; ++i)
        outside = std::max(outside, x * f.outline_nx[i] + y * f.outline_ny[i] - f.outline_off[i]);
    return outside;
}

// Four rays a pixel for the card's outline and edges, on a turned grid.
constexpr double kSubX[4] = {-.125, .375, .125, -.375};
constexpr double kSubY[4] = {-.375, -.125, .375, .125};
constexpr double kShadow[3] = {.16 * 255, .12 * 255, .06 * 255};  // the shadow's colour, sRGB levels

struct Band {
    const Frame* frame;
    int rows;  // a band's height
};
void draw_band(void* context, int index) noexcept {
    const Band& band = *static_cast<const Band*>(context);
    const Frame& f = *band.frame;
    const std::vector<double>& table = *f.encoding;
    const int y0 = f.box.y0 + index * band.rows, y1 = std::min(f.box.y1, y0 + band.rows);
    const double scale = f.picture.scale;
    for (int y = y0; y < y1; ++y) {
        std::byte* row = f.picture.pixels + static_cast<std::size_t>(y) * f.picture.stride;
        for (int x = f.box.x0; x < f.box.x1; ++x) {
            const double shade = shadow_at(f, (x + .5) / scale, (y + .5) / scale);
            double cover = 0;
            Lit sum{0, 0, 0};
            if (f.outline_sides > 0 && outside_outline(f, x + .5, y + .5) < 1) {
                const Vec d = plus(f.ray_origin_pixel, plus(times(f.ray_across, x), times(f.ray_down, y)));
                const Strike middle = strike_box(f.eye, d, f.half);
                Seen seen{{0, 0, 0}, false};
                if (middle.hit)
                    seen = look(f, d, middle);
                if (middle.hit && seen.inside) {
                    // Well inside a face: one ray.
                    cover = 1;
                    sum = seen.colour;
                } else {
                    // The outline or an edge: four rays, each that strikes lit, the light averaged.
                    int hits = 0;
                    for (int s = 0; s < 4; ++s) {
                        const Vec sub = plus(d, plus(times(f.ray_across, kSubX[s]), times(f.ray_down, kSubY[s])));
                        const Strike strike = strike_box(f.eye, sub, f.half);
                        if (!strike.hit)
                            continue;
                        const Seen lit = look(f, sub, strike);
                        sum.red += lit.colour.red;
                        sum.green += lit.colour.green;
                        sum.blue += lit.colour.blue;
                        ++hits;
                    }
                    cover = hits / 4.0;
                    if (hits > 0) {
                        sum.red /= hits;
                        sum.green /= hits;
                        sum.blue /= hits;
                    }
                }
            }
            double colour[3] = {0, 0, 0};
            if (cover > 0) {
                colour[0] = encode(table, sum.red);
                colour[1] = encode(table, sum.green);
                colour[2] = encode(table, sum.blue);
            }
            const double under = shade * (1 - cover);
            const double alpha = cover + under;
            std::byte* p = row + static_cast<std::size_t>(x) * 4;
            p[0] = static_cast<std::byte>(std::lround(std::clamp(colour[2] * cover + kShadow[2] * under, 0.0, 255.0)));
            p[1] = static_cast<std::byte>(std::lround(std::clamp(colour[1] * cover + kShadow[1] * under, 0.0, 255.0)));
            p[2] = static_cast<std::byte>(std::lround(std::clamp(colour[0] * cover + kShadow[0] * under, 0.0, 255.0)));
            p[3] = static_cast<std::byte>(std::lround(std::clamp(alpha * 255, 0.0, 255.0)));
        }
    }
}

// The slab's eight corners as the eye sees them on the picture's plane, points.
void slab_corners(const CardPose& pose, const Matrix& r, double xs[8], double ys[8]) {
    for (int i = 0; i < 8; ++i) {
        const Vec corner = onto_screen(r, Vec{(i & 1 ? .5 : -.5) * pose.width, (i & 2 ? .5 : -.5) * pose.height,
                                              (i & 4 ? .5 : -.5) * pose.thickness});
        const double nearer = pose.eye / std::max(1.0, pose.eye - corner.z);
        xs[i] = pose.centre_x + corner.x * nearer;
        ys[i] = pose.centre_y + corner.y * nearer;
    }
}
// The card's corners on the page beneath it, cast by the lamp: points in the picture. A corner
// tipped down to the page (or, turned far enough, through it) casts from the page itself, and
// one tipped far up is held no higher than a third of the card's width above its middle.
void shadow_corners(const CardPose& pose, const Matrix& r, double xs[4], double ys[4]) {
    const Vec lamp = unit(kLamp);
    const double cx[4] = {-.5, .5, .5, -.5}, cy[4] = {-.5, -.5, .5, .5};
    for (int i = 0; i < 4; ++i) {
        const Vec corner = onto_screen(r, Vec{cx[i] * pose.width, cy[i] * pose.height, 0});
        const double height = std::clamp(pose.lift + corner.z, 1.5, pose.lift + pose.width / 3);
        xs[i] = pose.centre_x + corner.x - lamp.x * height / lamp.z;
        ys[i] = pose.centre_y + corner.y - lamp.y * height / lamp.z;
    }
}
double shadow_blur(const CardPose& pose) {
    const double blur = 1.5 + .45 * pose.lift;
    return blur;
}

}  // namespace

Turn turn_about(double x, double y, double z, double angle) {
    const double length = std::sqrt(x * x + y * y + z * z);
    if (length <= 0)
        return {};
    const double s = std::sin(angle / 2) / length;
    const Turn turn{std::cos(angle / 2), x * s, y * s, z * s};
    return turn;
}

Turn turn_then(const Turn& first, const Turn& then) {
    const Turn& a = then;
    const Turn& b = first;
    const Turn result{a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
    const double length = std::sqrt(result.w * result.w + result.x * result.x + result.y * result.y + result.z * result.z);
    if (length <= 0)
        return {};
    const Turn normal{result.w / length, result.x / length, result.y / length, result.z / length};
    return normal;
}

Turn turn_toward(const Turn& from, const Turn& to, double amount) {
    double cosine = from.w * to.w + from.x * to.x + from.y * to.y + from.z * to.z;
    double sign = 1;
    if (cosine < 0) {
        cosine = -cosine;
        sign = -1;
    }
    const double a = std::clamp(amount, 0.0, 1.0);
    double keep = 1 - a, take = a;
    if (cosine < .9995) {
        const double angle = std::acos(std::clamp(cosine, -1.0, 1.0));
        const double sine = std::sin(angle);
        keep = std::sin((1 - a) * angle) / sine;
        take = std::sin(a * angle) / sine;
    }
    take *= sign;
    const Turn mixed{from.w * keep + to.w * take, from.x * keep + to.x * take, from.y * keep + to.y * take,
                     from.z * keep + to.z * take};
    const double length = std::sqrt(mixed.w * mixed.w + mixed.x * mixed.x + mixed.y * mixed.y + mixed.z * mixed.z);
    if (length <= 0)
        return to;
    const Turn normal{mixed.w / length, mixed.x / length, mixed.y / length, mixed.z / length};
    return normal;
}

double turn_apart(const Turn& a, const Turn& b) {
    const double cosine = std::abs(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z);
    const double angle = 2 * std::acos(std::clamp(cosine, 0.0, 1.0));
    return angle;
}

bool front_toward_eye(const Turn& turn) {
    const Matrix r = matrix_of(turn);
    const bool toward = r.m[2][2] > 0;
    return toward;
}

CardFace card_face_levels(const CardFaceMap& full) {
    CardFace face;
    if (full.width <= 0 || full.height <= 0)
        return face;
    face.levels.push_back(packed(full));
    CardFaceMap above = full;
    while (above.width >= 16 && above.height >= 16) {
        CardFaceMap below;
        below.width = above.width / 2;
        below.height = above.height / 2;
        below.texels.resize(static_cast<std::size_t>(below.width) * below.height);
        const std::size_t w = static_cast<std::size_t>(above.width);
        for (int y = 0; y < below.height; ++y)
            for (int x = 0; x < below.width; ++x) {
                const std::size_t i = static_cast<std::size_t>(2 * y) * w + static_cast<std::size_t>(2 * x);
                const CardTexel top = blend(above.texels[i], above.texels[i + 1], .5F);
                const CardTexel bottom = blend(above.texels[i + w], above.texels[i + w + 1], .5F);
                below.texels[static_cast<std::size_t>(y) * below.width + x] = blend(top, bottom, .5F);
            }
        face.levels.push_back(packed(below));
        above = std::move(below);
    }
    return face;
}

void scatter_card_facets(CardFaceMap& map, double grain, std::uint32_t seed) {
    if (map.width <= 0 || map.height <= 0 || grain < 1)
        return;
    for (int y = 0; y < map.height; ++y)
        for (int x = 0; x < map.width; ++x) {
            const double px = x + .5, py = y + .5;
            const int cx = static_cast<int>(std::floor(px / grain)), cy = static_cast<int>(std::floor(py / grain));
            CardTexel& t = map.texels[static_cast<std::size_t>(y) * map.width + x];
            t.facet = 0;
            for (int ny = cy - 1; ny <= cy + 1; ++ny)
                for (int nx = cx - 1; nx <= cx + 1; ++nx) {
                    const std::uint32_t key =
                        mix(static_cast<std::uint32_t>(nx) * 0x9e3779b1U ^ mix(static_cast<std::uint32_t>(ny) * 0x85ebca77U ^ seed));
                    if (unit_of(key) > .45)
                        continue;
                    const double fx = (nx + .2 + .6 * unit_of(mix(key ^ 1U))) * grain;
                    const double fy = (ny + .2 + .6 * unit_of(mix(key ^ 2U))) * grain;
                    const double radius = grain * (.28 + .22 * unit_of(mix(key ^ 3U)));
                    const double d = std::hypot(px - fx, py - fy) / radius;
                    if (d >= 1)
                        continue;
                    const double weight = (.5 + .5 * unit_of(mix(key ^ 4U))) * std::pow(1 - d * d, 1.5);
                    if (weight <= t.facet)
                        continue;
                    t.facet = static_cast<float>(weight);
                    // tipped a few degrees at most: dark as the card lies, a sweep of them flashing
                    // as it is turned through the lamp's reflection
                    t.facet_x = static_cast<float>((unit_of(mix(key ^ 5U)) - .5) * .3);
                    t.facet_y = static_cast<float>((unit_of(mix(key ^ 6U)) - .5) * .3);
                }
        }
}

bool box_empty(const PixelBox& box) {
    const bool empty = box.x1 <= box.x0 || box.y1 <= box.y0;
    return empty;
}

PixelBox box_union(const PixelBox& a, const PixelBox& b) {
    if (box_empty(a))
        return b;
    if (box_empty(b))
        return a;
    const PixelBox united{std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)};
    return united;
}

PixelBox held_card_box(const CardPose& pose, const CardPicture& picture) {
    const Matrix r = matrix_of(pose.turn);
    double left = std::numeric_limits<double>::infinity(), top = left;
    double right = -left, bottom = -left;
    double xs[8] = {}, ys[8] = {};
    slab_corners(pose, r, xs, ys);
    for (int i = 0; i < 8; ++i) {
        left = std::min(left, xs[i]);
        right = std::max(right, xs[i]);
        top = std::min(top, ys[i]);
        bottom = std::max(bottom, ys[i]);
    }
    if (pose.shadow > 0) {
        double sx[4] = {}, sy[4] = {};
        shadow_corners(pose, r, sx, sy);
        const double blur = shadow_blur(pose), farther = pose.eye / (pose.eye + pose.lift);
        for (int i = 0; i < 4; ++i) {
            const double x = pose.centre_x + (sx[i] - pose.centre_x) * farther;
            const double y = pose.centre_y + (sy[i] - pose.centre_y) * farther;
            left = std::min(left, x - blur);
            right = std::max(right, x + blur);
            top = std::min(top, y - blur);
            bottom = std::max(bottom, y + blur);
        }
    }
    const double s = picture.scale;
    const PixelBox box{std::clamp(static_cast<int>(std::floor(left * s)) - 2, 0, picture.width),
                       std::clamp(static_cast<int>(std::floor(top * s)) - 2, 0, picture.height),
                       std::clamp(static_cast<int>(std::ceil(right * s)) + 2, 0, picture.width),
                       std::clamp(static_cast<int>(std::ceil(bottom * s)) + 2, 0, picture.height)};
    return box;
}

void draw_held_card(const CardFace& front, const CardFace& back, const CardMaterial& material, const CardPose& pose,
                    const CardPicture& picture, PixelBox box) {
    box.x0 = std::clamp(box.x0, 0, picture.width);
    box.x1 = std::clamp(box.x1, box.x0, picture.width);
    box.y0 = std::clamp(box.y0, 0, picture.height);
    box.y1 = std::clamp(box.y1, box.y0, picture.height);
    if (box_empty(box) || picture.pixels == nullptr || picture.scale <= 0)
        return;
    const Matrix r = matrix_of(pose.turn);
    Frame f{};
    f.front = &front;
    f.back = &back;
    f.material = &material;
    f.encoding = &encode_table();
    f.pose = pose;
    f.picture = picture;
    f.box = box;
    f.half = {pose.width / 2, pose.height / 2, pose.thickness / 2};
    f.eye = onto_card(r, Vec{0, 0, pose.eye});
    const double step = 1 / picture.scale;
    f.ray_origin_pixel = onto_card(r, Vec{.5 * step - pose.centre_x, .5 * step - pose.centre_y, -pose.eye});
    f.ray_across = onto_card(r, Vec{step, 0, 0});
    f.ray_down = onto_card(r, Vec{0, step, 0});
    f.lamp = onto_card(r, unit(kLamp));
    f.window = onto_card(r, unit(kWindow));
    f.up = onto_card(r, Vec{0, -1, 0});
    // The slab's outline in device pixels: nothing outside it is traced.
    double xs[8] = {}, ys[8] = {};
    slab_corners(pose, r, xs, ys);
    for (int i = 0; i < 8; ++i) {
        xs[i] *= picture.scale;
        ys[i] *= picture.scale;
    }
    f.outline_sides = outline_of(xs, ys, 8, f.outline_nx, f.outline_ny, f.outline_off);
    // The shadow's outline on the page: the hull of the corners' shadows (a corner held to the
    // page can fold the four out of order); seen edge-on from the lamp, the card casts none.
    f.shadow_sides = 0;
    if (pose.shadow > 0) {
        double sx[4] = {}, sy[4] = {};
        shadow_corners(pose, r, sx, sy);
        f.shadow_sides = outline_of(sx, sy, 4, f.shadow_nx, f.shadow_ny, f.shadow_off);
    }
    f.shadow_blur = shadow_blur(pose);
    f.shadow_strength = .26 * std::clamp(pose.shadow, 0.0, 1.0) * (1 - .3 * std::min(1.0, pose.lift / 40));
    f.page_scale = (pose.lift + pose.eye) / pose.eye;
    for (int y = box.y0; y < box.y1; ++y)
        std::fill(picture.pixels + static_cast<std::size_t>(y) * picture.stride + static_cast<std::size_t>(box.x0) * 4,
                  picture.pixels + static_cast<std::size_t>(y) * picture.stride + static_cast<std::size_t>(box.x1) * 4,
                  std::byte{0});
    Band band{&f, 16};
    const int bands = (box.y1 - box.y0 + band.rows - 1) / band.rows;
    run_on_every_core(bands, draw_band, &band);
}

CardHit held_card_hit(const CardPose& pose, double x, double y) {
    const Matrix r = matrix_of(pose.turn);
    const Vec eye = onto_card(r, Vec{0, 0, pose.eye});
    const Vec d = onto_card(r, Vec{x - pose.centre_x, y - pose.centre_y, -pose.eye});
    const Strike strike = strike_box(eye, d, Vec{pose.width / 2, pose.height / 2, pose.thickness / 2});
    CardHit hit;
    if (!strike.hit)
        return hit;
    hit.hit = true;
    const Vec at = plus(eye, times(d, strike.t));
    hit.front = strike.axis == 2 && strike.side > 0;
    const bool back = strike.axis == 2 && strike.side < 0;
    hit.u = back ? .5 - at.x / pose.width : at.x / pose.width + .5;
    hit.v = at.y / pose.height + .5;
    return hit;
}

}  // namespace games
