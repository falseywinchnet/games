// The grass engine's acceptance tests: pictures have the asked-for size and are opaque,
// the same call gives the same picture, the weather changes the grass the way the season
// should and leaves it alone when it is all zero, flowers and the bare strip land where the
// scene puts them, and sampling and canopies behave.
#include "lawn.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int checks = 0;
void require(bool condition, const char* what) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

struct Mean {
    double red{};
    double green{};
    double blue{};
};

Mean mean_of(const grass::Layer& layer, int x0, int y0, int x1, int y1) {
    Mean m{};
    int count = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const std::uint32_t p = layer.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(layer.width) + static_cast<std::size_t>(x)];
            m.red += static_cast<double>((p >> 16U) & 255U);
            m.green += static_cast<double>((p >> 8U) & 255U);
            m.blue += static_cast<double>(p & 255U);
            ++count;
        }
    m.red /= count;
    m.green /= count;
    m.blue /= count;
    return m;
}

Mean mean_all(const grass::Layer& layer) {
    return mean_of(layer, 0, 0, layer.width, layer.height);
}

// An open field: nothing for the grass to stop at.
grass::LawnScene open_scene() {
    grass::LawnScene scene{};
    scene.width = 2;
    scene.height = 2;
    scene.distance.assign(4, 1000.0F);
    return scene;
}

constexpr double metres_w = 3.0;
constexpr double metres_h = 2.0;
constexpr double ppm = 48.0;

void test_size_and_opacity() {
    const grass::Layer tall = grass::render_lawn(grass::LawnLook::tall, open_scene(), 7, metres_w, metres_h, ppm);
    require(tall.width == 144 && tall.height == 96, "a layer covers the asked-for metres at the asked-for scale");
    require(tall.px.size() == static_cast<std::size_t>(144 * 96), "one pixel for every place");
    bool opaque = true;
    for (std::uint32_t p : tall.px)
        opaque = opaque && (p >> 24U) == 255U;
    require(opaque, "the lawn is opaque");
    const Mean m = mean_all(tall);
    require(m.green > m.red && m.green > m.blue, "summer grass is green");
    const grass::Layer mulch = grass::render_lawn(grass::LawnLook::mulch, open_scene(), 7, metres_w, metres_h, ppm);
    const Mean bark = mean_all(mulch);
    require(bark.red > bark.green && bark.green > bark.blue, "bark mulch is brown");
}

void test_deterministic() {
    const grass::Layer a = grass::render_lawn(grass::LawnLook::tall, open_scene(), 11, metres_w, metres_h, ppm);
    const grass::Layer b = grass::render_lawn(grass::LawnLook::tall, open_scene(), 11, metres_w, metres_h, ppm);
    require(a.px == b.px, "the same call grows the same grass");
    const grass::Layer c = grass::render_lawn(grass::LawnLook::tall, open_scene(), 12, metres_w, metres_h, ppm);
    require(a.px != c.px, "another seed grows other grass");
    const grass::Layer d = grass::render_lawn(grass::LawnLook::tall, open_scene(), 11, metres_w, metres_h, ppm, grass::LawnWeather{});
    require(a.px == d.px, "no weather is the default lawn exactly");
}

void test_weather() {
    const grass::Layer plain = grass::render_lawn(grass::LawnLook::tall, open_scene(), 5, metres_w, metres_h, ppm);
    grass::LawnWeather spring{};
    spring.fresh = 1;
    grass::LawnWeather autumn{};
    autumn.dry = .8;
    grass::LawnWeather winter{};
    winter.frost = .8;
    const Mean base = mean_all(plain);
    const Mean fresh = mean_all(grass::render_lawn(grass::LawnLook::tall, open_scene(), 5, metres_w, metres_h, ppm, spring));
    const Mean dry = mean_all(grass::render_lawn(grass::LawnLook::tall, open_scene(), 5, metres_w, metres_h, ppm, autumn));
    const Mean frost = mean_all(grass::render_lawn(grass::LawnLook::tall, open_scene(), 5, metres_w, metres_h, ppm, winter));
    std::printf("mean rgb: plain %.0f %.0f %.0f, fresh %.0f %.0f %.0f, dry %.0f %.0f %.0f, frost %.0f %.0f %.0f\n", base.red, base.green, base.blue,
                fresh.red, fresh.green, fresh.blue, dry.red, dry.green, dry.blue, frost.red, frost.green, frost.blue);
    require(fresh.green > base.green && fresh.red / fresh.green > base.red / base.green, "spring growth is lighter and yellower");
    require(dry.red / dry.green > base.red / base.green + .08, "autumn grass is tinged with straw");
    require(frost.blue > base.blue + 30 && frost.red > base.red + 30, "frost whitens the grass");
    require(frost.blue / frost.red > dry.blue / dry.red, "frost is cool, straw is warm");
}

