#pragma once
// Seeded noise for the stones' shapes and minerals: an integer hash, value noise in two
// and three dimensions with a quintic fade, and its octave sums. Everything follows from
// the seed; nothing here decides a rule of the game, only how a stone looks.
#include <cmath>
#include <cstdint>

namespace ts {

[[nodiscard]] inline std::uint32_t hash_bits(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

[[nodiscard]] inline std::uint32_t hash_cell(int x, int y, int z, std::uint32_t seed) {
    std::uint32_t h = seed * 0x9e3779b9U;
    h = hash_bits(h ^ static_cast<std::uint32_t>(x) * 0x85ebca6bU);
    h = hash_bits(h ^ static_cast<std::uint32_t>(y) * 0xc2b2ae35U);
    h = hash_bits(h ^ static_cast<std::uint32_t>(z) * 0x27d4eb2fU);
    return h;
}

// -1..1
[[nodiscard]] inline double hash_signed(int x, int y, int z, std::uint32_t seed) {
    const std::uint32_t h = hash_cell(x, y, z, seed);
    return static_cast<double>(h >> 8) * (2.0 / 16777216.0) - 1.0;
}

[[nodiscard]] inline double fade(double t) {
    return t * t * t * (t * (t * 6 - 15) + 10);
}

// Value noise, -1..1, smooth.
[[nodiscard]] inline double noise3(double x, double y, double z, std::uint32_t seed) {
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const double fz = std::floor(z);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const int iz = static_cast<int>(fz);
    const double tx = fade(x - fx);
    const double ty = fade(y - fy);
    const double tz = fade(z - fz);
    const double c000 = hash_signed(ix, iy, iz, seed);
    const double c100 = hash_signed(ix + 1, iy, iz, seed);
    const double c010 = hash_signed(ix, iy + 1, iz, seed);
    const double c110 = hash_signed(ix + 1, iy + 1, iz, seed);
    const double c001 = hash_signed(ix, iy, iz + 1, seed);
    const double c101 = hash_signed(ix + 1, iy, iz + 1, seed);
    const double c011 = hash_signed(ix, iy + 1, iz + 1, seed);
    const double c111 = hash_signed(ix + 1, iy + 1, iz + 1, seed);
    const double x00 = c000 + (c100 - c000) * tx;
    const double x10 = c010 + (c110 - c010) * tx;
    const double x01 = c001 + (c101 - c001) * tx;
    const double x11 = c011 + (c111 - c011) * tx;
    const double y0 = x00 + (x10 - x00) * ty;
    const double y1 = x01 + (x11 - x01) * ty;
    return y0 + (y1 - y0) * tz;
}

// Value noise in the plane, -1..1: four corners instead of eight.
[[nodiscard]] inline double noise2(double x, double y, std::uint32_t seed) {
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const double tx = fade(x - fx);
    const double ty = fade(y - fy);
    const double c00 = hash_signed(ix, iy, 0, seed);
    const double c10 = hash_signed(ix + 1, iy, 0, seed);
    const double c01 = hash_signed(ix, iy + 1, 0, seed);
    const double c11 = hash_signed(ix + 1, iy + 1, 0, seed);
    const double a = c00 + (c10 - c00) * tx;
    const double b = c01 + (c11 - c01) * tx;
    return a + (b - a) * ty;
}

// Octaves of noise3, each twice the frequency and `gain` the amplitude of the last;
// about -1..1.
[[nodiscard]] inline double fbm3(double x, double y, double z, std::uint32_t seed, int octaves,
                                 double gain = .5) {
    double sum = 0;
    double amplitude = 1;
    double total = 0;
    double f = 1;
    for (int octave = 0; octave < octaves; ++octave) {
        sum += amplitude * noise3(x * f, y * f, z * f, seed + static_cast<std::uint32_t>(octave) * 101U);
        total += amplitude;
        amplitude *= gain;
        f *= 2.03;
    }
    return sum / total;
}

[[nodiscard]] inline double fbm2(double x, double y, std::uint32_t seed, int octaves, double gain = .5) {
    double sum = 0;
    double amplitude = 1;
    double total = 0;
    double f = 1;
    for (int octave = 0; octave < octaves; ++octave) {
        sum += amplitude * noise2(x * f, y * f, seed + static_cast<std::uint32_t>(octave) * 101U);
        total += amplitude;
        amplitude *= gain;
        f *= 2.03;
    }
    return sum / total;
}

// A splitmix64 step: the stones' and the rules' random numbers.
[[nodiscard]] inline std::uint64_t next_random(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

// 0..1, from the top 53 bits.
[[nodiscard]] inline double next_unit(std::uint64_t& state) {
    return static_cast<double>(next_random(state) >> 11) * (1.0 / 9007199254740992.0);
}

[[nodiscard]] inline double next_between(std::uint64_t& state, double low, double high) {
    return low + (high - low) * next_unit(state);
}

// 0 .. count - 1
[[nodiscard]] inline int next_below(std::uint64_t& state, int count) {
    return static_cast<int>(next_random(state) % static_cast<std::uint64_t>(count));
}

}  // namespace ts
