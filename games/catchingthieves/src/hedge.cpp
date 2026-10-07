#include "hedge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>

namespace ct {

namespace {

constexpr double kInset = .06;      // the hedge stands this far in from the soil and the lawn
constexpr double kHeight = .52;     // to the clipped top
constexpr double kShoulder = .10;   // radius of the top's rounded edge
constexpr double kBatter = .035;    // how much wider at the foot than at the shoulder
constexpr int kTexSide = 128;       // the leaf textures, texels each way
constexpr double kTexPerUnit = 64;  // texels per world unit
constexpr int kShadowPerUnit = 16;  // the ground shade's texels per world unit

// ------------------------------------------------------------------ noise
std::uint32_t mix_bits(std::uint32_t x) {
    x ^= x >> 16U;
    x *= 0x7FEB352DU;
    x ^= x >> 15U;
    x *= 0x846CA68BU;
    x ^= x >> 16U;
    return x;
}
double lattice(int x, int y, int z, int seed) {
    const std::uint32_t h = mix_bits(static_cast<std::uint32_t>(x) * 73856093U ^ static_cast<std::uint32_t>(y) * 19349663U ^
                                     static_cast<std::uint32_t>(z) * 83492791U ^ static_cast<std::uint32_t>(seed) * 2654435761U);
    return (h & 0xFFFFFFU) / 16777215.0;
}
double smooth01(double t) { return t * t * (3 - 2 * t); }
double smooth_between(double a, double b, double x) { return smooth01(std::clamp((x - a) / (b - a), 0.0, 1.0)); }
// value noise in 0..1; `period` > 0 makes it wrap in x and y
double noise3(double x, double y, double z, int seed, int period = 0) {
    const double fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy), iz = static_cast<int>(fz);
    const double u = smooth01(x - fx), v = smooth01(y - fy), w = smooth01(z - fz);
    double c[2][2][2];
    for (int k = 0; k < 2; ++k)
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) {
                int a = ix + i, b = iy + j;
                if (period > 0) { a = ((a % period) + period) % period; b = ((b % period) + period) % period; }
                c[k][j][i] = lattice(a, b, iz + k, seed);
            }
    double layer[2];
    for (int k = 0; k < 2; ++k) {
        const double top = c[k][0][0] + (c[k][0][1] - c[k][0][0]) * u, bottom = c[k][1][0] + (c[k][1][1] - c[k][1][0]) * u;
        layer[k] = top + (bottom - top) * v;
    }
    return layer[0] + (layer[1] - layer[0]) * w;
}
double signed_noise(double x, double y, double z, int seed) { return noise3(x, y, z, seed) * 2 - 1; }

// ------------------------------------------------------------------ leaf textures
// A little depth buffer of leaves: each texel keeps the nearest leaf's colour.
struct Splat {
    int n = kTexSide;
    std::vector<float> z, r, g, b;
    Splat() : z(static_cast<size_t>(kTexSide) * kTexSide, -1e9f), r(z.size(), 0.f), g(z.size(), 0.f), b(z.size(), 0.f) {}
    void put(int x, int y, float depth, float red, float green, float blue) {
        x = ((x % n) + n) % n;
        y = ((y % n) + n) % n;
        const size_t i = static_cast<size_t>(y) * n + x;
        if (depth <= z[i]) return;
        z[i] = depth; r[i] = red; g[i] = green; b[i] = blue;
    }
};

struct Rand {
    std::uint32_t state;
    double next() {
        state = state * 1664525U + 1013904223U;
        return static_cast<double>(mix_bits(state) & 0xFFFFFFU) / 16777216.0;
    }
};

// One small leaf: an ellipse, domed, darker at its rim, lighter where it faces the sun (upper left).
void leaf(Splat& s, double cx, double cy, double angle, double len, double wid, double depth, double bright, double warm) {
    const double co = std::cos(angle), si = std::sin(angle);
    const int x0 = static_cast<int>(std::floor(cx - len - 1)), x1 = static_cast<int>(std::ceil(cx + len + 1));
    const int y0 = static_cast<int>(std::floor(cy - len - 1)), y1 = static_cast<int>(std::ceil(cy + len + 1));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double dx = x + .5 - cx, dy = y + .5 - cy;
            const double u = (dx * co + dy * si) / len, v = (-dx * si + dy * co) / wid;
            const double rr = u * u + v * v;
            if (rr > 1) continue;
            // the leaf's own slope: the side towards the sun is brighter
            const double facing = std::clamp(.5 - .35 * (dx * .7 + dy * .7) / std::max(1.0, len), 0.0, 1.0);
            const double k = bright * (.80 + .22 * facing) * (1 - .18 * rr);
            s.put(x, y, static_cast<float>(depth + .04 * (1 - rr)), static_cast<float>(k * (1 + .10 * warm)), static_cast<float>(k * (1 + .04 * warm)),
                  static_cast<float>(k * (.90 - .10 * warm)));
        }
}

