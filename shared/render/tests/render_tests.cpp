// The renderer's promises: shared edges covered exactly once, nothing written outside the
// scissor, depth decided before shading, blends that stay inside a byte, and a camera-space
// scan whose solved rows match the edge functions, clips at the near plane and corrects
// its weights for perspective.
#include "r2d.hpp"
#include "r2d_canvas.hpp"
#include "r3d.hpp"
#include "r3d_scan.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace {

using namespace render;

// Counts how often each pixel is covered.
struct Count {
    static constexpr int N = 0;
    static constexpr bool depth = false;
    bool shade(std::uint32_t* pixel, const float*) const {
        ++*pixel;
        return true;
    }
};

struct Image {
    std::vector<std::uint32_t> pixels;
    Target target;
    Image(int w, int h, std::uint32_t fill = 0) : pixels(static_cast<std::size_t>(w) * h, fill) {
        target = Target{pixels.data(), w, w, h, Order::bgra};
    }
};

void shared_edges_once() {
    // A fan of random triangles around shared vertices tiles a polygon: every pixel
    // centre inside is covered once, none twice.
    std::mt19937 random(7);
    std::uniform_real_distribution<float> jitter(-.49F, .49F);
    for (int trial = 0; trial < 200; ++trial) {
        Image image(64, 64);
        r3d::Pass pass{image.target, nullptr, image.target.bounds()};
        const r3d::Corner centre{32 + jitter(random) * 8, 32 + jitter(random) * 8, 0};
        const int spokes = 3 + trial % 9;
        std::vector<r3d::Corner> rim;
        for (int k = 0; k < spokes; ++k) {
            const float angle = 6.2831853F * (static_cast<float>(k) + jitter(random) * .3F) / static_cast<float>(spokes);
            rim.push_back({32 + 28 * std::cos(angle), 32 + 28 * std::sin(angle), 0});
        }
        for (int k = 0; k < spokes; ++k) {
            r3d::triangle(pass, centre, rim[static_cast<std::size_t>(k)], rim[static_cast<std::size_t>((k + 1) % spokes)],
                          Count{});
        }
        for (std::uint32_t value : image.pixels) {
            assert(value <= 1);
        }
        // Points well inside the polygon are covered.
        assert(image.pixels[32 * 64 + 32] == 1);
    }
}

void scissor_bounds_writes() {
    Image image(40, 30);
    const Rect clip{10, 5, 25, 20};
    r3d::Pass pass{image.target, nullptr, clip};
    r3d::triangle(pass, {-50, -50, 0}, {200, -50, 0}, {-50, 200, 0}, r3d::Flat{0xffffffffU});
    for (int y = 0; y < 30; ++y) {
        for (int x = 0; x < 40; ++x) {
            const bool inside = x >= clip.x0 && x < clip.x1 && y >= clip.y0 && y < clip.y1;
            assert((image.pixels[static_cast<std::size_t>(y) * 40 + x] != 0) == inside);
        }
    }
}

void depth_before_shading() {
    // The far triangle drawn second must not show; drawn first it is covered.
    for (int order = 0; order < 2; ++order) {
        Image image(16, 16);
        r3d::Buffers buffers;
        buffers.resize(16, 16, true);
        r3d::Pass pass{image.target, &buffers, image.target.bounds()};
        const float near_first[2] = {1, 2};
        const float far_first[2] = {2, 1};
        const float* depths = order == 0 ? near_first : far_first;
        for (int k = 0; k < 2; ++k) {
            const float z = depths[k];
            const bool near = z < 1.5F;
            r3d::triangle(pass, {-1, -1, z}, {40, -1, z}, {-1, 40, z}, r3d::Flat{near ? 0xff0000ffU : 0xff00ff00U},
                          near ? 1 : 2);
        }
        assert(image.pixels[5 * 16 + 5] == 0xff0000ffU);
        assert(buffers.id_at(5, 5) == 1);
        buffers.clear(Rect{0, 0, 8, 8});
        assert(buffers.id_at(5, 5) == -1 && buffers.id_at(12, 2) == 1);
    }
}

