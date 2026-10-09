// The renderer's promises: shared edges covered exactly once, nothing written outside the
// scissor, depth decided before shading, and blends that stay inside a byte.
#include "r2d.hpp"
#include "r2d_canvas.hpp"
#include "r3d.hpp"

#include <cassert>
#include <cmath>
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
    canvas_rectangles_match_paths();
    std::puts("render: all checks passed");
    return 0;
}
