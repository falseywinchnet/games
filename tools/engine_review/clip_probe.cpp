// Standalone diagnostic, not a replacement for an existing test or its oracle.
// clang++ -std=c++20 -O2 -ffp-contract=off -Ishared/ambient/src
//   tools/engine_review/clip_probe.cpp -o .build/clip_probe
#include "raster3d.hpp"
#include <array>
#include <cstdio>

struct Picture {
    static constexpr bool perspective = true;
    std::array<float, 64 * 64> value{};
    std::array<bool, 64 * 64> covered{};
    bool test(int, int, float) { return true; }
    void cover(const ambient::Coverage& c, bool) {
        const std::size_t i = static_cast<std::size_t>(c.y * 64 + c.x);
        value[i] = c.weight1; // Attribute 0 at A, 1 at B, 0 at C.
        covered[i] = true;
    }
};

int main() {
    ambient::Projection p{};
    p.width = p.height = 64;
    p.focal = 40;
    p.near_plane = .1F;
    const ambient::ViewPoint a{-.04F, -.04F, .2F};
    const ambient::ViewPoint b{.04F, -.04F, .05F};
    const ambient::ViewPoint c{-.04F, .04F, .05F};
    Picture actual, expected;
    ambient::rasterize(actual, p, a, b, c, ambient::Cull::none);
    // One vertex survives: the clipped polygon is A, intersection AB, intersection CA.
    const float t = (p.near_plane - a.depth) / (b.depth - a.depth);
    const float u = (p.near_plane - c.depth) / (a.depth - c.depth);
    const ambient::detail::ClipVertex vertices[3] = {
        {a, 1, 0, 0},
        {{ambient::mix(a.x, b.x, t), ambient::mix(a.y, b.y, t), p.near_plane}, 1 - t, t, 0},
        {{ambient::mix(c.x, a.x, u), ambient::mix(c.y, a.y, u), p.near_plane}, u, 0, 1 - u}};
    ambient::ScreenPoint screen[3];
    for (int k = 0; k < 3; ++k) screen[k] = ambient::to_screen(p, vertices[k].view);
    const bool front = (screen[1].x - screen[0].x) * (screen[2].y - screen[0].y) -
                       (screen[2].x - screen[0].x) * (screen[1].y - screen[0].y) < 0;
    ambient::detail::scan<Picture, false>(expected, 64, 64, screen, vertices, front);
    int covered = 0, changed = 0, coverage_errors = 0;
    float maximum = 0;
    for (std::size_t i = 0; i < actual.value.size(); ++i) {
        coverage_errors += actual.covered[i] != expected.covered[i];
        if (!expected.covered[i]) continue;
        ++covered;
        const float error = std::abs(actual.value[i] - expected.value[i]);
        changed += error > 1e-6F;
        maximum = std::max(maximum, error);
    }
    std::printf("covered %d, coverage differences %d, attribute differences %d, max error %.9g\n",
                covered, coverage_errors, changed, static_cast<double>(maximum));
    // This is a measurement: report the discrepancy, rather than changing a test expectation.
    return 0;
}
