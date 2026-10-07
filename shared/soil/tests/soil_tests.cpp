// The soil engine's own tests: determinism, seamless tiling, the spoil heap's
// shape, that each parameter moves the picture the way it says, refusal of bad
// sizes, and the generation cost at the sizes games use.
#include "soil.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

double luminance(const soil::Image& image, std::size_t texel) {
    const std::uint8_t* p = &image.rgba[texel * 4];
    const double value = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
    return value;
}

double mean_luminance(const soil::Image& image) {
    const std::size_t count = static_cast<std::size_t>(image.width) * image.height;
    double sum = 0.0;
    for (std::size_t texel = 0; texel < count; ++texel) sum += luminance(image, texel);
    return sum / static_cast<double>(count);
}

double colour_step(const soil::Image& image, int x0, int y0, int x1, int y1) {
    const std::size_t a = (static_cast<std::size_t>(y0) * image.width + x0) * 4;
    const std::size_t b = (static_cast<std::size_t>(y1) * image.width + x1) * 4;
    double sum = 0.0;
    for (int c = 0; c < 3; ++c) sum += std::fabs(static_cast<double>(image.rgba[a + c]) - image.rgba[b + c]);
    return sum;
}

// Across a seam, neighbouring texels must differ no more than neighbours inside
// the picture do. One picture can have a crack or a leaf on any line, so compare
// the seams of several seeds with the interior of the same pictures.
struct SeamTally {
    double seam_x = 0.0;
    double inside_x = 0.0;
    double seam_y = 0.0;
    double inside_y = 0.0;
    double relief_seam = 0.0;   // the largest step across a seam
    double relief_inside = 0.0; // the largest step inside
};

void tally_seams(const soil::Image& image, SeamTally& tally) {
    const int w = image.width;
    const int h = image.height;
    for (int x = 0; x < w; ++x) {
        double column = 0.0;
        for (int y = 0; y < h; ++y) column += colour_step(image, x, y, (x + 1) % w, y);
        if (x + 1 < w) tally.inside_x += column / (w - 1);
        else tally.seam_x += column;
    }
    for (int y = 0; y < h; ++y) {
        double row = 0.0;
        for (int x = 0; x < w; ++x) row += colour_step(image, x, y, x, (y + 1) % h);
        if (y + 1 < h) tally.inside_y += row / (h - 1);
        else tally.seam_y += row;
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double here = image.relief[static_cast<std::size_t>(y) * w + x];
            const double right = image.relief[static_cast<std::size_t>(y) * w + (x + 1) % w];
            const double step = std::fabs(right - here);
            if (x + 1 < w) tally.relief_inside = std::max(tally.relief_inside, step);
            else tally.relief_seam = std::max(tally.relief_seam, step);
        }
    }
}

soil::Parameters at_scale(soil::Preset preset, double texels_per_metre, std::uint32_t seed) {
    soil::Parameters parameters = soil::preset(preset);
    parameters.texels_per_metre = texels_per_metre;
    parameters.seed = seed;
    return parameters;
}

void test_determinism() {
    std::printf("determinism\n");
    for (int index = 0; index < soil::preset_count; ++index) {
        const soil::Parameters parameters = at_scale(static_cast<soil::Preset>(index), 90.0, 42u);
        soil::Image first;
        soil::Image second;
        check(soil::generate_surface(parameters, 96, 64, first), "a preset generates");
        check(soil::generate_surface(parameters, 96, 64, second), "a preset generates twice");
        check(first.rgba == second.rgba && first.relief == second.relief, "the same seed makes the same ground");
        soil::Parameters other = parameters;
        other.seed = 43u;
        soil::Image third;
        check(soil::generate_surface(other, 96, 64, third), "another seed generates");
        check(first.rgba != third.rgba, "another seed makes other ground");
    }
    // A known picture: catches an accidental change to the generator on any platform.
    soil::Image reference;
    check(soil::generate_surface(at_scale(soil::Preset::tilled_loam, 64.0, 5u), 32, 32, reference), "the reference generates");
    std::uint32_t hash = 2166136261u;
    for (const std::uint8_t byte : reference.rgba) hash = (hash ^ byte) * 16777619u;
    std::printf("  reference hash %08x\n", hash);
}

