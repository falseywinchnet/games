#include "particles.hpp"

#include <algorithm>
#include <cmath>

namespace particles {
namespace {

std::uint32_t mix32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
// The k-th random number of a particle: 0..1.
float draw(std::uint32_t particle, std::uint32_t k) {
    const std::uint32_t h = mix32(particle ^ mix32(k * 0x9E3779B1U + 0x85EBCA77U));
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}
float between(Range range, float t) {
    return range.low + (range.high - range.low) * t;
}
float jitter(float mean, float spread, float t) {
    return mean + spread * (2 * t - 1);
}

// The air's swirling motion at a point and time: the curl of a smooth stream
// function, so it neither piles particles up nor thins them out.
Vec swirl_at(const Forces& forces, Vec p, float time) {
    if (forces.swirl <= 0)
        return {};
    const float k = 6.2831853f / forces.swirl_scale;
    const float w = 6.2831853f / forces.swirl_period;
    const float ax = k * p.x + w * time * .7f;
    const float ay = k * p.y * .8f - w * time * .5f;
    const float bx = 1.7f * k * (p.x + p.y) + w * time * .9f + 1.3f;
    // psi = sin(ax) cos(ay) + .5 sin(bx); u = d psi / dy, v = -d psi / dx.
    const float u = -std::sin(ax) * std::sin(ay) * k * .8f + .5f * std::cos(bx) * 1.7f * k;
    const float v = -(std::cos(ax) * std::cos(ay) * k + .5f * std::cos(bx) * 1.7f * k);
    const float scale = forces.swirl / (2.2f * k);
    const float drift = std::sin(k * .6f * p.z + w * time * .6f + 2.1f);
    return {u * scale, v * scale, drift * forces.swirl * .4f};
}

} // namespace

System::System(std::size_t capacity) : capacity_(capacity) {
    position_.resize(capacity);
    velocity_.resize(capacity);
    age_.resize(capacity);
    life_.resize(capacity);
    size_.resize(capacity);
    tail_.resize(capacity);
    kind_.resize(capacity);
    origin_.resize(capacity);
    seed_.resize(capacity);
}

int System::add_emitter(const Emitter& emitter) {
    Source source;
    source.spec = emitter;
    emitters_.push_back(source);
    return static_cast<int>(emitters_.size()) - 1;
}

void System::set_rate(int emitter, float rate) {
    if (emitter < 0 || emitter >= static_cast<int>(emitters_.size()))
        return;
    emitters_[static_cast<std::size_t>(emitter)].spec.rate = std::max(0.0f, rate);
}

void System::advance(double seconds, double step_seconds) {
    landings_.clear();
    if (seconds <= 0 || step_seconds <= 0)
        return;
    carried_ += seconds;
    // A step is taken when its time has come, give or take the rounding of the sums.
    while (carried_ >= step_seconds - 1e-9) {
        step(static_cast<float>(step_seconds));
        carried_ -= step_seconds;
        time_ += step_seconds;
    }
}

void System::birth(Source& source, std::size_t emitter_index, float into_step) {
    const std::uint32_t number = static_cast<std::uint32_t>(source.born);
    ++source.born;
    if (count_ >= capacity_) {
        ++skipped_;
        return;
    }
    const Emitter& e = source.spec;
    const std::uint32_t id = mix32(e.seed * 0x27d4eb2dU ^ number);
    const std::size_t i = count_;
    ++count_;
    position_[i] = {e.low.x + (e.high.x - e.low.x) * draw(id, 0), e.low.y + (e.high.y - e.low.y) * draw(id, 1),
                    e.low.z + (e.high.z - e.low.z) * draw(id, 2)};
    velocity_[i] = {jitter(e.velocity.x, e.velocity_spread.x, draw(id, 3)),
                    jitter(e.velocity.y, e.velocity_spread.y, draw(id, 4)),
                    jitter(e.velocity.z, e.velocity_spread.z, draw(id, 5))};
    // Born part way through the step: carried on by the rest of it, so births at a
    // steady rate do not bunch into sheets.
    position_[i].x += velocity_[i].x * into_step;
    position_[i].y += velocity_[i].y * into_step;
    position_[i].z += velocity_[i].z * into_step;
    age_[i] = into_step;
    life_[i] = between(e.life, draw(id, 6));
    size_[i] = between(e.size, draw(id, 7));
    tail_[i] = between(e.tail, draw(id, 8));
    kind_[i] = e.kind;
    origin_[i] = static_cast<std::uint16_t>(emitter_index);
    seed_[i] = mix32(id + 17);
}

void System::remove(std::size_t index) {
    const std::size_t last = count_ - 1;
    if (index != last) {
        position_[index] = position_[last];
        velocity_[index] = velocity_[last];
        age_[index] = age_[last];
        life_[index] = life_[last];
        size_[index] = size_[last];
        tail_[index] = tail_[last];
        kind_[index] = kind_[last];
        origin_[index] = origin_[last];
        seed_[index] = seed_[last];
    }
    --count_;
}

void System::step(float seconds) {
    const float now = static_cast<float>(time_);
    // Move and age what lives, ending what is spent or has reached its floor.
    std::size_t i = 0;
    while (i < count_) {
        const Forces& f = emitters_[origin_[i]].spec.forces;
        Vec v = velocity_[i];
        Vec p = position_[i];
        v.y += (f.lift - f.gravity) * seconds;
        if (f.drag > 0) {
            const Vec air = swirl_at(f, p, now);
            const float pull = std::min(1.0f, f.drag * seconds);
            v.x += (air.x - v.x) * pull;
            v.y += (air.y - v.y) * pull;
            v.z += (air.z - v.z) * pull;
        }
        p.x += v.x * seconds;
        p.y += v.y * seconds;
        p.z += v.z * seconds;
        velocity_[i] = v;
        position_[i] = p;
        age_[i] += seconds;
        size_[i] += f.growth * seconds;
        if (p.y < f.floor) {
            landings_.push_back(p);
            remove(i);
            continue;
        }
        if (age_[i] >= life_[i]) {
            remove(i);
            continue;
        }
        ++i;
    }
    // Then the births due this step.
    for (std::size_t e = 0; e < emitters_.size(); ++e) {
        Source& source = emitters_[e];
        source.owed += static_cast<double>(source.spec.rate) * seconds;
        const double due = std::floor(source.owed);
        source.owed -= due;
        const int births = static_cast<int>(due);
        for (int b = 0; b < births; ++b) {
            const float into = seconds * (static_cast<float>(b) + .5f) / static_cast<float>(births);
            birth(source, e, into);
        }
    }
}

Projected project(const Lens& lens, Vec point) {
    const Vec d{point.x - lens.eye.x, point.y - lens.eye.y, point.z - lens.eye.z};
    const float ahead = d.x * lens.forward.x + d.y * lens.forward.y + d.z * lens.forward.z;
    const float across = d.x * lens.right.x + d.y * lens.right.y + d.z * lens.right.z;
    const float upward = d.x * lens.up.x + d.y * lens.up.y + d.z * lens.up.z;
    Projected out;
    if (lens.bend <= 0) {
        if (ahead <= .05f)
            return out;
        out.x = static_cast<float>(lens.width) * .5f + across / ahead * lens.focal;
        out.y = static_cast<float>(lens.height) * .5f - upward / ahead * lens.focal;
        out.depth = ahead;
        out.scale = lens.focal / ahead;
        out.in_front = true;
        return out;
    }
    const float distance = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (distance <= .05f)
        return out;
    const float side = std::sqrt(across * across + upward * upward);
    // sin and cos of the angle from the view's centre are side and ahead over the
    // distance: no trigonometry needed.
    const float b = lens.bend;
    const float c = ahead / distance;
    if (b + c <= .05f)
        return out;
    const float radius = (b + 1) * lens.focal * side / (b * distance + ahead);
    const float ux = side > 1e-6f ? across / side : 0.0f;
    const float uy = side > 1e-6f ? upward / side : 0.0f;
    out.x = static_cast<float>(lens.width) * .5f + radius * ux;
    out.y = static_cast<float>(lens.height) * .5f - radius * uy;
    out.depth = distance;
    out.scale = (b + 1) * lens.focal * (b * c + 1) / ((b + c) * (b + c)) / distance;
    out.in_front = true;
    return out;
}

namespace {
// Widens the half-open box (l, t, r, b) to take in another; an empty box takes it whole.
void widen(int& l, int& t, int& r, int& b, int left, int top, int right, int bottom) {
    if (left >= right || top >= bottom)
        return;
    if (l >= r || t >= b) {
        l = left;
        t = top;
        r = right;
        b = bottom;
        return;
    }
    l = std::min(l, left);
    t = std::min(t, top);
    r = std::max(r, right);
    b = std::max(b, bottom);
}
} // namespace

void Layer::reset(int w, int h) {
    if (w != width || h != height || colour.empty()) {
        width = w;
        height = h;
        const std::size_t n = static_cast<std::size_t>(std::max(0, w)) * static_cast<std::size_t>(std::max(0, h));
        colour.assign(n * 3, 0.0f);
        coverage.assign(n, 0.0f);
        through.assign(n, 1.0f);
        added.assign(n * 3, 0.0f);
        coarse_width = (std::max(0, w) + coarse_step - 1) / coarse_step + 1;
        coarse_height = (std::max(0, h) + coarse_step - 1) / coarse_step + 1;
        coarse.assign(static_cast<std::size_t>(coarse_width) * static_cast<std::size_t>(coarse_height) * 3, 0.0f);
    } else {
        // Only what was drawn on: row by row.
        for (int y = fine_top; y < fine_bottom; ++y) {
            const std::size_t first = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(fine_left);
            const std::size_t count = static_cast<std::size_t>(fine_right - fine_left);
            std::fill(colour.begin() + static_cast<std::ptrdiff_t>(first * 3), colour.begin() + static_cast<std::ptrdiff_t>((first + count) * 3), 0.0f);
            std::fill(coverage.begin() + static_cast<std::ptrdiff_t>(first), coverage.begin() + static_cast<std::ptrdiff_t>(first + count), 0.0f);
            std::fill(through.begin() + static_cast<std::ptrdiff_t>(first), through.begin() + static_cast<std::ptrdiff_t>(first + count), 1.0f);
            std::fill(added.begin() + static_cast<std::ptrdiff_t>(first * 3), added.begin() + static_cast<std::ptrdiff_t>((first + count) * 3), 0.0f);
        }
        if (glow_left < glow_right && glow_top < glow_bottom) {
            const int cl = std::max(0, glow_left / coarse_step - 1), cr = std::min(coarse_width, glow_right / coarse_step + 2);
            const int ct = std::max(0, glow_top / coarse_step - 1), cb = std::min(coarse_height, glow_bottom / coarse_step + 2);
            for (int y = ct; y < cb; ++y) {
                const std::size_t first = static_cast<std::size_t>(y) * static_cast<std::size_t>(coarse_width) + static_cast<std::size_t>(cl);
                std::fill(coarse.begin() + static_cast<std::ptrdiff_t>(first * 3),
                          coarse.begin() + static_cast<std::ptrdiff_t>((first + static_cast<std::size_t>(cr - cl)) * 3), 0.0f);
            }
        }
    }
    fine_left = fine_top = fine_right = fine_bottom = 0;
    glow_left = glow_top = glow_right = glow_bottom = 0;
    dirty_left = dirty_top = dirty_right = dirty_bottom = 0;
}

void Layer::mark(int left, int top, int right, int bottom) {
    left = std::max(0, left);
    top = std::max(0, top);
    right = std::min(width, right);
    bottom = std::min(height, bottom);
    widen(fine_left, fine_top, fine_right, fine_bottom, left, top, right, bottom);
    widen(dirty_left, dirty_top, dirty_right, dirty_bottom, left, top, right, bottom);
}

void Layer::mark_glow(int left, int top, int right, int bottom) {
    left = std::max(0, left);
    top = std::max(0, top);
    right = std::min(width, right);
    bottom = std::min(height, bottom);
    widen(glow_left, glow_top, glow_right, glow_bottom, left, top, right, bottom);
    widen(dirty_left, dirty_top, dirty_right, dirty_bottom, left, top, right, bottom);
}

namespace {

// Smooth noise along a line, 0..1, from a particle's seed: a random value at each
// whole step, eased between.
float bead_value(std::uint32_t seed, std::uint32_t k) {
    return static_cast<float>(mix32(seed + k * 0x9E3779B1U) >> 8) * (1.0f / 16777216.0f);
}
float beads(std::uint32_t seed, float t) {
    const float cell = std::floor(t);
    const float f = t - cell;
    const std::uint32_t k = static_cast<std::uint32_t>(static_cast<std::int32_t>(cell));
    const float a = bead_value(seed, k);
    const float b = bead_value(seed, k + 1);
    const float s = f * f * (3 - 2 * f);
    return a + (b - a) * s;
}

struct Target {
    Layer& layer;
    std::span<const float> depth;
};

bool hidden(const Target& target, std::size_t pixel, float depth) {
    return !target.depth.empty() && target.depth[pixel] < depth;
}

// A streak from its tail (a) to its head (b), `width` pixels wide at its head and
// tapering toward its tail, soft at its edges, its opacity gathering toward the head
// and, if frayed, broken into beads along its `length` metres.
// A thin streak running mostly up or down (most falling water): walked down its rows,
// each row touching only the few pixels across it, with the same soft edge, taper,
// head and fray as the general case.
void draw_thin_streak(Target& target, Projected a, Projected b, float half, float alpha, Shade shade, const Splat& settings,
                      const float* along, int bead_count) {
    Layer& layer = target.layer;
    const float head = settings.head;
    const float vx = b.x - a.x, vy = b.y - a.y;
    const float length = std::sqrt(vx * vx + vy * vy);
    // Horizontal distance to perpendicular distance.
    const float across = std::fabs(vy) / length;
    const float y_low = std::min(a.y, b.y), y_high = std::max(a.y, b.y);
    const int top = std::max(0, static_cast<int>(std::floor(y_low - half - 1)));
    const int bottom = std::min(layer.height - 1, static_cast<int>(std::ceil(y_high + half + 1)));
    const float depth = b.depth;
    int box_left = layer.width, box_right = -1;
    for (int y = top; y <= bottom; ++y) {
        const float py = static_cast<float>(y) + .5f;
        const float u = std::clamp((py - a.y) / vy, 0.0f, 1.0f);
        const float cx = a.x + vx * u;
        // Beyond the streak's ends, the distance past the end counts too.
        const float beyond = py < y_low ? y_low - py : (py > y_high ? py - y_high : 0.0f);
        const float half_here = half * (settings.taper + (1 - settings.taper) * u);
        const float soft = std::max(.5f, half_here * settings.softness);
        const float inner = half_here - soft;
        const float span = half_here + .5f - inner;
        const int x0 = std::max(0, static_cast<int>(std::floor(cx - half_here - 1)));
        const int x1 = std::min(layer.width - 1, static_cast<int>(std::ceil(cx + half_here + 1)));
        const float at = u * static_cast<float>(bead_count - 1);
        const int k = std::min(bead_count - 2, static_cast<int>(at));
        const float frayed = along[k] + (along[k + 1] - along[k]) * (at - static_cast<float>(k));
        const float row_cover = alpha * ((1 - head) + head * u * u) * frayed;
        for (int x = x0; x <= x1; ++x) {
            const float dx = (static_cast<float>(x) + .5f - cx) * across;
            const float distance = beyond > 0 ? std::sqrt(dx * dx + beyond * beyond) : std::fabs(dx);
            const float ramp = std::clamp((half_here + .5f - distance) / span, 0.0f, 1.0f);
            if (ramp <= 0)
                continue;
            const std::size_t pixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(layer.width) + static_cast<std::size_t>(x);
            if (hidden(target, pixel, depth))
                continue;
            const float cover = std::min(.98f, row_cover * ramp * ramp * (3 - 2 * ramp));
            layer.colour[pixel * 3] += shade.r * cover;
            layer.colour[pixel * 3 + 1] += shade.g * cover;
            layer.colour[pixel * 3 + 2] += shade.b * cover;
            layer.coverage[pixel] += cover;
            layer.through[pixel] *= 1 - cover;
            box_left = std::min(box_left, x);
            box_right = std::max(box_right, x);
        }
    }
    if (box_left <= box_right)
        layer.mark(box_left, top, box_right + 1, bottom + 1);
}

void draw_streak(Target& target, Projected a, Projected b, float width, Shade shade, const Splat& settings,
                 std::uint32_t seed, float length) {
    const float head = settings.head;
    Layer& layer = target.layer;
    // A streak thinner than a pixel covers it in proportion.
    float alpha = shade.alpha;
    if (width < 1) {
        alpha *= width;
        width = 1;
    }
    const float half = width * .5f;
    if (width <= 3 && std::fabs(b.y - a.y) >= 2 && std::fabs(b.y - a.y) >= 2 * std::fabs(b.x - a.x)) {
        constexpr int beads_along = 5;
        float along[beads_along];
        for (int k = 0; k < beads_along; ++k) {
            float frayed = 1;
            if (settings.fray > 0) {
                const float n = beads(seed, static_cast<float>(k) / (beads_along - 1) * length / settings.fray_length);
                frayed = 1 - settings.fray * (1 - n * n);
            }
            along[k] = frayed;
        }
        draw_thin_streak(target, a, b, half, alpha, shade, settings, along, beads_along);
        return;
    }
    const float x0 = std::min(a.x, b.x) - half - 1, x1 = std::max(a.x, b.x) + half + 1;
    const float y0 = std::min(a.y, b.y) - half - 1, y1 = std::max(a.y, b.y) + half + 1;
    const int left = std::max(0, static_cast<int>(std::floor(x0)));
    const int right = std::min(layer.width - 1, static_cast<int>(std::ceil(x1)));
    const int top = std::max(0, static_cast<int>(std::floor(y0)));
    const int bottom = std::min(layer.height - 1, static_cast<int>(std::ceil(y1)));
    if (left > right || top > bottom)
        return;
    layer.mark(left, top, right + 1, bottom + 1);
    const float vx = b.x - a.x, vy = b.y - a.y;
    const float length2 = vx * vx + vy * vy;
    const float inverse_length2 = length2 > 1e-6f ? 1 / length2 : 0.0f;
    const float reach2 = (half + .5f) * (half + .5f);
    const float depth = b.depth;
    // The fray along the streak, worked out at a few points and read between them.
    constexpr int bead_count = 5;
    float along[bead_count];
    for (int k = 0; k < bead_count; ++k) {
        float frayed = 1;
        if (settings.fray > 0) {
            const float n = beads(seed, static_cast<float>(k) / (bead_count - 1) * length / settings.fray_length);
            frayed = 1 - settings.fray * (1 - n * n);
        }
        along[k] = frayed;
    }
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            const float px = static_cast<float>(x) + .5f - a.x;
            const float py = static_cast<float>(y) + .5f - a.y;
            const float u = length2 > 1e-6f ? std::clamp((px * vx + py * vy) * inverse_length2, 0.0f, 1.0f) : 1.0f;
            const float dx = px - vx * u, dy = py - vy * u;
            const float distance2 = dx * dx + dy * dy;
            if (distance2 > reach2)
                continue;
            const float distance = std::sqrt(distance2);
            const float half_here = half * (settings.taper + (1 - settings.taper) * u);
            const float soft = std::max(.5f, half_here * settings.softness);
            const float inner = half_here - soft;
            const float ramp = std::clamp((half_here + .5f - distance) / (half_here + .5f - inner), 0.0f, 1.0f);
            const float edge = ramp * ramp * (3 - 2 * ramp);
            if (edge <= 0)
                continue;
            float frayed = 1;
            if (settings.fray > 0) {
                const float at = u * static_cast<float>(bead_count - 1);
                const int k = std::min(bead_count - 2, static_cast<int>(at));
                const float f = at - static_cast<float>(k);
                frayed = along[k] + (along[k + 1] - along[k]) * f;
            }
            const std::size_t pixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(layer.width) +
                                      static_cast<std::size_t>(x);
            if (hidden(target, pixel, depth))
                continue;
            const float profile = (1 - head) + head * u * u;
            const float cover = std::min(.98f, alpha * profile * edge * frayed);
            layer.colour[pixel * 3] += shade.r * cover;
            layer.colour[pixel * 3 + 1] += shade.g * cover;
            layer.colour[pixel * 3 + 2] += shade.b * cover;
            layer.coverage[pixel] += cover;
            layer.through[pixel] *= 1 - cover;
        }
}

// A Gaussian's falloff, exp(-x), for x from 0 to 4.5 in 256 steps.
struct Falloff {
    std::array<float, 257> values{};
    Falloff() {
        for (std::size_t i = 0; i < values.size(); ++i)
            values[i] = std::exp(-4.5f * static_cast<float>(i) / 256);
    }
    float at(float x) const {
        if (x >= 4.5f)
            return 0;
        return values[static_cast<std::size_t>(x * (256 / 4.5f))];
    }
};

// A glow wider than a few pixels, on the coarse grid (a quarter the resolution).
void draw_coarse_glow(Target& target, Projected at, float radius, Shade shade) {
    static const Falloff falloff;
    Layer& layer = target.layer;
    constexpr float step = Layer::coarse_step;
    const float cx = at.x / step, cy = at.y / step, r = std::min(radius / step, 100.0f);
    const float sigma = r * .5f;
    const float reach = r * 1.5f;
    const int left = std::max(0, static_cast<int>(std::floor(cx - reach)));
    const int right = std::min(layer.coarse_width - 1, static_cast<int>(std::ceil(cx + reach)));
    const int top = std::max(0, static_cast<int>(std::floor(cy - reach)));
    const int bottom = std::min(layer.coarse_height - 1, static_cast<int>(std::ceil(cy + reach)));
    if (left > right || top > bottom)
        return;
    // The fine pixels it reaches once smoothed back up.
    layer.mark_glow((left - 1) * Layer::coarse_step, (top - 1) * Layer::coarse_step, (right + 2) * Layer::coarse_step,
                    (bottom + 2) * Layer::coarse_step);
    // Each coarse pixel stands for step * step fine ones: its share of the energy.
    const float norm = shade.alpha / (6.2831853f * sigma * sigma * step * step);
    const float inverse = 1 / (2 * sigma * sigma);
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            const float dx = static_cast<float>(x) + .5f - cx;
            const float dy = static_cast<float>(y) + .5f - cy;
            const float weight = falloff.at((dx * dx + dy * dy) * inverse) * norm;
            if (weight <= 0)
                continue;
            const std::size_t fine = static_cast<std::size_t>(std::min(layer.height - 1, y * Layer::coarse_step)) *
                                         static_cast<std::size_t>(layer.width) +
                                     static_cast<std::size_t>(std::min(layer.width - 1, x * Layer::coarse_step));
            if (hidden(target, fine, at.depth))
                continue;
            const std::size_t pixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(layer.coarse_width) + static_cast<std::size_t>(x);
            layer.coarse[pixel * 3] += shade.r * weight;
            layer.coarse[pixel * 3 + 1] += shade.g * weight;
            layer.coarse[pixel * 3 + 2] += shade.b * weight;
        }
}

