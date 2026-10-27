#include "caustics.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace ambient {
namespace {

constexpr int table_size = 4096;  // cosine table entries per cycle, a power of two
constexpr double two_pi = 6.283185307179586;

struct Wave {
    int kx{};
    int ky{};
    int cycles{};        // whole cycles per loop, so the loop closes
    double amplitude{};  // surface height, tile units
    double phase{};      // 0..1
};

// A small deterministic generator, so a scene's pattern is the same everywhere.
struct Mixer {
    std::uint32_t state{};
    std::uint32_t next() {
        state = state * 1664525U + 1013904223U;
        std::uint32_t x = state;
        x ^= x >> 16U;
        x *= 0x7FEB352DU;
        x ^= x >> 15U;
        return x;
    }
    double unit() {
        const double result = static_cast<double>(next() & 0xFFFFFFU) / 16777216.0;
        return result;
    }
};

std::vector<Wave> make_waves(std::uint32_t seed) {
    std::vector<Wave> waves{};
    Mixer mixer{seed * 2654435761U + 12345U};
    while (waves.size() < 14) {
        const int kx = static_cast<int>(mixer.next() % 15U) - 7;
        const int ky = static_cast<int>(mixer.next() % 15U) - 7;
        const double radius = std::sqrt(static_cast<double>(kx * kx + ky * ky));
        if (radius < 1.4 || radius > 7.2)
            continue;
        Wave wave{};
        wave.kx = kx;
        wave.ky = ky;
        // Longer waves move more slowly, as on water; one or two cycles per loop.
        wave.cycles = (radius > 4.0 ? 2 : 1) * ((mixer.next() & 1U) != 0 ? 1 : -1);
        wave.amplitude = 1.0 / std::pow(radius, 1.75);
        wave.phase = mixer.unit();
        waves.push_back(wave);
    }
    return waves;
}

// Box blur with wrapping, along rows then columns.
void blur(std::vector<float>& values, int size, int radius) {
    std::vector<float> scratch(values.size());
    const float inverse = 1.0F / static_cast<float>(2 * radius + 1);
    const int mask = size - 1;
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                float sum = 0;
                for (int d = -radius; d <= radius; ++d) {
                    const int sx = pass == 0 ? ((x + d) & mask) : x;
                    const int sy = pass == 0 ? y : ((y + d) & mask);
                    sum += values[static_cast<std::size_t>(sy) * static_cast<std::size_t>(size) +
                                  static_cast<std::size_t>(sx)];
                }
                scratch[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)] =
                    sum * inverse;
            }
        }
        values.swap(scratch);
    }
}

std::uint32_t level(float value) {
    const std::uint32_t result = static_cast<std::uint32_t>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F);
    return result;
}

} // namespace

CausticField::CausticField(const CausticSpec& spec, Vec3 light) {
    const int size = std::clamp(spec.size, 16, 256);
    if (!std::has_single_bit(static_cast<unsigned>(size)) || spec.frames < 2 || spec.period <= 0 || spec.tile <= 0)
        return;
    size_ = size;
    mask_ = size - 1;
    frames_ = spec.frames;
    period_ = spec.period;
    texels_per_unit_ = static_cast<float>(size) / spec.tile;
    cos_angle_ = std::cos(spec.angle);
    sin_angle_ = std::sin(spec.angle);
    const Vec3 toward = normalize(light);
    const float up = std::max(toward.y, 0.2F);
    slant_ = {toward.x / up, 0, toward.z / up};
    surface_ = spec.surface;
    floor_ = std::min(spec.floor, spec.surface - 0.01F);
    floor_gain_ = std::clamp(spec.floor_gain, 0.0F, 1.0F);

    std::vector<double> cosine(table_size);
    for (int index = 0; index < table_size; ++index)
        cosine[static_cast<std::size_t>(index)] = std::cos(two_pi * index / table_size);
    const std::vector<Wave> waves = make_waves(spec.seed);
    // Two photons per texel side: enough that the web's thinnest lines are continuous.
    const int photons = size * 2;
    const int step = table_size / photons;
    const std::size_t texels = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    data_.assign(texels * static_cast<std::size_t>(frames_), 0);
    std::vector<float> gathered(texels);
    std::vector<float> soft(texels);
    const double photon_weight = static_cast<double>(size) * size / (static_cast<double>(photons) * photons);
    const int mask = size - 1;
    for (int frame = 0; frame < frames_; ++frame) {
        const double t = static_cast<double>(frame) / frames_;
        std::fill(gathered.begin(), gathered.end(), 0.0F);
        std::vector<int> offsets(waves.size());
        std::vector<double> gains(waves.size());
        for (std::size_t w = 0; w < waves.size(); ++w) {
            const double cycle = waves[w].cycles * t + waves[w].phase;
            offsets[w] = static_cast<int>(std::lround((cycle - std::floor(cycle)) * table_size));
            // d/dx of a sin(2 pi k.x) is 2 pi k a cos(...), in tile units.
            gains[w] = two_pi * waves[w].amplitude * spec.focus * size;
        }
        for (int j = 0; j < photons; ++j) {
            for (int i = 0; i < photons; ++i) {
                double gx = 0;
                double gy = 0;
                for (std::size_t w = 0; w < waves.size(); ++w) {
                    const int index = (waves[w].kx * i * step + waves[w].ky * j * step + offsets[w]) & (table_size - 1);
                    const double c = cosine[static_cast<std::size_t>(index)] * gains[w];
                    gx += c * waves[w].kx;
                    gy += c * waves[w].ky;
                }
                // Where the photon lands, in texels, and a bilinear splat there.
                const double x = (static_cast<double>(i) + 0.5) * 0.5 - gx;
                const double y = (static_cast<double>(j) + 0.5) * 0.5 - gy;
                const double fx = std::floor(x);
                const double fy = std::floor(y);
                const float ax = static_cast<float>(x - fx);
                const float ay = static_cast<float>(y - fy);
                const int x0 = static_cast<int>(fx) & mask;
                const int y0 = static_cast<int>(fy) & mask;
                const int x1 = (x0 + 1) & mask;
                const int y1 = (y0 + 1) & mask;
                const std::size_t row0 = static_cast<std::size_t>(y0) * static_cast<std::size_t>(size);
                const std::size_t row1 = static_cast<std::size_t>(y1) * static_cast<std::size_t>(size);
                gathered[row0 + static_cast<std::size_t>(x0)] += (1 - ax) * (1 - ay);
                gathered[row0 + static_cast<std::size_t>(x1)] += ax * (1 - ay);
                gathered[row1 + static_cast<std::size_t>(x0)] += (1 - ax) * ay;
                gathered[row1 + static_cast<std::size_t>(x1)] += ax * ay;
            }
        }
        for (float& value : gathered)
            value *= static_cast<float>(photon_weight);  // 1 where the light is even
        soft = gathered;
        blur(soft, size, 1);
        std::uint16_t* out = data_.data() + texels * static_cast<std::size_t>(frame);
        for (std::size_t index = 0; index < texels; ++index) {
            // Below the mean is shade between the lines; the lines saturate gently.
            const float lifted = std::max(0.0F, gathered[index] - 0.75F) * 0.5F;
            const float sharp = lifted / (1.0F + lifted * 0.35F) * 1.35F;
            const float blurred = std::max(0.0F, soft[index] - 0.8F) * 0.75F;
            out[index] = static_cast<std::uint16_t>(level(sharp) | (level(blurred) << 8U));
        }
    }
}