void smooth_interpolates() {
    Image image(32, 32);
    r3d::Pass pass{image.target, nullptr, image.target.bounds()};
    const float red[3] = {255, 0, 0};
    const float green[3] = {0, 255, 0};
    const float blue[3] = {0, 0, 255};
    r3d::triangle(pass, {0, 0, 0}, {32, 0, 0}, {0, 32, 0}, red, green, blue, r3d::Smooth{Order::rgba});
    const std::uint32_t corner = image.pixels[0];
    assert((corner & 0xff) > 230 && ((corner >> 8) & 0xff) < 20 && (corner >> 24) == 255);
}

void blends_stay_in_bytes() {
    std::mt19937 random(3);
    for (int trial = 0; trial < 20000; ++trial) {
        const Color c{static_cast<float>(random() % 256), static_cast<float>(random() % 256),
                      static_cast<float>(random() % 256), static_cast<float>(random() % 256)};
        const std::uint32_t alpha = random() % 256;
        // A valid premultiplied destination: channels no larger than its alpha.
        const std::uint32_t d = (random() % (alpha + 1)) | ((random() % (alpha + 1)) << 8) |
                                ((random() % (alpha + 1)) << 16) | (alpha << 24);
        std::uint32_t pixel = d;
        const r3d::Over over = r3d::Over::make(Order::bgra, c);
        static_cast<void>(over.shade(&pixel, nullptr));
        const double a = std::round(c.a) / 255;
        for (int lane = 0; lane < 4; ++lane) {
            const double source = lane == 3 ? std::round(c.a) : std::round((lane == 0 ? c.b : lane == 1 ? c.g : c.r) * a);
            const double expected = source + ((d >> (lane * 8)) & 0xff) * (1 - a);
            const double got = (pixel >> (lane * 8)) & 0xff;
            assert(std::fabs(got - expected) <= 1.01);
        }
    }
}

void mirror_mixes() {
    // A one-texel-high panorama of a single colour: the mirror shows it mixed with the tint.
    std::vector<std::uint8_t> file = {'G', 'P', 'I', 'X', 1, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0};
    for (int k = 0; k < 2; ++k) {
        file.insert(file.end(), {200, 100, 50, 255});
    }
    r3d::Panorama panorama;
    assert(panorama.load_gpix(file, Order::rgba, 1, 0));
    const r3d::Mirror<false> mirror = r3d::Mirror<false>::make(panorama, Color{0, 0, 255}, .5F);
    std::uint32_t pixel = 0;
    const float uv[2] = {.3F, .5F};
    static_cast<void>(mirror.shade(&pixel, uv));
    assert(std::abs(static_cast<int>(pixel & 0xff) - 100) <= 1);
    assert(std::abs(static_cast<int>((pixel >> 8) & 0xff) - 50) <= 1);
    assert(std::abs(static_cast<int>((pixel >> 16) & 0xff) - 152) <= 1);
    assert((pixel >> 24) == 255);
}

void layer_over_and_restore() {
    Image base(8, 8, 0xff102030U);
    Image layer(8, 8, 0);
    layer.pixels[9] = 0xff405060U;
    r2d::over(base.target, layer.target, Rect{0, 0, 8, 8}, .5F);
    assert(base.pixels[0] == 0xff102030U);
    assert(base.pixels[9] != 0xff102030U && (base.pixels[9] >> 24) == 255);
    Image kept(8, 8, 0xff000000U);
    r2d::restore(base.target, kept.target, Rect{0, 0, 2, 2});
    assert(base.pixels[9] == 0xff000000U && base.pixels[2] == 0xff102030U);
}

// --- The camera-space scan (r3d_scan.hpp).

