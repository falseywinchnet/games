#include "world.hpp"

#include <algorithm>
#include <cmath>

namespace eggy {

std::uint64_t mix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

const char* biome_name(Biome b) {
    static const char* names[] = {"Meadow", "Forest", "Autumn Wood", "Pond Country", "Rocky Ravine",
                                  "Alpine Slopes", "Snowfield", "Ice Falls", "Windy Ridge", "The Summit"};
    return names[static_cast<int>(b)];
}

namespace {
constexpr std::int64_t kSummitRun = 320;
double smooth(double t) { t = std::clamp(t, 0.0, 1.0); return t * t * (3 - 2 * t); }
}  // namespace

double World::hash01(std::int64_t a, std::int64_t b, std::int64_t c) const {
    std::uint64_t h = mix64(seed_ ^ mix64(static_cast<std::uint64_t>(a) * 0x9E3779B185EBCA87ULL ^
                                          mix64(static_cast<std::uint64_t>(b) * 0xC2B2AE3D27D4EB4FULL ^
                                                mix64(static_cast<std::uint64_t>(c) + 0x165667B19E3779F9ULL))));
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

double World::noise1(double x, std::int64_t salt) const {
    const double fl = std::floor(x);
    const std::int64_t i = static_cast<std::int64_t>(fl);
    const double f = smooth(x - fl);
    const double a = hash01(i, salt, 7001) * 2 - 1, b = hash01(i + 1, salt, 7001) * 2 - 1;
    return a + (b - a) * f;
}

double World::noise2(double x, double y, std::int64_t salt) const {
    const double fx = std::floor(x), fy = std::floor(y);
    const std::int64_t ix = static_cast<std::int64_t>(fx), iy = static_cast<std::int64_t>(fy);
    const double tx = smooth(x - fx), ty = smooth(y - fy);
    auto h = [&](std::int64_t a, std::int64_t b) { return hash01(a, b, salt) * 2 - 1; };
    const double a = h(ix, iy), b = h(ix + 1, iy), c = h(ix, iy + 1), d = h(ix + 1, iy + 1);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

World::World(std::uint64_t seed) : seed_(seed), cache_(1024) {
    length_ = 190'000'000 + static_cast<std::int64_t>(hash01(1, 2, 3) * 70'000'000);
    const int count = 200 + static_cast<int>(hash01(4, 5, 6) * 221);
    stars_.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        Star s;
        s.v = 3000 + static_cast<std::int64_t>(hash01(i, 77, 1) * static_cast<double>(length_ - 6000));
        const double side = hash01(i, 77, 2) < .5 ? -1 : 1;
        double u = lane(static_cast<double>(s.v)) + side * (2.4 + hash01(i, 77, 3) * 1.6);
        if (u < .6 || u > kWidth - .6) u = lane(static_cast<double>(s.v)) - side * (2.4 + hash01(i, 77, 3) * 1.6);
        s.u = static_cast<float>(std::clamp(u, .6, kWidth - .6));
        stars_.push_back(s);
    }
    std::sort(stars_.begin(), stars_.end(), [](const Star& a, const Star& b) { return a.v < b.v; });
}

int World::star_at(int u, std::int64_t v) const {
    auto it = std::lower_bound(stars_.begin(), stars_.end(), v, [](const Star& s, std::int64_t x) { return s.v < x; });
    for (; it != stars_.end() && it->v == v; ++it)
        if (static_cast<int>(it->u) == u) return static_cast<int>(it - stars_.begin());
    return -1;
}

Biome World::pick_biome(std::int64_t k) const {
    if (k <= 0) return Biome::meadow;
    if (k == 1) return Biome::forest;
    const double p = std::clamp(static_cast<double>(k) * kSegment / static_cast<double>(length_), 0.0, 1.0);
    const double q = 1 - p;
    double w[9] = {1.0 * std::pow(q, 1.5) + .15, 1.1 * q + .10, .5 * q + .05, .6 * q * q + .08, .5 + .3 * p,
                   .25 + .9 * p, .12 + 1.2 * p, .05 + .8 * p, .05 + .8 * p};
    auto choose = [&](double r, int exclude) {
        double total = 0;
        for (int i = 0; i < 9; ++i) if (i != exclude) total += w[i];
        r *= total;
        for (int i = 0; i < 9; ++i) {
            if (i == exclude) continue;
            if (r < w[i]) return i;
            r -= w[i];
        }
        return 8;
    };
    const int prev = choose(hash01(k - 1, 0, 1), -1);
    int mine = choose(hash01(k, 0, 1), -1);
    if (mine == prev && hash01(k, 0, 3) < .75) mine = choose(hash01(k, 0, 2), prev);
    return static_cast<Biome>(mine);
}

SegmentInfo World::segment(std::int64_t k) const {
    SegCache& c = seg_cache_[static_cast<size_t>(k & 63)];
    if (c.k == k) return c.info;
    c.k = k;
    c.info = compute_segment(k);
    return c.info;
}

SegmentInfo World::compute_segment(std::int64_t k) const {
    SegmentInfo s;
    if (k * kSegment >= length_ - kSummitRun) {
        s.biome = Biome::summit;
        s.cold = .6f; s.wind = .3f;
        return s;
    }
    s.biome = pick_biome(k);
    const double h = hash01(k, 1, 10);
    switch (s.biome) {
        case Biome::ravine: case Biome::alpine: case Biome::ridge: s.terraces = h < .55; break;
        case Biome::meadow: case Biome::forest: case Biome::autumn: s.terraces = h < .3; break;
        case Biome::snow: s.terraces = h < .35; break;
        default: s.terraces = h < .15; break;
    }
    if (k < 2) s.terraces = false;
    s.terrace_rows = hash01(k, 1, 11) < .4 ? 3 : 2;
    const bool brooky = s.biome == Biome::meadow || s.biome == Biome::forest || s.biome == Biome::autumn ||
                        s.biome == Biome::alpine || s.biome == Biome::pond;
    s.brook = brooky && hash01(k, 1, 12) < .55 && k > 0;
    s.brook_row = static_cast<double>(k * kSegment) + 50 + hash01(k, 1, 13) * 150;
    s.brook_phase = hash01(k, 1, 14) * 6.283;
    s.brook_amp = 1 + hash01(k, 1, 15) * 2;
    s.autumn = s.biome == Biome::autumn;
    const float j = static_cast<float>(.6 + hash01(k, 1, 16) * .6);
    const float wind[] = {.25f, .15f, .2f, .15f, .35f, .6f, .5f, .5f, .9f};
    const float leaves[] = {.08f, .5f, 1.f, .2f, 0, .05f, 0, 0, 0};
    const float heat[] = {.6f, .1f, .15f, .4f, .8f, .4f, .15f, .1f, .2f};
    const float cold[] = {0, 0, .1f, 0, .1f, .4f, .8f, .9f, .7f};
    const int b = static_cast<int>(s.biome);
    s.wind = std::min(1.f, wind[b] * j);
    s.leaves = leaves[b];
    s.heat = heat[b];
    s.cold = cold[b];
    if ((s.biome == Biome::meadow || s.biome == Biome::forest || s.biome == Biome::pond) && hash01(k, 1, 17) < .25)
        s.rain = static_cast<float>(.4 + hash01(k, 1, 18) * .6);
    return s;
}

double World::lane(double v) const {
    const double c = kWidth * .5 + 3.0 * (.65 * noise1(v * .018, 11) + .35 * noise1(v * .051, 12));
    return std::clamp(c, 1.3, kWidth - 2.3);
}

// Smooth everywhere: the height of a row's far edge is exactly the next row's
// near edge. The slope breathes (steeper and gentler stretches) along the climb.
// Base camp: level ground for the first rows, then the slope eases in smoothly
// (the climb distance d(v) ramps its rate from 0 to 1, so there is no kink).
double World::climb_distance(double v) const {
    if (v <= kCampEnd) return 0;
    const double R = kRampRows;
    const double x = v - kCampEnd;
    if (x < R) return x * x / (2 * R);
    return x - R / 2;
}

double World::base_at(double v) const {
    if (v <= 0) return 0;
    const double summit0 = static_cast<double>(length_ - kSummitRun);
    auto climb = [&](double x) {
        const double d = climb_distance(x);
        return kSlope * d + 2.4 * (noise1(d * .021, 81) - noise1(0, 81)) + .9 * (noise1(d * .067, 82) - noise1(0, 82));
    };
    if (v >= summit0) {
        const double x = std::min(1.0, (v - summit0) / kSummitRun);
        return climb(summit0) + kSlope * kSummitRun * .45 * (1 - (1 - x) * (1 - x));
    }
    return climb(v);
}

double World::camp_flatness(double v) const {  // 1 = flat camp ground, 0 = full mountain
    return 1 - std::clamp((v - kCampEnd + 6) / (kRampRows * .8), 0.0, 1.0);
}
double World::base_near(std::int64_t v) const { return base_at(static_cast<double>(v)); }
double World::base_far(std::int64_t v) const { return base_at(static_cast<double>(v + 1)); }

double World::relief(double u, double v) const {
    double r = .30 * noise2(u * .32, v * .11, 3) + .12 * noise2(u * .9, v * .35, 4) + (kWidth * .5 - u) * .05;
    if (u < 0) r += std::pow(-u, 1.3) * .42;               // the mountain rises on the far flank
    if (u > kWidth) r -= (u - kWidth) * .32 + std::pow(u - kWidth, 1.35) * .06;  // the mountainside continues down toward us
    if (v > static_cast<double>(length_ - kSummitRun)) r *= .5;
    return r * (1 - .9 * camp_flatness(v));
}

double World::ground(double u, double v) const {
    const double fu = std::floor(u), fv = std::floor(v);
    const std::int64_t iv = static_cast<std::int64_t>(fv);
    const double tu = u - fu, tv = v - fv;
    if (u < 0 || u >= kWidth) {
        const double near = base_near(iv), far = base_far(iv);
        return near + (far - near) * tv + relief(u, v);
    }
    const Tile& t = tile(static_cast<int>(fu), iv);
    const double a = t.z[0] + (t.z[1] - t.z[0]) * tu;
    const double b = t.z[3] + (t.z[2] - t.z[3]) * tu;
    return a + (b - a) * tv;
}

// Low-frequency regional noise chooses among the grounds that suit a surface
// and biome, so meadows drift from lush to flowery to clover, rock from granite
// to lichen to shale, snow from fresh to wind-carved. Neighbours splat-blend.
std::uint8_t World::look_for(Surface s, Biome b, double u, double v) const {
    const double r = .5 + .5 * noise2(u * .11, v * .045, 90) + .15 * noise2(u * .4, v * .16, 91);
    auto pick = [&](std::initializer_list<int> ids) {
        const int n = static_cast<int>(ids.size());
        const int i = std::clamp(static_cast<int>(r * n), 0, n - 1);
        return static_cast<std::uint8_t>(*(ids.begin() + i));
    };
    switch (s) {
        case Surface::grass:
            if (b == Biome::alpine || b == Biome::ridge) return pick({4, 5, 3, 4});
            if (b == Biome::pond) return pick({7, 0, 6, 2});
            return pick({0, 1, 2, 3, 0, 6});
        case Surface::forest_floor:
            if (b == Biome::autumn) return pick({10, 11, 9, 10});
            return pick({8, 9, 13, 12, 14, 8});
        case Surface::moss: return pick({12, 6});
        case Surface::path: return pick({15, 15, 16, 17});
        case Surface::rock:
            if (b == Biome::ravine) return pick({20, 22, 24, 28, 25});
            if (b == Biome::ridge) return pick({23, 21, 33});
            if (b == Biome::snow || b == Biome::ice || b == Biome::summit) return pick({33, 21, 33});
            return pick({21, 24, 25, 20});
        case Surface::gravel: return pick({27, 26, 19});
        case Surface::snow:
            if (b == Biome::summit) return pick({29, 30});
            return pick({29, 30, 31, 34, 32});
        case Surface::ice: return pick({35, 36, 37});
        case Surface::water: return pick({38, 39});
        case Surface::sand: return pick({18, 19});
    }
    return 0;
}

std::uint8_t World::flank_look(double u, std::int64_t v, bool steep) const {
    const Biome b = row(v).biome;
    Surface s = Surface::grass;
    switch (b) {
        case Biome::forest: case Biome::autumn: s = Surface::forest_floor; break;
        case Biome::ravine: case Biome::ridge: s = Surface::rock; break;
        case Biome::alpine: s = steep ? Surface::rock : Surface::grass; break;
        case Biome::snow: case Biome::ice: case Biome::summit: s = steep ? Surface::rock : Surface::snow; break;
        default: s = steep ? Surface::rock : Surface::grass; break;
    }
    return look_for(s, b, u, static_cast<double>(v));
}

int World::camp_fire_u() const {
    const double ln = lane(8.5);
    return static_cast<int>(std::clamp(ln + 2.4 <= kWidth - 1 ? ln + 2.4 : ln - 2.6, 0.0, kWidth - 1.0));
}

bool World::blocking(Feature f) {
    return f == Feature::campfire || f == Feature::crate || f == Feature::tree || f == Feature::pine || f == Feature::boulder || f == Feature::stump ||
           f == Feature::cairn || f == Feature::signpost || f == Feature::flags || f == Feature::bush;
}

double World::hop_height(Feature f) {
    if (f == Feature::log) return .42;
    if (f == Feature::rock) return .45;
    if (f == Feature::snowdrift) return .32;
    return 0;
}

const Row& World::row(std::int64_t v) const {
    Row& slot = cache_[static_cast<size_t>(v & 1023)];
    if (slot.v != v) generate(v, slot);
    return slot;
}

void World::generate(std::int64_t v, Row& out) const {
    out.v = v;
    const double dv = static_cast<double>(v);
    const std::int64_t k = v >= 0 ? v / kSegment : -1;
    const SegmentInfo sa = segment(std::max<std::int64_t>(0, k));
    const SegmentInfo sb = segment(std::max<std::int64_t>(0, k + 1));
    const double tseg = static_cast<double>(v - std::max<std::int64_t>(0, k) * kSegment);
    const double bfac = smooth((tseg - (kSegment - 48)) / 48.0);
    const double near = base_near(v), far = base_far(v);
    out.riser = near - base_far(v - 1);
    const double ln = lane(dv + .5);
    const bool summit = v >= length_ - kSummitRun;
    int countb = 0;
    // whole-row features
    bool log_row = false;
    int log_a = 0, log_b = -1;
    {
        const double lp = sa.biome == Biome::forest ? .05 : sa.biome == Biome::autumn ? .06
                        : sa.biome == Biome::meadow ? .012 : sa.biome == Biome::alpine ? .01 : 0;
        if (v > 20 && !summit && hash01(v, 0, 50) < lp && out.riser < .05) {
            log_row = true;
            const int len = 2 + static_cast<int>(hash01(v, 0, 51) * 3);
            log_a = static_cast<int>(hash01(v, 0, 52) * (kWidth - len + 1));
            log_b = log_a + len - 1;
        }
    }
    const std::int64_t pblock = v / 48;
    const std::int64_t prow = pblock * 48 + static_cast<std::int64_t>(hash01(pblock, 0, 60) * 48);
    int puddle_u = -1;
    if (prow == v && v > 10) {
        const double side = hash01(pblock, 0, 61) < .5 ? -1.5 : 1.5;
        puddle_u = static_cast<int>(std::clamp(std::floor(ln + side), 0.0, kWidth - 1.0));
    }
    const bool sign_row = (v % 2000 == 1000) || v == 4;
    for (int u = 0; u < kWidth; ++u) {
        Tile& t = out.t[static_cast<size_t>(u)];
        const double du = u;
        t.z[0] = near + relief(du, dv);
        t.z[1] = near + relief(du + 1, dv);
        t.z[2] = far + relief(du + 1, dv + 1);
        t.z[3] = far + relief(du, dv + 1);
        const double n = .5 + .5 * noise2(du * .45, dv * .09, 22);
        const SegmentInfo& s = (n < bfac) ? sb : sa;
        Biome b = summit ? Biome::summit : s.biome;
        t.biome = b;
        if (b == sb.biome) ++countb;
        t.variant = static_cast<std::uint8_t>(hash01(u, v, 30) * 255);
        t.fx = static_cast<float>(.3 + .4 * hash01(u, v, 31));
        t.fy = static_cast<float>(.3 + .4 * hash01(u, v, 32));
        t.flow = 0;
        t.feature = Feature::none;
        const double lanedist = std::fabs(du + .5 - ln);
        t.lane = lanedist < 1.25;
        const double sn = noise2(du * .5, dv * .13, 33);
        bool band = false;
        switch (b) {
            case Biome::meadow: t.surface = Surface::grass; break;
            case Biome::forest: case Biome::autumn: t.surface = sn > .45 ? Surface::moss : Surface::forest_floor; break;
            case Biome::pond:
                t.surface = Surface::grass;
                if (noise2(du * .42, dv * .16, 34) > .22 && v > 8) t.surface = Surface::water;
                break;
            case Biome::ravine: t.surface = sn > .05 ? Surface::rock : Surface::gravel; break;
            case Biome::alpine: t.surface = sn > .35 ? Surface::rock : sn < -.25 ? Surface::gravel : Surface::grass; break;
            case Biome::snow: t.surface = sn > .55 ? Surface::rock : Surface::snow; break;
            case Biome::ice: {
                t.surface = Surface::snow;
                const std::int64_t blk = v / 24;
                const std::int64_t start = blk * 24 + 4;
                const std::int64_t len = 6 + static_cast<std::int64_t>(hash01(blk, 0, 70) * 9);
                if (hash01(blk, 0, 71) < .75 && v >= start && v < start + len) { t.surface = Surface::ice; band = true; }
                break;
            }
            case Biome::ridge: t.surface = sn > .2 ? Surface::snow : Surface::rock; break;
            case Biome::summit: t.surface = Surface::snow; break;
            default: break;
        }
        if ((b == Biome::meadow || b == Biome::forest || b == Biome::autumn || b == Biome::alpine) &&
            lanedist < .55 && noise2(du * .3, dv * .07, 35) > .15)
            t.surface = Surface::path;
        if (s.brook && !summit) {
            const double d = dv + .5 - (s.brook_row + s.brook_amp * std::sin(du * .55 + s.brook_phase));
            if (std::fabs(d) < 1.0) { t.surface = Surface::water; t.flow = 1; }
        }
        t.look = look_for(t.surface, b, du + .5, dv + .5);
        const bool star_here = star_at(u, v) >= 0;
        if (t.surface == Surface::water) {
            if (t.lane) t.feature = hash01(u, v, 40) < .55 ? Feature::stone : Feature::lily;
            else if (hash01(u, v, 41) < .14) t.feature = Feature::lily;
            continue;
        }
        if (band) {
            if (lanedist < .8 || hash01(u, v, 42) < .12) t.feature = Feature::ledge;
            else if (hash01(u, v, 43) < .05) t.feature = Feature::crystal;
            continue;
        }
        const double r = hash01(u, v, 44);
        const bool may_block = !t.lane && !star_here && lanedist >= 1.3 && v > 6;
        struct Opt { Feature f; double p; };
        std::vector<Opt> opts;
        switch (b) {
            case Biome::meadow: opts = {{Feature::tree, .04}, {Feature::bush, .03}, {Feature::boulder, .015}, {Feature::rock, .02}, {Feature::flower, .22}, {Feature::clover, .10}, {Feature::tuft, .25}, {Feature::mushroom, .01}}; break;
            case Biome::forest: opts = {{Feature::tree, .20}, {Feature::pine, .08}, {Feature::bush, .05}, {Feature::stump, .03}, {Feature::fern, .18}, {Feature::mushroom, .06}, {Feature::tuft, .10}, {Feature::flower, .03}, {Feature::lichen, .02}}; break;
            case Biome::autumn: opts = {{Feature::tree, .22}, {Feature::bush, .04}, {Feature::stump, .03}, {Feature::fern, .10}, {Feature::mushroom, .10}, {Feature::tuft, .10}, {Feature::pebble, .03}}; break;
            case Biome::pond: opts = {{Feature::tree, .04}, {Feature::bush, .05}, {Feature::flower, .12}, {Feature::tuft, .25}, {Feature::mushroom, .03}, {Feature::fern, .04}}; break;
            case Biome::ravine: opts = {{Feature::boulder, .14}, {Feature::bush, .02}, {Feature::rock, .07}, {Feature::pebble, .20}, {Feature::lichen, .10}, {Feature::tuft, .05}}; break;
            case Biome::alpine: opts = {{Feature::boulder, .05}, {Feature::rock, .05}, {Feature::pine, .05}, {Feature::bush, .04}, {Feature::lichen, .15}, {Feature::flower, .08}, {Feature::tuft, .12}, {Feature::cairn, .01}, {Feature::pebble, .10}}; break;
            case Biome::snow: opts = {{Feature::pine, .07}, {Feature::boulder, .04}, {Feature::snowdrift, .05}, {Feature::pebble, .03}, {Feature::crystal, .01}}; break;
            case Biome::ice: opts = {{Feature::snowdrift, .05}, {Feature::boulder, .03}, {Feature::crystal, .05}}; break;
            case Biome::ridge: opts = {{Feature::boulder, .08}, {Feature::rock, .06}, {Feature::cairn, .02}, {Feature::flags, .012}, {Feature::lichen, .08}, {Feature::snowdrift, .04}}; break;
            case Biome::summit: opts = {{Feature::flags, (u == 0 || u == kWidth - 1) ? .08 : 0.0}}; break;
            default: break;
        }
        double acc = 0;
        for (const Opt& o : opts) {
            acc += o.p;
            if (r < acc) { t.feature = o.f; break; }
        }
        if (blocking(t.feature) && !may_block) t.feature = hash01(u, v, 45) < .5 ? Feature::tuft : Feature::pebble;
        if (World::hop_height(t.feature) > 0 && star_here) t.feature = Feature::none;
        if ((b == Biome::snow || b == Biome::summit || b == Biome::ice || b == Biome::ridge) &&
            (t.feature == Feature::tuft || t.feature == Feature::flower || t.feature == Feature::clover))
            t.feature = Feature::pebble;
        if (log_row && u >= log_a && u <= log_b) t.feature = Feature::log;
        if (u == puddle_u) t.feature = Feature::puddle;
        if (v < kCampEnd) {
            if (blocking(t.feature) || hop_height(t.feature) > 0 || t.feature == Feature::puddle) t.feature = hash01(u, v, 46) < .5 ? Feature::tuft : Feature::flower;
            const int fire_u = camp_fire_u();
            if (v == 8 && u == fire_u) t.feature = Feature::campfire;
            if ((v == 11 && u == fire_u) || (v == 12 && u == fire_u) || (v == 11 && u == std::clamp(fire_u + (fire_u > 5 ? 1 : -1), 0, kWidth - 1)))
                t.feature = Feature::crate;
            if (v == kCampEnd - 3 && u == std::clamp(fire_u + (fire_u > 5 ? -1 : 1), 0, kWidth - 1) && !t.lane) t.feature = Feature::signpost;
        }
        if (sign_row && u == static_cast<int>(std::clamp(std::floor(ln) + 2, 0.0, kWidth - 1.0)) && !t.lane)
            t.feature = Feature::signpost;
    }
    out.biome = summit ? Biome::summit : (countb * 2 > kWidth ? sb.biome : sa.biome);
}

}  // namespace eggy