// A twig: a thin line of bark, mostly upward, deep in the bush.
void twig(Splat& s, Rand& rnd, double x, double y, double angle, double length, double depth) {
    const int steps = static_cast<int>(length * 2);
    for (int i = 0; i < steps; ++i) {
        x += std::cos(angle) * .5;
        y += std::sin(angle) * .5;
        angle += (rnd.next() - .5) * .18;
        const double k = .30 + .06 * rnd.next();
        s.put(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), static_cast<float>(depth), static_cast<float>(k * 1.45),
              static_cast<float>(k * 1.05), static_cast<float>(k * .80));
    }
}

// The leaf mass. `surface` 0 for a side .. 1 for the clipped top. Sprigs of small leaves bunch into rounded
// clumps; each clump is lit like a little dome (bright where it faces the sun, darker underneath), the
// hollows between clumps are dark, and the deepest gaps show twigs. (lx, ly): towards the sun, in texels.
Tex leaf_texture(double surface, std::uint32_t seed, double lx, double ly) {
    const int n = kTexSide;
    const size_t count = static_cast<size_t>(n) * n;
    Rand rnd{seed};
    std::vector<float> height(count, -6.f);
    struct Clump {
        double x, y, r, z;
    };
    std::vector<Clump> clumps;
    const int clump_count = static_cast<int>(300 + 140 * surface);
    for (int c = 0; c < clump_count; ++c) {
        Clump k;
        k.x = rnd.next() * n;
        k.y = rnd.next() * n;
        k.r = (3.0 + rnd.next() * (3.6 - 1.6 * surface));
        const double q = rnd.next();
        k.z = -q * q * (1.8 - 1.2 * surface);  // clipped: the clumps stand at nearly one height
        clumps.push_back(k);
        const int r = static_cast<int>(std::ceil(k.r));
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x) {
                const double d2 = (x * x + y * y) / (k.r * k.r);
                if (d2 > 1) continue;
                const int px = ((static_cast<int>(k.x) + x) % n + n) % n, py = ((static_cast<int>(k.y) + y) % n + n) % n;
                const float hz = static_cast<float>(k.z + k.r * .55 * std::sqrt(1 - d2));
                float& slot = height[static_cast<size_t>(py) * n + px];
                slot = std::max(slot, hz);
            }
    }
    // light on the clumps, and how sunk each place is below the mean of its neighbourhood
    std::vector<float> across(count), mean(count);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float sum = 0;
            for (int j = -5; j <= 5; ++j) sum += height[static_cast<size_t>(y) * n + static_cast<size_t>((x + j + n) % n)];
            across[static_cast<size_t>(y) * n + x] = sum / 11;
        }
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float sum = 0;
            for (int j = -5; j <= 5; ++j) sum += across[static_cast<size_t>((y + j + n) % n) * n + x];
            mean[static_cast<size_t>(y) * n + x] = sum / 11;
        }
    std::vector<float> lit(count), sunk(count);
    const double ll = std::sqrt(lx * lx + ly * ly + 1.0);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const size_t i = static_cast<size_t>(y) * n + x;
            const double hl = height[static_cast<size_t>(y) * n + (x + n - 1) % n], hr = height[static_cast<size_t>(y) * n + (x + 1) % n];
            const double hu = height[static_cast<size_t>((y + n - 1) % n) * n + x], hd = height[static_cast<size_t>((y + 1) % n) * n + x];
            const double gx = (hr - hl) * .5, gy = (hd - hu) * .5;
            const double nl = std::sqrt(gx * gx + gy * gy + 1.0);
            lit[i] = static_cast<float>(std::max(0.0, (-gx * lx - gy * ly + 1.0) / (nl * ll)));
            sunk[i] = static_cast<float>(std::clamp(.55 + (height[i] - mean[i]) * .22, 0.0, 1.0));
        }
    // the leaves: small, many, each a little lighter or darker than its neighbour
    Splat s;
    for (const Clump& k : clumps) {
        const int leaves = static_cast<int>(k.r * k.r * (1.05 + .3 * surface));
        for (int l = 0; l < leaves; ++l) {
            const double a = rnd.next() * 6.2831853, d = k.r * .95 * std::sqrt(rnd.next());
            const double x = k.x + std::cos(a) * d, y = k.y + std::sin(a) * d;
            const double len = 1.15 + rnd.next() * .9, wid = len * (.5 + .2 * rnd.next());
            const double depth = k.z + k.r * .55 * std::sqrt(std::max(0.0, 1 - d * d / (k.r * k.r))) + .3;
            const double warm = rnd.next() < .25 ? 1 : rnd.next() < .3 ? -1 : 0;
            leaf(s, x, y, rnd.next() * 6.2831853, len, wid, depth, .84 + .30 * rnd.next(), warm);
        }
    }
    // twigs in the deep gaps
    Splat bark;
    const int twigs = static_cast<int>(26 - 18 * surface);
    for (int i = 0; i < twigs; ++i)
        twig(bark, rnd, rnd.next() * n, rnd.next() * n, -1.5708 + (rnd.next() - .5) * 1.6, 8 + rnd.next() * 20, 0);
    std::vector<float> r(count), g(count), b(count);
    double sum = 0;
    for (size_t i = 0; i < count; ++i) {
        const double open = (.60 + .40 * lit[i]) * (.62 + .38 * sunk[i]);
        if (s.z[i] > -1e8f) {
            r[i] = static_cast<float>(open * s.r[i]);
            g[i] = static_cast<float>(open * s.g[i]);
            b[i] = static_cast<float>(open * s.b[i]);
        } else if (bark.z[i] > -1e8f) {
            r[i] = bark.r[i] * .55f; g[i] = bark.g[i] * .55f; b[i] = bark.b[i] * .55f;
        } else {
            const float v = static_cast<float>(.22 + .10 * sunk[i]);
            r[i] = v * .85f; g[i] = v; b[i] = v * 1.05f;
        }
        sum += g[i];
    }
    const double gain = .70 / std::max(.05, sum / static_cast<double>(count));
    Tex t;
    t.make(n, n);
    for (size_t i = 0; i < count; ++i) {
        const std::uint32_t cr = static_cast<std::uint32_t>(std::clamp(r[i] * gain * 255.0, 0.0, 255.0));
        const std::uint32_t cg = static_cast<std::uint32_t>(std::clamp(g[i] * gain * 255.0, 0.0, 255.0));
        const std::uint32_t cb = static_cast<std::uint32_t>(std::clamp(b[i] * gain * 255.0, 0.0, 255.0));
        t.px[i] = 0xFF000000U | cr << 16U | cg << 8U | cb;
    }
    t.build_mips();
    return t;
}