// Counts coverage per pixel and the facings reported.
struct Counting {
    static constexpr bool perspective = false;
    int width = 0;
    std::vector<int>* counts = nullptr;
    int fronts = 0;
    int backs = 0;
    bool test(int, int, float) {
        return true;
    }
    void cover(const r3d::Coverage& at, bool front) {
        ++(*counts)[static_cast<std::size_t>(at.y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(at.x)];
        fronts += front ? 1 : 0;
        backs += front ? 0 : 1;
    }
};

// The pixels a triangle covers by the per-pixel edge test the scan's row solver stands
// in for: the same quantisation, orientation and top-left bias, at every centre.
void reference_coverage(const r3d::ScreenPoint* s, int width, int height, std::vector<int>& counts) {
    std::int64_t fx[3];
    std::int64_t fy[3];
    for (int k = 0; k < 3; ++k) {
        fx[k] = static_cast<std::int64_t>(std::lrint(std::clamp(s[k].x, -4.0e6F, 4.0e6F) * 256.0F));
        fy[k] = static_cast<std::int64_t>(std::lrint(std::clamp(s[k].y, -4.0e6F, 4.0e6F) * 256.0F));
    }
    const std::int64_t area = (fx[1] - fx[0]) * (fy[2] - fy[0]) - (fy[1] - fy[0]) * (fx[2] - fx[0]);
    if (area == 0) {
        return;
    }
    const int o1 = area < 0 ? 2 : 1;
    const int o2 = area < 0 ? 1 : 2;
    const std::int64_t x[3] = {fx[0], fx[o1], fx[o2]};
    const std::int64_t y[3] = {fy[0], fy[o1], fy[o2]};
    // Edge k runs from corner (k + 1) % 3 to (k + 2) % 3, opposite corner k.
    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            const std::int64_t cx = static_cast<std::int64_t>(px) * 256 + 128;
            const std::int64_t cy = static_cast<std::int64_t>(py) * 256 + 128;
            bool inside = true;
            for (int k = 0; k < 3; ++k) {
                const int from = (k + 1) % 3;
                const int to = (k + 2) % 3;
                const std::int64_t a = y[from] - y[to];
                const std::int64_t b = x[to] - x[from];
                const std::int64_t bias = (a > 0 || (a == 0 && b > 0)) ? 0 : -1;
                const std::int64_t w = a * (cx - x[from]) + b * (cy - y[from]) + bias;
                inside = inside && w >= 0;
            }
            if (inside) {
                ++counts[static_cast<std::size_t>(py) * static_cast<std::size_t>(width) + static_cast<std::size_t>(px)];
            }
        }
    }
}

void scan_rows_match_edge_functions() {
    // Random triangles, sharp and fat, partly off screen: the solved rows cover exactly
    // the pixels the three edge functions admit, so the pixel loop may skip the test.
    std::mt19937 random(5);
    std::uniform_real_distribution<float> at(-20, 84);
    const r3d::Lens lens{64, 64, 1, .1F};
    for (int trial = 0; trial < 400; ++trial) {
        std::vector<int> scanned(64 * 64, 0);
        std::vector<int> expected(64 * 64, 0);
        const r3d::ScreenPoint s[3] = {{at(random), at(random), 1}, {at(random), at(random), 1}, {at(random), at(random), 1}};
        Counting policy{64, &scanned, 0, 0};
        r3d::rasterize_projected(policy, lens, s[0], s[1], s[2], r3d::Cull::none);
        reference_coverage(s, 64, 64, expected);
        assert(scanned == expected);
    }
    // A fan of triangles sharing edges covers every pixel inside once, none twice.
    for (int trial = 0; trial < 100; ++trial) {
        std::vector<int> counts(64 * 64, 0);
        std::uniform_real_distribution<float> jitter(-.49F, .49F);
        const r3d::ScreenPoint centre{32 + jitter(random) * 6, 32 + jitter(random) * 6, 1};
        const int spokes = 3 + trial % 7;
        std::vector<r3d::ScreenPoint> rim;
        for (int k = 0; k < spokes; ++k) {
            const float angle = 6.2831853F * (static_cast<float>(k) + jitter(random) * .4F) / static_cast<float>(spokes);
            rim.push_back({32 + 29 * std::cos(angle), 32 + 29 * std::sin(angle), 1});
        }
        Counting policy{64, &counts, 0, 0};
        for (int k = 0; k < spokes; ++k) {
            r3d::rasterize_projected(policy, lens, centre, rim[static_cast<std::size_t>(k)],
                                     rim[static_cast<std::size_t>((k + 1) % spokes)], r3d::Cull::none);
        }
        for (int value : counts) {
            assert(value <= 1);
        }
        assert(counts[32 * 64 + 32] == 1);
    }
}

