// Canopy Falls drawn and heard headless: that a size bakes the same whatever order its
// bands are baked in, that the picture is the scene (blue sky above, the wooden deck below) and
// moves from frame to frame, that the water's particles stay inside their pool, and
// that the forest's sound is finite, inside full scale, not silent, and the same for
// the same seed.
#include "falls_voice.hpp"
#include "still.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

cf::Look look() {
    cf::Look l;
    l.sky.sun_elevation = 46 * cf::pi / 180;
    l.sky.haze = 2.0;
    return l;
}

// A small picture of the falls, its bands baked in order or backward.
std::vector<std::uint32_t> picture(bool backward, int frames) {
    cf::LiveFalls falls(look(), cf::Camera{});
    falls.set_density(.5);
    falls.prepare_once();
    falls.begin_size(96, 64, 2);
    const int bands = falls.bands();
    for (int k = 0; k < bands; ++k)
        falls.bake_band(backward ? bands - 1 - k : k);
    falls.finish_size();
    falls.adopt();
    for (int f = 0; f < frames; ++f)
        falls.advance(1.0 / 24);
    std::vector<std::uint32_t> out;
    falls.draw(out);
    return out;
}

double luminance(std::uint32_t word) {
    return .2126 * ((word >> 16) & 255) + .7152 * ((word >> 8) & 255) + .0722 * (word & 255);
}

double rows_mean(const std::vector<std::uint32_t>& p, int first, int last) {
    double sum = 0;
    for (int y = first; y < last; ++y)
        for (int x = 0; x < 96; ++x)
            sum += luminance(p[static_cast<std::size_t>(y * 96 + x)]);
    return sum / ((last - first) * 96);
}

void baking_and_frames() {
    const std::vector<std::uint32_t> a = picture(false, 12);
    const std::vector<std::uint32_t> b = picture(true, 12);
    check(a.size() == 96U * 64U, "a frame is the size baked");
    check(a == b, "the same picture whatever order the bands are baked in");
    const double sky = rows_mean(a, 0, 8);
    const double deck = rows_mean(a, 56, 64);
    check(sky > 60, "the top of the picture is the bright sky");
    std::uint32_t word = a[static_cast<std::size_t>(2 * 96 + 10)];
    check(((word & 255) > ((word >> 16) & 255)), "the sky is blue");
    const std::uint32_t wood = a[static_cast<std::size_t>(62 * 96 + 48)];
    check(((wood >> 16) & 255) > (wood & 255), "the deck at the bottom is warm wood, not sky");
    // The water moves: later frames differ in the falls.
    const std::vector<std::uint32_t> later = picture(false, 36);
    int changed = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        changed += a[i] != later[i] ? 1 : 0;
    check(changed > 40, "the falls move from frame to frame");
    std::printf("falls: sky %.0f, deck %.0f, %d pixels moved in a second\n", sky, deck, changed);
}

void particles_in_their_pool() {
    cf::LiveFalls falls(look(), cf::Camera{});
    falls.set_density(1);
    falls.prepare_once();
    falls.begin_size(48, 32, 1);
    for (int k = 0; k < falls.bands(); ++k)
        falls.bake_band(k);
    falls.finish_size();
    falls.adopt();
    for (int f = 0; f < 48; ++f)
        falls.advance(1.0 / 24);
    const std::size_t count = falls.particle_count();
    check(count > 1000, "the falls throw water");
    check(count < 40000, "inside the particles' pool");
    std::printf("falls: %zu particles after two seconds at full detail\n", count);
}

void the_forest_heard() {
    std::vector<float> first(static_cast<std::size_t>(cf::FallsVoice::sample_rate) * 6 * 2, 0.0f);
    std::vector<float> second = first;
    cf::FallsVoice a(3), b(3);
    for (std::size_t at = 0; at < first.size(); at += 1024) {
        const std::size_t n = std::min<std::size_t>(1024, first.size() - at);
        a.render_add(std::span<float>(first.data() + at, n), 1.0);
    }
    for (std::size_t at = 0; at < second.size(); at += 700) {
        const std::size_t n = std::min<std::size_t>(700, second.size() - at);
        b.render_add(std::span<float>(second.data() + at, n), 1.0);
    }
    double peak = 0, power = 0;
    bool finite = true;
    for (const float v : first) {
        finite = finite && std::isfinite(v);
        peak = std::max(peak, static_cast<double>(std::fabs(v)));
        power += static_cast<double>(v) * v;
    }
    const double rms = std::sqrt(power / static_cast<double>(first.size()));
    check(finite, "the forest's sound is finite");
    check(peak < .95, "inside full scale");
    check(rms > .005, "not silent");
    check(first == second, "the same seed sounds the same whatever the blocks");
    std::printf("forest: peak %.2f, rms %.3f\n", peak, rms);
}

} // namespace

int main() {
    baking_and_frames();
    particles_in_their_pool();
    the_forest_heard();
    if (failures == 0)
        std::printf("canopyfalls: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