// Snow lying on the clipped top, as a cut-out laid over the leaves: soft lumps lit from the front left and blue
// in their shade, thinning into gaps where the leaves show through.
Tex snow_texture() {
    const int n = kTexSide;
    std::vector<double> lump(static_cast<size_t>(n) * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            lump[static_cast<size_t>(y) * n + x] = .55 * noise3(x / 9.0, y / 9.0, 0, 41, 15) + .30 * noise3(x / 4.6, y / 4.6, 0, 43, 28) +
                                                   .15 * noise3(x / 2.0, y / 2.0, 0, 47, 64);
    Tex t;
    t.make(n, n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const double h = lump[static_cast<size_t>(y) * n + x];
            const double gx = lump[static_cast<size_t>(y) * n + (x + 1) % n] - lump[static_cast<size_t>(y) * n + (x + n - 1) % n];
            const double gy = lump[static_cast<size_t>((y + 1) % n) * n + x] - lump[static_cast<size_t>((y + n - 1) % n) * n + x];
            // the sun is down and to the left in the top's texture
            const double lit = std::clamp(.62 + 2.6 * (gx * .6 - gy * .7), 0.0, 1.0);
            const double r = .74 + .26 * lit, g = .80 + .20 * lit, b = .92 + .08 * lit;
            const std::uint32_t alpha = h > .31 ? 255U : 0U;
            t.px[static_cast<size_t>(y) * n + x] = alpha << 24U | static_cast<std::uint32_t>(r * 255) << 16U | static_cast<std::uint32_t>(g * 255) << 8U |
                                                   static_cast<std::uint32_t>(b * 255);
        }
    t.build_mips();
    return t;
}

// The fringe: leaf tips standing up out of the clipped edge, cut out against whatever lies behind.
Tex fringe_texture() {
    Splat s;
    Rand rnd{0xF21Du};
    for (int i = 0; i < 260; ++i) {
        const double x = rnd.next() * kTexSide, len = 1.3 + rnd.next() * 1.4;
        const double y = 6 + rnd.next() * 10;  // tips standing at different heights in the 128 x 16 band
        leaf(s, x, y, -1.5708 + (rnd.next() - .5) * 1.9, len, len * .55, 1, .86 + .3 * rnd.next(), rnd.next() < .3 ? 1 : 0);
    }
    Tex t;
    t.make(kTexSide, 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < kTexSide; ++x) {
            const size_t i = static_cast<size_t>(y) * kTexSide + x;
            std::uint32_t c = 0;
            if (s.z[i] > -1e8f) {
                const std::uint32_t r = static_cast<std::uint32_t>(std::clamp(s.r[i] * 200.0, 0.0, 255.0));
                const std::uint32_t g = static_cast<std::uint32_t>(std::clamp(s.g[i] * 200.0, 0.0, 255.0));
                const std::uint32_t b = static_cast<std::uint32_t>(std::clamp(s.b[i] * 200.0, 0.0, 255.0));
                c = 0xFF000000U | r << 16U | g << 8U | b;
            }
            t.px[i] = c;
        }
    // no mips: averaged, the tips would fall below the cut-out's threshold and vanish
    return t;
}

struct HedgeTextures {
    Tex top, side, snow, fringe;
    // the sun comes from the front left: down and left on the top (rows run towards the viewer), up and left on a side
    HedgeTextures() : top(leaf_texture(1, 0xB0C5u, -.6, .7)), side(leaf_texture(0, 0x7E77u, -.6, -.8)), snow(snow_texture()), fringe(fringe_texture()) {}
};
const HedgeTextures& textures() {
    static HedgeTextures t;
    return t;
}

// ------------------------------------------------------------------ the shape
// Where the hedge is: inside a hedge square and not within the inset of any square that is not hedge.
struct Shape {
    const Level& lv;
    explicit Shape(const Level& level) : lv(level) {}
    bool wall(int gx, int gy) const { return lv.in(gx, gy) && lv.tiles[static_cast<size_t>(lv.idx(gx, gy))] == Tile::wall; }
    double cx(int gx) const { return gx - (lv.w - 1) / 2.0; }
    double cy(int gy) const { return (lv.h - 1) / 2.0 - gy; }
    int gx_of(double x) const { return static_cast<int>(std::floor(x + (lv.w - 1) / 2.0 + .5)); }
    int gy_of(double y) const { return static_cast<int>(std::floor((lv.h - 1) / 2.0 - y + .5)); }
    bool inside(double x, double y) const {
        const int gx = gx_of(x), gy = gy_of(y);
        if (!wall(gx, gy)) return false;
        const double reach = .5 + kInset;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if ((dx == 0 && dy == 0) || wall(gx + dx, gy + dy)) continue;
                if (std::fabs(x - cx(gx + dx)) < reach && std::fabs(y - cy(gy + dy)) < reach) return false;
            }
        return true;
    }
    // distance in from the hedge's outline
    double depth(double x, double y) const {
        const int gx = gx_of(x), gy = gy_of(y);
        const double reach = .5 + kInset;
        double best = 9;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                if (wall(gx + dx, gy + dy)) continue;
                const double ox = std::max(0.0, std::fabs(x - cx(gx + dx)) - reach), oy = std::max(0.0, std::fabs(y - cy(gy + dy)) - reach);
                best = std::min(best, std::sqrt(ox * ox + oy * oy));
            }
        return best;
    }
    // which way is out at a point of the outline: unit steps in x and y (both at a corner), zero inside
    void outward(double x, double y, double& ox, double& oy) const {
        const double e = .02;
        double sx = 0, sy = 0;
        for (int j = -1; j <= 1; j += 2)
            for (int i = -1; i <= 1; i += 2)
                if (!inside(x + i * e, y + j * e)) { sx += i; sy += j; }
        ox = sx > 0 ? 1 : sx < 0 ? -1 : 0;
        oy = sy > 0 ? 1 : sy < 0 ? -1 : 0;
    }
    double top(double x, double y, double d) const {
        // clipped flat, the line wandering a little along the run
        const double h = kHeight + .028 * signed_noise(x * .55, y * .55, 1.5, 7) + .010 * signed_noise(x * 2.1, y * 2.1, 3.5, 8);
        const double r = kShoulder;
        if (d >= r) return h;
        const double q = r - d;
        return h - (r - std::sqrt(std::max(0.0, r * r - q * q)));
    }
};