// Keeps one attribute, interpolated from the three corners, per covered pixel.
struct Picture {
    static constexpr bool perspective = false;
    float corner[3] = {0, 1, 0};
    std::vector<float> value = std::vector<float>(64 * 64, -1.0F);
    bool test(int, int, float) {
        return true;
    }
    void cover(const r3d::Coverage& at, bool) {
        value[static_cast<std::size_t>(at.y) * 64 + static_cast<std::size_t>(at.x)] =
            corner[0] * at.weight0 + corner[1] * at.weight1 + corner[2] * at.weight2;
    }
};

void scan_clips_at_the_near_plane() {
    // One corner in front of the near plane, two behind: the clipped triangle keeps the
    // attributes of the original corners (not the unit weights of its cut polygon).
    const r3d::Lens lens{64, 64, 40, .1F};
    const r3d::ViewPoint a{-.04F, -.04F, .2F};
    const r3d::ViewPoint b{.04F, -.04F, .05F};
    const r3d::ViewPoint c{-.04F, .04F, .05F};
    Picture actual;
    r3d::rasterize(actual, lens, a, b, c, r3d::Cull::none);
    const float t = (lens.near_plane - a.depth) / (b.depth - a.depth);
    const float u = (lens.near_plane - c.depth) / (a.depth - c.depth);
    const r3d::detail::ClipVertex vertices[3] = {
        {a, 1, 0, 0},
        {{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, lens.near_plane}, 1 - t, t, 0},
        {{c.x + (a.x - c.x) * u, c.y + (a.y - c.y) * u, lens.near_plane}, u, 0, 1 - u}};
    r3d::ScreenPoint screen[3];
    for (int k = 0; k < 3; ++k) {
        screen[k] = r3d::to_screen(lens, vertices[k].view);
    }
    const bool front = (screen[1].x - screen[0].x) * (screen[2].y - screen[0].y) -
                           (screen[2].x - screen[0].x) * (screen[1].y - screen[0].y) <
                       0;
    Picture expected;
    r3d::detail::sweep<Picture, false>(expected, 64, 64, screen, vertices, front);
    int covered = 0;
    for (std::size_t i = 0; i < actual.value.size(); ++i) {
        assert((actual.value[i] < 0) == (expected.value[i] < 0));
        if (expected.value[i] < 0) {
            continue;
        }
        ++covered;
        assert(std::fabs(actual.value[i] - expected.value[i]) <= 1e-6F);
    }
    assert(covered > 20);
    // A triangle wholly behind the near plane draws nothing; one wholly in front, all of it.
    Picture behind;
    r3d::rasterize(behind, lens, {0, 0, .05F}, {.1F, 0, .05F}, {0, .1F, .05F}, r3d::Cull::none);
    for (float value : behind.value) {
        assert(value < 0);
    }
}