// Light added in a soft disc of `radius` pixels (a Gaussian, its energy kept).
void draw_glow(Target& target, Projected at, float radius, Shade shade) {
    static const Falloff falloff;
    Layer& layer = target.layer;
    radius = std::clamp(radius, .5f, 400.0f);
    if (radius > 10 && layer.coarse_width > 0) {
        draw_coarse_glow(target, at, radius, shade);
        return;
    }
    const float sigma = radius * .5f;
    const float reach = radius * 1.5f;
    const int left = std::max(0, static_cast<int>(std::floor(at.x - reach)));
    const int right = std::min(layer.width - 1, static_cast<int>(std::ceil(at.x + reach)));
    const int top = std::max(0, static_cast<int>(std::floor(at.y - reach)));
    const int bottom = std::min(layer.height - 1, static_cast<int>(std::ceil(at.y + reach)));
    if (left > right || top > bottom)
        return;
    layer.mark(left, top, right + 1, bottom + 1);
    const float norm = shade.alpha / (6.2831853f * sigma * sigma);
    const float inverse = 1 / (2 * sigma * sigma);
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            const float dx = static_cast<float>(x) + .5f - at.x;
            const float dy = static_cast<float>(y) + .5f - at.y;
            const float weight = falloff.at((dx * dx + dy * dy) * inverse) * norm;
            if (weight <= 0)
                continue;
            const std::size_t pixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(layer.width) +
                                      static_cast<std::size_t>(x);
            if (hidden(target, pixel, at.depth))
                continue;
            layer.added[pixel * 3] += shade.r * weight;
            layer.added[pixel * 3 + 1] += shade.g * weight;
            layer.added[pixel * 3 + 2] += shade.b * weight;
        }
}

} // namespace