// Displaced position of a shell point: the sides lean in towards the top, and the leaf mass bulges and
// hollows where the clipping missed a sprig or a branch sagged.
struct Placed {
    V3 p;
    double hollow = 0;  // -1 a hollow .. 1 a bulge
};
Placed place(V3 base, double ox, double oy, double shoulder_z) {
    const double up = std::clamp(base.z / std::max(.05, shoulder_z), 0.0, 1.0);
    const double bulge = .65 * signed_noise(base.x * 3.1, base.y * 3.1, base.z * 3.1, 21) + .35 * signed_noise(base.x * 7.3, base.y * 7.3, base.z * 7.3, 22);
    const double batter = kBatter * std::pow(1 - up, 1.3);
    const double out = batter + .024 * bulge;
    const double lift = (.016 * signed_noise(base.x * 2.7, base.y * 2.7, 0, 23) + .008 * signed_noise(base.x * 6.1, base.y * 6.1, 0, 24)) * up;
    Placed r;
    r.p = {base.x + ox * out, base.y + oy * out, base.z + lift};
    r.hollow = bulge;
    return r;
}

std::uint64_t key_of(V3 base) {
    const std::uint64_t qx = static_cast<std::uint64_t>(static_cast<std::int64_t>(std::lround(base.x * 4096)) + (1 << 20));
    const std::uint64_t qy = static_cast<std::uint64_t>(static_cast<std::int64_t>(std::lround(base.y * 4096)) + (1 << 20));
    const std::uint64_t qz = static_cast<std::uint64_t>(static_cast<std::int64_t>(std::lround(base.z * 4096)) + (1 << 12));
    return qx << 42U | qy << 21U | qz;
}

