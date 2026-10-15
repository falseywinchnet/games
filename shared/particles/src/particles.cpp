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
    const float angle = std::atan2(side, ahead);
    const float b = lens.bend;
    const float c = std::cos(angle);
    if (b + c <= .05f)
        return out;
    const float radius = (b + 1) * lens.focal * std::sin(angle) / (b + c);
    const float ux = side > 1e-6f ? across / side : 0.0f;
    const float uy = side > 1e-6f ? upward / side : 0.0f;
    out.x = static_cast<float>(lens.width) * .5f + radius * ux;
    out.y = static_cast<float>(lens.height) * .5f - radius * uy;
    out.depth = distance;
    out.scale = (b + 1) * lens.focal * (b * c + 1) / ((b + c) * (b + c)) / distance;
    out.in_front = true;
    return out;
}

void Layer::reset(int w, int h) {
    width = w;
    height = h;
    const std::size_t n = static_cast<std::size_t>(std::max(0, w)) * static_cast<std::size_t>(std::max(0, h));
    colour.assign(n * 3, 0.0f);
    coverage.assign(n, 0.0f);
    through.assign(n, 1.0f);
    added.assign(n * 3, 0.0f);
}

namespace {

// Smooth noise along a line, 0..1, from a particle's seed.
float beads(std::uint32_t seed, float t) {
    const float cell = std::floor(t);
    const float f = t - cell;
    const std::uint32_t k = static_cast<std::uint32_t>(static_cast<std::int32_t>(cell));
    const float a = draw(seed, k + 101);
    const float b = draw(seed, k + 102);
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
    const float x0 = std::min(a.x, b.x) - half - 1, x1 = std::max(a.x, b.x) + half + 1;
    const float y0 = std::min(a.y, b.y) - half - 1, y1 = std::max(a.y, b.y) + half + 1;
    const int left = std::max(0, static_cast<int>(std::floor(x0)));
    const int right = std::min(layer.width - 1, static_cast<int>(std::ceil(x1)));
    const int top = std::max(0, static_cast<int>(std::floor(y0)));
    const int bottom = std::min(layer.height - 1, static_cast<int>(std::ceil(y1)));
    if (left > right || top > bottom)
        return;
    const float vx = b.x - a.x, vy = b.y - a.y;
    const float length2 = vx * vx + vy * vy;
    const float depth = b.depth;
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            const float px = static_cast<float>(x) + .5f - a.x;
            const float py = static_cast<float>(y) + .5f - a.y;
            const float u = length2 > 1e-6f ? std::clamp((px * vx + py * vy) / length2, 0.0f, 1.0f) : 1.0f;
            const float dx = px - vx * u, dy = py - vy * u;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float half_here = half * (settings.taper + (1 - settings.taper) * u);
            const float soft = std::max(.5f, half_here * settings.softness);
            const float inner = half_here - soft;
            const float ramp = std::clamp((half_here + .5f - distance) / (half_here + .5f - inner), 0.0f, 1.0f);
            const float edge = ramp * ramp * (3 - 2 * ramp);
            if (edge <= 0)
                continue;
            float frayed = 1;
            if (settings.fray > 0) {
                const float n = beads(seed, u * length / settings.fray_length);
                frayed = 1 - settings.fray * (1 - n * n);
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

// Light added in a soft disc of `radius` pixels (a Gaussian, its energy kept).
void draw_glow(Target& target, Projected at, float radius, Shade shade) {
    Layer& layer = target.layer;
    radius = std::clamp(radius, .5f, 400.0f);
    const float sigma = radius * .5f;
    const float reach = radius * 1.5f;
    const int left = std::max(0, static_cast<int>(std::floor(at.x - reach)));
    const int right = std::min(layer.width - 1, static_cast<int>(std::ceil(at.x + reach)));
    const int top = std::max(0, static_cast<int>(std::floor(at.y - reach)));
    const int bottom = std::min(layer.height - 1, static_cast<int>(std::ceil(at.y + reach)));
    if (left > right || top > bottom)
        return;
    const float norm = shade.alpha / (6.2831853f * sigma * sigma);
    const float inverse = 1 / (2 * sigma * sigma);
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            const float dx = static_cast<float>(x) + .5f - at.x;
            const float dy = static_cast<float>(y) + .5f - at.y;
            const float weight = std::exp(-(dx * dx + dy * dy) * inverse) * norm;
            if (weight < 1e-7f)
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
    for (std::size_t i = 0; i < n; ++i) {
        const float covered = 1 - layer.through[i];
        if (layer.coverage[i] > 1e-6f) {
            const float average = 1 / layer.coverage[i];
            for (std::size_t k = 0; k < 3; ++k)
                picture[i * 3 + k] = picture[i * 3 + k] * layer.through[i] + layer.colour[i * 3 + k] * average * covered;
        }
        for (std::size_t k = 0; k < 3; ++k)
            picture[i * 3 + k] += layer.added[i * 3 + k];
    }
}

} // namespace particles