void Splatter::draw(const System& system, std::span<const Shade> shades, const Lens& lens,
                    std::span<const float> depth, const Splat& settings, Layer& layer) {
    if (layer.width != lens.width || layer.height != lens.height)
        layer.reset(lens.width, lens.height);
    const std::size_t pixels = static_cast<std::size_t>(lens.width) * static_cast<std::size_t>(lens.height);
    Target target{layer, depth.size() == pixels ? depth : std::span<const float>{}};
    const std::span<const Vec> positions = system.positions();
    const std::span<const Vec> velocities = system.velocities();
    const std::span<const float> sizes = system.sizes();
    const std::span<const float> tails = system.tails();
    const std::span<const Kind> kinds = system.kinds();
    const std::size_t count = std::min(system.count(), shades.size());
    for (std::size_t i = 0; i < count; ++i) {
        const Shade shade = shades[i];
        if (shade.alpha <= 0)
            continue;
        const Vec p = positions[i];
        const Projected head = project(lens, p);
        if (!head.in_front)
            continue;
        const float scale = head.scale;
        if (kinds[i] == Kind::streak) {
            const Vec v = velocities[i];
            const float back = tails[i] + settings.exposure;
            const Projected tail = project(lens, {p.x - v.x * back, p.y - v.y * back, p.z - v.z * back});
            if (!tail.in_front)
                continue;
            const float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z) * back;
            draw_streak(target, tail, head, sizes[i] * scale, shade, settings, system.seeds()[i], length);
        } else if (kinds[i] == Kind::mist) {
            draw_glow(target, head, sizes[i] * scale, shade);
        } else {
            draw_glow(target, head, settings.glint_size, shade);
        }
    }
}