struct Builder {
    const Shape& shape;
    std::map<std::uint64_t, V3> normals;
    std::vector<HedgeCache::Corner>* out = nullptr;
    std::vector<std::uint64_t> keys_top, keys_side;
    explicit Builder(const Shape& s) : shape(s) {}

    HedgeCache::Corner corner(V3 base, bool side, double along_x) {
        double ox = 0, oy = 0;
        shape.outward(base.x, base.y, ox, oy);
        const double shoulder = shape.top(base.x, base.y, 0);
        const Placed q = place(base, ox, oy, shoulder);
        HedgeCache::Corner c;
        c.p = q.p;
        if (side) {
            c.s = (along_x > 0 ? base.x : base.y) * kTexPerUnit / kTexSide;
            c.t = (kHeight - base.z) * kTexPerUnit / kTexSide;
        } else {
            c.s = base.x * kTexPerUnit / kTexSide;
            c.t = -base.y * kTexPerUnit / kTexSide;
        }
        c.foot = static_cast<float>(1 - smooth_between(.0, .24, base.z));
        // dark at the foot where the stems are bare and little light gets in; hollows darker, bulges lighter
        const double foot = 1 - .62 * c.foot;
        c.shade = static_cast<float>(std::clamp(foot * (1 + .16 * q.hollow), 0.0, 1.2));
        c.tone = static_cast<float>(.75 * signed_noise(base.x * .42, base.y * .42, 5.5, 31) + .25 * signed_noise(base.x * 1.7, base.y * 1.7, 9.5, 32));
        return c;
    }
    void triangle(std::vector<HedgeCache::Corner>& list, std::vector<std::uint64_t>& keys, const HedgeCache::Corner& a, V3 ba,
                  const HedgeCache::Corner& b, V3 bb, const HedgeCache::Corner& c, V3 bc) {
        const V3 n = cross(b.p - a.p, c.p - a.p);
        const std::uint64_t ka = key_of(ba), kb = key_of(bb), kc = key_of(bc);
        normals[ka] = normals[ka] + n;
        normals[kb] = normals[kb] + n;
        normals[kc] = normals[kc] + n;
        list.push_back(a); list.push_back(b); list.push_back(c);
        keys.push_back(ka); keys.push_back(kb); keys.push_back(kc);
    }
    // a quad given in counter-clockwise order seen from outside
    void quad(std::vector<HedgeCache::Corner>& list, std::vector<std::uint64_t>& keys, V3 b0, V3 b1, V3 b2, V3 b3, bool side, double along_x) {
        const HedgeCache::Corner c0 = corner(b0, side, along_x), c1 = corner(b1, side, along_x), c2 = corner(b2, side, along_x), c3 = corner(b3, side, along_x);
        triangle(list, keys, c0, b0, c1, b1, c2, b2);
        triangle(list, keys, c0, b0, c2, b2, c3, b3);
    }
};