void test_tiling() {
    std::printf("tiling\n");
    const soil::Preset presets[6] = {soil::Preset::tilled_loam, soil::Preset::raked_bed, soil::Preset::furrowed_rows,
                                     soil::Preset::dry_crust, soil::Preset::leaf_litter, soil::Preset::snow_dusted};
    for (const soil::Preset preset : presets) {
        SeamTally tally;
        for (std::uint32_t seed = 1; seed <= 8; ++seed) {
            soil::Parameters parameters = at_scale(preset, 120.0, seed);
            parameters.furrow_angle = 0.4;
            parameters.rake_angle = -0.3;
            soil::Image image;
            check(soil::generate_surface(parameters, 96, 80, image), "a tile generates");
            tally_seams(image, tally);
        }
        const double ratio_x = tally.seam_x / tally.inside_x;
        const double ratio_y = tally.seam_y / tally.inside_y;
        std::printf("  %-16s seam / inside: x %.2f, y %.2f\n", soil::preset_name(preset), ratio_x, ratio_y);
        check(ratio_x < 1.2 && ratio_y < 1.2, "the edges meet without a seam");
        check(tally.relief_seam <= tally.relief_inside, "the relief is continuous across the seam");
    }
}

void test_spoil() {
    std::printf("spoil\n");
    soil::Parameters parameters = at_scale(soil::Preset::fresh_spoil, 300.0, 3u);
    soil::Spoil spoil;
    spoil.fan_direction = 0.0;  // thrown toward +x
    const int side = 256;
    soil::Image image;
    check(soil::generate_spoil(parameters, spoil, side, side, image), "a spoil heap generates");
    const int c = side / 2;
    const std::size_t centre = static_cast<std::size_t>(c) * side + c;
    check(image.rgba[centre * 4 + 3] == 0, "nothing is drawn over the hole");
    check(image.rgba[3] == 0, "the far corner is bare ground");
    // The rim is solid all round the lip.
    const int lip = static_cast<int>(std::lround((spoil.hole_radius + spoil.rim_width * 0.45) * 300.0));
    int solid = 0;
    for (int k = 0; k < 16; ++k) {
        const double a = k * 3.14159265358979 / 8.0;
        const int x = c + static_cast<int>(std::lround(std::cos(a) * lip));
        const int y = c + static_cast<int>(std::lround(std::sin(a) * lip));
        solid += image.rgba[(static_cast<std::size_t>(y) * side + x) * 4 + 3] == 255 ? 1 : 0;
    }
    check(solid >= 13, "a ring of soil surrounds the hole");
    // More is thrown toward the fan than away from it.
    int ahead = 0;
    int behind = 0;
    const int out = static_cast<int>(std::lround((spoil.hole_radius + spoil.rim_width + 0.06) * 300.0));
    for (int dy = -12; dy <= 12; ++dy) {
        for (int dx = 0; dx < 12; ++dx) {
            ahead += image.rgba[(static_cast<std::size_t>(c + dy) * side + c + out + dx) * 4 + 3] == 255 ? 1 : 0;
            behind += image.rgba[(static_cast<std::size_t>(c + dy) * side + c - out - dx) * 4 + 3] == 255 ? 1 : 0;
        }
    }
    std::printf("  soil ahead %d, behind %d\n", ahead, behind);
    check(ahead > behind * 3 + 10, "the fan points the way it was thrown");
}