void Splatter::compose(const Layer& layer, std::span<float> picture) {
    const std::size_t n = static_cast<std::size_t>(layer.width) * static_cast<std::size_t>(layer.height);
    if (picture.size() < n * 3)
        return;
    // The large glows, smoothed back up from the coarse grid (bilinear between coarse
    // pixel centres), where they fell.
    constexpr float step = Layer::coarse_step;
    if (layer.glow_left < layer.glow_right && layer.coarse.size() >= static_cast<std::size_t>(layer.coarse_width * layer.coarse_height) * 3)
        for (int y = layer.glow_top; y < layer.glow_bottom; ++y) {
            const float gy = std::clamp((static_cast<float>(y) + .5f) / step - .5f, 0.0f, static_cast<float>(layer.coarse_height - 1));
            const int y0 = static_cast<int>(gy);
            const int y1 = std::min(layer.coarse_height - 1, y0 + 1);
            const float fy = gy - static_cast<float>(y0);
            const std::size_t row0 = static_cast<std::size_t>(y0) * static_cast<std::size_t>(layer.coarse_width);
            const std::size_t row1 = static_cast<std::size_t>(y1) * static_cast<std::size_t>(layer.coarse_width);
            for (int x = layer.glow_left; x < layer.glow_right; ++x) {
                const float gx = std::clamp((static_cast<float>(x) + .5f) / step - .5f, 0.0f, static_cast<float>(layer.coarse_width - 1));
                const int x0 = static_cast<int>(gx);
                const int x1 = std::min(layer.coarse_width - 1, x0 + 1);
                const float fx = gx - static_cast<float>(x0);
                const std::size_t a = (row0 + static_cast<std::size_t>(x0)) * 3;
                const std::size_t b = (row0 + static_cast<std::size_t>(x1)) * 3;
                const std::size_t c = (row1 + static_cast<std::size_t>(x0)) * 3;
                const std::size_t d = (row1 + static_cast<std::size_t>(x1)) * 3;
                const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(layer.width) + static_cast<std::size_t>(x)) * 3;
                for (std::size_t k = 0; k < 3; ++k) {
                    const float top = layer.coarse[a + k] + (layer.coarse[b + k] - layer.coarse[a + k]) * fx;
                    const float low = layer.coarse[c + k] + (layer.coarse[d + k] - layer.coarse[c + k]) * fx;
                    picture[i + k] += top + (low - top) * fy;
                }
            }
        }
    // The streaks over what they cover, and the small glows' light.
    for (int y = layer.fine_top; y < layer.fine_bottom; ++y)
        for (int x = layer.fine_left; x < layer.fine_right; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(layer.width) + static_cast<std::size_t>(x);
            if (layer.coverage[i] > 1e-6f) {
                const float covered = 1 - layer.through[i];
                const float average = 1 / layer.coverage[i];
                for (std::size_t k = 0; k < 3; ++k)
                    picture[i * 3 + k] = picture[i * 3 + k] * layer.through[i] + layer.colour[i * 3 + k] * average * covered;
            }
            for (std::size_t k = 0; k < 3; ++k)
                picture[i * 3 + k] += layer.added[i * 3 + k];
        }
}

} // namespace particles