void build_shell(HedgeCache& cache, const Level& lv) {
    const Shape shape(lv);
    Builder b(shape);
    cache.top.clear();
    cache.side.clear();
    cache.fringe.clear();
    const double lines[9] = {-.5, -.5 + kInset, -.39, -.30, 0, .30, .39, .5 - kInset, .5};
    const double rows[5] = {0, .13, .40, .72, 1};
    for (int gy = 0; gy < lv.h; ++gy)
        for (int gx = 0; gx < lv.w; ++gx) {
            if (!shape.wall(gx, gy)) continue;
            const double cx = shape.cx(gx), cy = shape.cy(gy);
            for (int j = 0; j < 8; ++j)
                for (int i = 0; i < 8; ++i) {
                    const double x0 = cx + lines[i], x1 = cx + lines[i + 1], y0 = cy + lines[j], y1 = cy + lines[j + 1];
                    if (!shape.inside((x0 + x1) / 2, (y0 + y1) / 2)) continue;
                    const V3 p00{x0, y0, shape.top(x0, y0, shape.depth(x0, y0))}, p10{x1, y0, shape.top(x1, y0, shape.depth(x1, y0))};
                    const V3 p11{x1, y1, shape.top(x1, y1, shape.depth(x1, y1))}, p01{x0, y1, shape.top(x0, y1, shape.depth(x0, y1))};
                    b.quad(cache.top, b.keys_top, p00, p10, p11, p01, false, 0);
                    // a side wherever the next piece across an edge is not hedge
                    const double mx = (x0 + x1) / 2, my = (y0 + y1) / 2, e = .01;
                    const V3 ends[4][2] = {{p00, p10}, {p10, p11}, {p11, p01}, {p01, p00}};
                    const double probes[4][2] = {{mx, y0 - e}, {x1 + e, my}, {mx, y1 + e}, {x0 - e, my}};
                    for (int k = 0; k < 4; ++k) {
                        if (shape.inside(probes[k][0], probes[k][1])) continue;
                        // ends[k] runs counter-clockwise round the top, so seen from outside it runs left to right
                        const V3 a = ends[k][0], c = ends[k][1];
                        const double along_x = k == 0 || k == 2 ? 1 : 0;
                        for (int r = 0; r < 4; ++r) {
                            const V3 a0{a.x, a.y, a.z * rows[r]}, c0{c.x, c.y, c.z * rows[r]};
                            const V3 a1{a.x, a.y, a.z * rows[r + 1]}, c1{c.x, c.y, c.z * rows[r + 1]};
                            b.quad(cache.side, b.keys_side, a0, c0, c1, a1, true, along_x);
                        }
                        // a fringe of sprigs standing on the shoulder, against the sky
                        double ox = 0, oy = 0;
                        shape.outward((a.x + c.x) / 2, (a.y + c.y) / 2, ox, oy);
                        const HedgeCache::Corner ta = b.corner(a, true, along_x), tc = b.corner(c, true, along_x);
                        HedgeCache::Corner f[4] = {ta, tc, tc, ta};
                        f[0].p = ta.p + V3{-ox * .03, -oy * .03, .05};
                        f[1].p = tc.p + V3{-ox * .03, -oy * .03, .05};
                        f[2].p = tc.p + V3{ox * .015, oy * .015, .16};
                        f[3].p = ta.p + V3{ox * .015, oy * .015, .16};
                        for (int q = 0; q < 4; ++q) {
                            f[q].t = q < 2 ? 1 : 0;
                            f[q].n = norm(V3{ox * .25, oy * .25, 1});  // lit as the top is, not as the side below it
                            f[q].up = .8f;
                        }
                        const int order[6] = {0, 1, 2, 0, 2, 3};
                        for (int q = 0; q < 6; ++q) cache.fringe.push_back(f[order[q]]);
                    }
                }
        }
    for (size_t i = 0; i < cache.top.size(); ++i) {
        cache.top[i].n = norm(b.normals[b.keys_top[i]]);
        cache.top[i].up = static_cast<float>(std::max(0.0, cache.top[i].n.z));
    }
    for (size_t i = 0; i < cache.side.size(); ++i) {
        cache.side[i].n = norm(b.normals[b.keys_side[i]]);
        cache.side[i].up = static_cast<float>(std::max(0.0, cache.side[i].n.z));
    }
}