void test_scene() {
    // A daisy in the middle of an open field shows there and nowhere far from it.
    grass::LawnScene flowered = open_scene();
    flowered.flowers.push_back(grass::LawnFlower{3, 1.5, 1.0});
    const grass::Layer plain = grass::render_lawn(grass::LawnLook::tall, open_scene(), 3, metres_w, metres_h, ppm);
    const grass::Layer daisy = grass::render_lawn(grass::LawnLook::tall, flowered, 3, metres_w, metres_h, ppm);
    const Mean near_plain = mean_of(plain, 62, 38, 82, 58);
    const Mean near_daisy = mean_of(daisy, 62, 38, 82, 58);
    require(near_daisy.red + near_daisy.blue > near_plain.red + near_plain.blue + 3, "the daisy shows where it was put");
    require(mean_of(daisy, 0, 0, 30, 30).green == mean_of(plain, 0, 0, 30, 30).green, "the daisy changes nothing far from it");
    // Something standing in the middle leaves a bare strip around its foot.
    grass::LawnScene blocked{};
    blocked.width = 31;
    blocked.height = 21;
    blocked.distance.assign(static_cast<std::size_t>(31 * 21), 0.0F);
    for (int y = 0; y < 21; ++y)
        for (int x = 0; x < 31; ++x) {
            const double dx = (x - 15) * .1, dy = (y - 10) * .1;
            blocked.distance[static_cast<std::size_t>(y * 31 + x)] = static_cast<float>(std::max(0.0, std::hypot(dx, dy) - .2));
        }
    const grass::Layer bare = grass::render_lawn(grass::LawnLook::tall, blocked, 3, metres_w, metres_h, ppm);
    const Mean middle = mean_of(bare, 68, 44, 76, 52);
    const Mean open = mean_of(bare, 10, 10, 40, 40);
    require(middle.green < open.green && middle.red / middle.green > open.red / open.green, "the grass stops in a bare strip");
}

void test_sampling() {
    grass::Layer layer{};
    layer.width = 2;
    layer.height = 1;
    layer.px = {0xFF000000U, 0xFFFFFFFFU};
    require(grass::sample_clamped(layer, -5, 0) == 0xFF000000U, "sampling clamps at the left edge");
    require(grass::sample_clamped(layer, 9, 3) == 0xFFFFFFFFU, "sampling clamps at the right edge");
    const std::uint32_t half = grass::sample_clamped(layer, .5, 0);
    require(((half >> 8U) & 255U) >= 126U && ((half >> 8U) & 255U) <= 129U, "sampling between pixels blends them");
    require(grass::sample_clamped(grass::Layer{}, 0, 0) == 0U, "an empty layer samples as nothing");
}

void test_tree() {
    const grass::Cutout tree = grass::render_tree(0, .8, 4, 40);
    require(!tree.image.empty(), "a canopy is drawn");
    int clear = 0;
    int solid = 0;
    for (std::uint32_t p : tree.image.px) {
        clear += (p >> 24U) == 0U ? 1 : 0;
        solid += (p >> 24U) == 255U ? 1 : 0;
    }
    require(clear > 0 && solid > 0, "a canopy is a cut-out with leaves and gaps");
    require(tree.root_x > 0 && tree.root_x < tree.image.width, "the canopy's root is inside it");
}

} // namespace

int main() {
    test_size_and_opacity();
    test_deterministic();
    test_weather();
    test_scene();
    test_sampling();
    test_tree();
    std::printf("grass engine: %d checks passed\n", checks);
    return 0;
}