// Checks the corrected weights against the depth the scan itself interpolates.
struct Depths {
    static constexpr bool perspective = true;
    float depth[3] = {0, 0, 0};
    int covered = 0;
    float worst = 0;
    bool test(int, int, float) {
        return true;
    }
    void cover(const r3d::Coverage& at, bool) {
        const float from_weights = depth[0] * at.weight0 + depth[1] * at.weight1 + depth[2] * at.weight2;
        const float from_scan = 1.0F / at.inverse_depth;
        worst = std::max(worst, std::fabs(from_weights - from_scan) / from_scan);
        worst = std::max(worst, std::fabs(at.weight0 + at.weight1 + at.weight2 - 1));
        ++covered;
    }
};

void scan_corrects_for_perspective() {
    // A floor receding from depth 1 to 9: perspective-correct weights interpolate depth
    // itself to the depth the scan steps, and sum to one.
    const r3d::Lens lens{64, 64, 48, .1F};
    const r3d::ViewPoint a{-1, -.5F, 1};
    const r3d::ViewPoint b{1, -.5F, 1};
    const r3d::ViewPoint c{0, -.5F, 9};
    Depths policy;
    policy.depth[0] = a.depth;
    policy.depth[1] = b.depth;
    policy.depth[2] = c.depth;
    r3d::rasterize(policy, lens, a, b, c, r3d::Cull::none);
    assert(policy.covered > 200);
    assert(policy.worst < 2e-3F);
}

void scan_reports_and_culls_facing() {
    const r3d::Lens lens{64, 64, 1, .1F};
    std::vector<int> counts(64 * 64, 0);
    const r3d::ViewPoint a{-20, -20, 1};
    const r3d::ViewPoint b{20, -20, 1};
    const r3d::ViewPoint c{0, 20, 1};
    Counting drawn{64, &counts, 0, 0};
    r3d::rasterize(drawn, lens, a, b, c, r3d::Cull::none);
    assert(drawn.fronts > 0 && drawn.backs == 0);
    Counting reversed{64, &counts, 0, 0};
    r3d::rasterize(reversed, lens, a, c, b, r3d::Cull::back);
    assert(reversed.fronts + reversed.backs == 0);
    r3d::rasterize(reversed, lens, a, c, b, r3d::Cull::none);
    assert(reversed.backs > 0 && reversed.fronts == 0);
}

void canvas_rectangles_match_paths() {
    // The rectangle path and the general rasteriser cover pixels alike, edges included.
    std::mt19937 random(11);
    std::uniform_real_distribution<double> at(-3, 40);
    for (int trial = 0; trial < 300; ++trial) {
        render::r2d::Canvas fast;
        render::r2d::Canvas general;
        fast.resize(40, 30);
        general.resize(40, 30);
        fast.clear({.2f, .3f, .4f, 1});
        general.clear({.2f, .3f, .4f, 1});
        const double x0 = at(random), y0 = at(random) * .7, w = at(random) * .6, h = at(random) * .5;
        const render::r2d::Col c{.9f, .5f, .1f, trial % 3 == 0 ? .6f : 1.f};
        fast.fill_rect(x0, y0, w, h, c);
        general.begin();
        general.move(x0, y0);
        general.line(x0 + w * .5, y0);  // a point on the top edge keeps it off the rectangle path
        general.line(x0 + w, y0);
        general.line(x0 + w, y0 + h);
        general.line(x0, y0 + h);
        general.close();
        general.fill(c);
        for (std::size_t i = 0; i < fast.px.size(); ++i) {
            assert(std::abs(static_cast<int>(fast.px[i]) - static_cast<int>(general.px[i])) <= 1);
        }
    }
}

}  // namespace

int main() {
    shared_edges_once();
    scissor_bounds_writes();
    depth_before_shading();
    smooth_interpolates();
    blends_stay_in_bytes();
    mirror_mixes();
    layer_over_and_restore();
    scan_rows_match_edge_functions();
    scan_clips_at_the_near_plane();
    scan_corrects_for_perspective();
    scan_reports_and_culls_facing();
    canvas_rectangles_match_paths();
    std::puts("render: all checks passed");
    return 0;
}