// ------------------------------------------------------------------ the shade on the ground
void build_shadow(HedgeCache& cache, const Level& lv, V3 sun) {
    const Shape shape(lv);
    cache.shadow_cols = lv.w + 2;
    cache.shadow_rows = lv.h + 2;
    int tw = 4, th = 4;
    while (tw < cache.shadow_cols * kShadowPerUnit) tw *= 2;
    while (th < cache.shadow_rows * kShadowPerUnit) th *= 2;
    cache.shadow_x0 = shape.cx(0) - 1.5;
    cache.shadow_y0 = shape.cy(lv.h - 1) - 1.5;
    cache.shadow_span_x = static_cast<double>(tw) / kShadowPerUnit;
    cache.shadow_span_y = static_cast<double>(th) / kShadowPerUnit;
    const size_t count = static_cast<size_t>(tw) * th;
    std::vector<float> mass(count, 0.f), cast(count, 0.f), near(count, 0.f);
    const int used_w = cache.shadow_cols * kShadowPerUnit, used_h = cache.shadow_rows * kShadowPerUnit;
    for (int y = 0; y < used_h; ++y)
        for (int x = 0; x < used_w; ++x) {
            const double wx = cache.shadow_x0 + (x + .5) / kShadowPerUnit, wy = cache.shadow_y0 + (y + .5) / kShadowPerUnit;
            mass[static_cast<size_t>(y) * tw + x] = shape.inside(wx, wy) ? 1.f : 0.f;
        }
    // the sun's shadow: the hedge's footprint swept away from the sun as far as its height throws it
    const double horizontal = std::hypot(sun.x, sun.y);
    const double reach = (kHeight - kShoulder * .5) * horizontal / std::max(.2, sun.z);
    const double dx = -sun.x / std::max(1e-6, horizontal), dy = -sun.y / std::max(1e-6, horizontal);
    for (int y = 0; y < used_h; ++y)
        for (int x = 0; x < used_w; ++x) {
            float v = 0;
            for (int k = 1; k <= 12 && v < 1; ++k) {
                const double s = reach * k / 12 * kShadowPerUnit;
                const int sx = static_cast<int>(std::lround(x - dx * s)), sy = static_cast<int>(std::lround(y - dy * s));
                if (sx >= 0 && sy >= 0 && sx < used_w && sy < used_h) v = std::max(v, mass[static_cast<size_t>(sy) * tw + sx]);
            }
            cast[static_cast<size_t>(y) * tw + x] = v;
        }
    // soften both: three box passes each way
    near = mass;
    std::vector<float> tmp(count, 0.f);
    for (int pass = 0; pass < 6; ++pass) {
        std::vector<float>& field = pass < 3 ? near : cast;
        const int radius = pass < 3 ? 2 : 1;
        for (int y = 0; y < used_h; ++y)
            for (int x = 0; x < used_w; ++x) {
                float s = 0;
                for (int k = -radius; k <= radius; ++k) s += field[static_cast<size_t>(y) * tw + std::clamp(x + k, 0, used_w - 1)];
                tmp[static_cast<size_t>(y) * tw + x] = s / (2 * radius + 1);
            }
        for (int y = 0; y < used_h; ++y)
            for (int x = 0; x < used_w; ++x) {
                float s = 0;
                for (int k = -radius; k <= radius; ++k) s += tmp[static_cast<size_t>(std::clamp(y + k, 0, used_h - 1)) * tw + x];
                field[static_cast<size_t>(y) * tw + x] = s / (2 * radius + 1);
            }
    }
    cache.shadow.make(tw, th);
    std::fill(cache.shadow.px.begin(), cache.shadow.px.end(), 0U);
    std::vector<std::uint8_t> any(static_cast<size_t>(cache.shadow_cols) * cache.shadow_rows, 0);
    for (int y = 0; y < used_h; ++y)
        for (int x = 0; x < used_w; ++x) {
            const size_t i = static_cast<size_t>(y) * tw + x;
            const double a = std::clamp(.62 * std::sqrt(near[i]) + .30 * cast[i], 0.0, .66);
            const std::uint32_t q = static_cast<std::uint32_t>(std::lround(a * 255));
            cache.shadow.px[i] = q << 24U;
            if (q > 5) any[static_cast<size_t>(y / kShadowPerUnit) * cache.shadow_cols + x / kShadowPerUnit] = 1;
        }
    cache.shadow_cells.clear();
    for (size_t i = 0; i < any.size(); ++i)
        if (any[i]) cache.shadow_cells.push_back(static_cast<int>(i));
}

// ------------------------------------------------------------------ colour for the season
struct HedgeColours {
    Col old_leaf, leaf, fresh;  // the drift along the run: older and darker .. new growth
    double flush;               // how much new growth lies on top and upper sides
    bool snow;
    Col frost;                  // winter's leaves whitened by frost
};
HedgeColours colours_for(Season season) {
    switch (season) {
        case Season::spring: return {hex(0x3A6E30), hex(0x579242), hex(0x9CC850), .55, false, {}};
        case Season::summer: return {hex(0x2C5E2E), hex(0x437E38), hex(0x6AA042), .22, false, {}};
        case Season::autumn: return {hex(0x8A5028), hex(0xB46E36), hex(0xDA9A4C), .35, false, {}};  // a beech hedge in its copper
        case Season::winter: return {hex(0x284E36), hex(0x386646), hex(0x4A7856), 0, true, hex(0x8AA8A4)};
        case Season::night: return {hex(0x284E36), hex(0x386646), hex(0x4A7856), 0, true, hex(0x8AA8A4)};
    }
    return colours_for(Season::spring);
}

Col leaf_colour(const HedgeColours& k, const HedgeCache::Corner& c) {
    const float drift = c.tone;
    Col col = drift < 0 ? mix(k.leaf, k.old_leaf, -drift) : mix(k.leaf, k.fresh, drift * .55f);
    // new growth on the outer, upper leaves
    const float flush = static_cast<float>(k.flush) * (.35f + .65f * c.up) * std::clamp(.5f + drift, 0.f, 1.f) * (1 - c.foot);
    col = mix(col, k.fresh, flush);
    if (k.snow) col = mix(col, k.frost, .20f + .25f * c.up);
    // the outer leaves on top catch the most light
    const float k2 = c.shade * (1 + .14f * c.up * c.up);
    return {col.r * k2, col.g * k2, col.b * k2, 1};
}