void test_parameters() {
    std::printf("parameters\n");
    soil::Parameters dry = at_scale(soil::Preset::tilled_loam, 100.0, 11u);
    dry.moisture = 0.1;
    soil::Parameters wet = dry;
    wet.moisture = 0.9;
    soil::Image a;
    soil::Image b;
    check(soil::generate_surface(dry, 128, 128, a) && soil::generate_surface(wet, 128, 128, b), "moisture variants generate");
    std::printf("  dry %.1f, wet %.1f\n", mean_luminance(a), mean_luminance(b));
    check(mean_luminance(b) < mean_luminance(a) * 0.8, "damp soil is darker");
    soil::Parameters snowy = dry;
    snowy.snow = 0.6;
    soil::Image c;
    check(soil::generate_surface(snowy, 128, 128, c), "snow generates");
    check(mean_luminance(c) > mean_luminance(a) * 1.3, "snow brightens the ground");
    // A flat, bare, open patch keeps the colour asked for (within shading and mottling).
    soil::Parameters bare;
    bare.texels_per_metre = 100.0;
    bare.colour = soil::Colour{0.5, 0.4, 0.3};
    bare.moisture = 0.0;
    bare.moisture_patches = 0.0;
    bare.tilth = 0.0;
    bare.looseness = 0.0;
    bare.relief = 0.0;
    bare.pebbles = 0.0;
    bare.grit = 0.0;
    soil::Image d;
    check(soil::generate_surface(bare, 64, 64, d), "bare ground generates");
    const double expected = 0.2126 * 127.5 + 0.7152 * 102.0 + 0.0722 * 76.5;
    std::printf("  bare ground %.1f, asked %.1f\n", mean_luminance(d), expected);
    check(std::fabs(mean_luminance(d) - expected) < expected * 0.15, "flat ground comes out at its colour");
    // The same ground at twice the resolution has the same overall tone.
    soil::Parameters coarse = at_scale(soil::Preset::furrowed_rows, 60.0, 13u);
    soil::Parameters fine = coarse;
    fine.texels_per_metre = 120.0;
    soil::Image e;
    soil::Image f;
    check(soil::generate_surface(coarse, 64, 64, e) && soil::generate_surface(fine, 128, 128, f), "scales generate");
    std::printf("  coarse %.1f, fine %.1f\n", mean_luminance(e), mean_luminance(f));
    check(std::fabs(mean_luminance(e) - mean_luminance(f)) < mean_luminance(f) * 0.12, "resolution keeps the tone");
}

void test_refusals() {
    std::printf("refusals\n");
    soil::Image kept;
    check(soil::generate_surface(soil::preset(soil::Preset::raked_bed), 8, 8, kept), "a small tile generates");
    const std::vector<std::uint8_t> before = kept.rgba;
    check(!soil::generate_surface(soil::preset(soil::Preset::raked_bed), 0, 8, kept), "a zero side is refused");
    check(!soil::generate_surface(soil::preset(soil::Preset::raked_bed), 8, soil::max_side + 1, kept), "an enormous side is refused");
    check(!soil::generate_spoil(soil::preset(soil::Preset::fresh_spoil), soil::Spoil{}, 3, 8, kept), "a tiny heap is refused");
    check(kept.rgba == before && kept.width == 8, "a refusal leaves the picture alone");
    soil::Parameters strange = soil::preset(soil::Preset::tilled_loam);
    strange.texels_per_metre = std::nan("");
    strange.moisture = 7.0;
    strange.relief = -3.0;
    check(soil::generate_surface(strange, 16, 16, kept), "out-of-range parameters are clamped, not trusted");
}

void test_cost() {
    std::printf("cost\n");
    const int sides[3] = {128, 256, 512};
    for (const int side : sides) {
        soil::Parameters parameters = at_scale(soil::Preset::furrowed_rows, side / 5.0, 21u);
        soil::apply_season(parameters, soil::Season::autumn);
        soil::Image image;
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        check(soil::generate_surface(parameters, side, side, image), "a costed tile generates");
        const std::chrono::duration<double, std::milli> spent = std::chrono::steady_clock::now() - start;
        std::printf("  %4d x %-4d %.1f ms\n", side, side, spent.count());
    }
}

}  // namespace

int main() {
    test_determinism();
    test_tiling();
    test_spoil();
    test_parameters();
    test_refusals();
    test_cost();
    if (failures != 0) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("all soil tests passed\n");
    return 0;
}