void CausticField::texel_of(Vec3 world, float& u, float& v) const {
    // Straight up the light to the surface, then into the turned tile.
    const float rise = surface_ - world.y;
    const float x = world.x + slant_.x * rise;
    const float z = world.z + slant_.z * rise;
    u = (x * cos_angle_ - z * sin_angle_) * texels_per_unit_;
    v = (x * sin_angle_ + z * cos_angle_) * texels_per_unit_;
}

std::uint32_t CausticField::tap_bits(float height, float sharpness) const {
    const float depth = std::clamp((surface_ - height) / (surface_ - floor_), 0.0F, 1.0F);
    // Sharp just under the surface, softer toward the floor, where the lines have
    // spread (the floor keeps a little crispness, as in a shallow tank).
    const std::uint32_t sharp = level((1.0F - depth * 0.35F) * sharpness);
    const std::uint32_t gain = level(mix(1.0F, floor_gain_, depth));
    const std::uint32_t result = (sharp << 16U) | (gain << 24U);
    return result;
}

std::uint32_t CausticField::tap_texel(float u, float v, float height, float sharpness) const {
    if (frames_ == 0)
        return 0;
    const std::uint32_t result = texel_index(u, v) | tap_bits(height, sharpness);
    return result;
}

std::uint32_t CausticField::tap(Vec3 world) const {
    float u = 0;
    float v = 0;
    texel_of(world, u, v);
    const std::uint32_t result = tap_texel(u, v, world.y);
    return result;
}

CausticField::Phase CausticField::phase(float time) const {
    Phase result{};
    if (frames_ == 0) {
        static const std::uint16_t dark[256 * 256] = {};
        result.first = dark;
        result.second = dark;
        return result;
    }
    const double cycle = static_cast<double>(time) / period_;
    const double position = (cycle - std::floor(cycle)) * frames_;
    const int first = std::min(static_cast<int>(position), frames_ - 1);
    const int second = (first + 1) % frames_;
    const std::size_t texels = static_cast<std::size_t>(size_) * static_cast<std::size_t>(size_);
    result.first = data_.data() + texels * static_cast<std::size_t>(first);
    result.second = data_.data() + texels * static_cast<std::size_t>(second);
    result.blend = static_cast<std::uint32_t>(std::clamp((position - first) * 256.0, 0.0, 256.0));
    return result;
}

std::uint16_t CausticField::texel(int frame, int index) const {
    if (frame < 0 || frame >= frames_ || index < 0 || index >= size_ * size_)
        return 0;
    const std::uint16_t result =
        data_[static_cast<std::size_t>(frame) * static_cast<std::size_t>(size_) * static_cast<std::size_t>(size_) +
              static_cast<std::size_t>(index)];
    return result;
}

} // namespace ambient