void colour_shell(HedgeCache& cache, Season season) {
    const HedgeColours k = colours_for(season);
    cache.top_v.clear();
    cache.side_v.clear();
    cache.snow_v.clear();
    for (const HedgeCache::Corner& c : cache.top) {
        Vtx v;
        v.p = c.p;
        v.n = c.n;
        v.s = c.s;
        v.t = c.t;
        v.c = leaf_colour(k, c);
        if (k.snow) v.c = mix(v.c, hex(0xDCE6EE), .55f * std::clamp((c.up - .45f) / .45f, 0.f, 1.f));
        cache.top_v.push_back(v);
    }
    // on a snowy day snow lies on the flat of the top, a little above the leaves, with the shoulders frosted
    for (size_t i = 0; k.snow && i + 2 < cache.top.size(); i += 3) {
        if (cache.top[i].up < .9f || cache.top[i + 1].up < .9f || cache.top[i + 2].up < .9f) continue;
        for (int j = 0; j < 3; ++j) {
            const HedgeCache::Corner& c = cache.top[i + static_cast<size_t>(j)];
            Vtx v;
            v.p = c.p + c.n * .014;
            v.n = c.n;
            v.s = c.s;
            v.t = c.t;
            const float w = .90f + .10f * std::min(1.f, c.shade);
            v.c = {w, w, w, 1};
            cache.snow_v.push_back(v);
        }
    }
    for (const HedgeCache::Corner& c : cache.side) {
        Vtx v;
        v.p = c.p;
        v.n = c.n;
        v.s = c.s;
        v.t = c.t;
        v.c = leaf_colour(k, c);
        cache.side_v.push_back(v);
    }
    cache.fringe_v.clear();
    for (const HedgeCache::Corner& c : cache.fringe) {
        Vtx v;
        v.p = c.p;
        v.n = c.n;
        v.s = c.s;
        v.t = c.t;
        v.c = leaf_colour(k, c);
        if (k.snow) v.c = mix(v.c, hex(0xDCE6EE), .35f);
        cache.fringe_v.push_back(v);
    }
    cache.season = season;
    cache.coloured = true;
}

constexpr std::uint16_t kShade = translucent | unlit | no_depth_write | double_sided;

}  // namespace

void Garden::draw_hedges(const GardenState& s, double t) {
    static_cast<void>(t);
    if (!hedge_) hedge_ = std::make_shared<HedgeCache>();
    HedgeCache& c = *hedge_;
    if (c.w != lv_.w || c.h != lv_.h || c.tiles != lv_.tiles) {
        c.w = lv_.w;
        c.h = lv_.h;
        c.tiles = lv_.tiles;
        build_shell(c, lv_);
        build_shadow(c, lv_, r.light.sun);
        c.coloured = false;
    }
    if (!c.coloured || c.season != s.season) colour_shell(c, s.season);
    const HedgeTextures& tx = textures();
    if (!c.side_v.empty()) r.draw(c.side_v.data(), c.side_v.size(), &tx.side, opaque);
    if (!c.top_v.empty()) r.draw(c.top_v.data(), c.top_v.size(), &tx.top, opaque);
    if (!c.snow_v.empty()) r.draw(c.snow_v.data(), c.snow_v.size(), &tx.snow, cutout);
    if (!c.fringe_v.empty()) r.draw(c.fringe_v.data(), c.fringe_v.size(), &tx.fringe, static_cast<std::uint16_t>(cutout | double_sided));
    // the shade at the foot and the sun's shadow, on the soil and the lawn alike; drawn after the hedge, so
    // the depth test spares the pixels the hedge covers
    const float strength = s.season == Season::night ? .7f : 1.f;
    for (int cell : c.shadow_cells) {
        const int col = cell % c.shadow_cols, row = cell / c.shadow_cols;
        const double x0 = c.shadow_x0 + col, y0 = c.shadow_y0 + row;
        const double s0 = col / c.shadow_span_x, t0 = row / c.shadow_span_y, s1 = (col + 1) / c.shadow_span_x, t1 = (row + 1) / c.shadow_span_y;
        const Col ink{0, 0, 0, strength};
        const V3 n{0, 0, 1};
        Vtx q[6] = {{{x0, y0, .004}, n, s0, t0, ink}, {{x0 + 1, y0, .004}, n, s1, t0, ink}, {{x0 + 1, y0 + 1, .004}, n, s1, t1, ink},
                    {{x0, y0, .004}, n, s0, t0, ink}, {{x0 + 1, y0 + 1, .004}, n, s1, t1, ink}, {{x0, y0 + 1, .004}, n, s0, t1, ink}};
        r.draw(q, 6, &c.shadow, kShade);
    }
}

}  // namespace ct
