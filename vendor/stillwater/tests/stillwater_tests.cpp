// Stillwater's scene checks against the shipped archive: it loads and validates,
// the look stays in range, the picture is the tank (not water alone), animation
// changes what it should and nothing else, and frames are reproducible.
//
//   stillwater_rules_tests <path to assets/scene/riverscape.ambient>
#include "archive.hpp"
#include "motion.hpp"
#include "riverscape_look.hpp"
#include "stage.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
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

int differing(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b) {
    int count = 0;
    for (std::size_t index = 0; index < a.size() && index < b.size(); ++index)
        count += a[index] != b[index] ? 1 : 0;
    return count;
}

void test_look_ranges(const sw::RiverscapeLook& look) {
    // The display mapping is monotonic and stays inside 0..1.
    float previous = -1;
    for (int step = 0; step <= 400; ++step) {
        const float value = static_cast<float>(step) / 40.0F;
        const ambient::Rgb out = sw::display_aces({value, value, value});
        require(out.x >= 0 && out.x <= 1 && out.y >= 0 && out.y <= 1 && out.z >= 0 && out.z <= 1,
                "display colours stay in range");
        require(out.y >= previous - 1e-5F, "the display mapping is monotonic");
        previous = out.y;
    }
    require(sw::display_aces({0, 0, 0}).y < 0.02F, "black stays black");
    // The animated light is a pattern in 0..1 that moves with time.
    float lowest = 1;
    float highest = 0;
    int changed = 0;
    for (int k = 0; k < 2000; ++k) {
        const float x = -10.0F + static_cast<float>(k) * 0.01F;
        const float a = look.animated_light(x, 0.3F * x, 5.0F);
        const float b = look.animated_light(x, 0.3F * x, 9.0F);
        require(a >= 0 && a <= 1, "animated light is between 0 and 1");
        lowest = std::min(lowest, a);
        highest = std::max(highest, a);
        changed += std::abs(a - b) > 0.01F ? 1 : 0;
    }
    require(lowest == 0 && highest > 0.8F, "the light forms bright ridges between dark water");
    require(changed > 200, "the light pattern moves");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s riverscape.ambient\n", argv[0]);
        return 2;
    }
    ambient::SceneData scene{};
    std::string error{};
    require(ambient::load_scene(argv[1], scene, error), "the shipped archive loads and validates");
    require(scene.textures.size() == 6 && scene.static_meshes.size() > 10 && scene.placements.size() > 7000,
            "the fixed scenery is all there");
    require(scene.sway_roots.size() == 626 && scene.sway.indices.size() / 3 > 80000 &&
                scene.sway.indices.size() / 3 < 200000,
            "the foliage is within its processor budget");
    require(scene.actors.size() == 18 && scene.risers.size() == 30, "sixteen fish, two crabs and the air stone");

    const sw::RiverscapeLook look(scene);
    test_look_ranges(look);

    const ambient::ShadowMap shadows = ambient::build_shadow_map(scene, look, 512);
    std::size_t shadowed = 0;
    for (const float depth : shadows.depth)
        shadowed += std::isfinite(depth) ? 1 : 0;
    require(shadowed > shadows.depth.size() / 4, "the shadow map sees the scenery");
    const ambient::Foliage foliage = ambient::prepare_foliage(scene, look, shadows);
    const int width = 236;
    const int height = 152;
    ambient::FixedLayer layer = ambient::build_fixed_layer(scene, look, shadows, foliage, width, height, 2, 0.5F);
    require(layer.width == width && layer.height == height, "a fixed layer at the requested size");
    std::size_t covered = 0;
    std::size_t lit = 0;
    for (std::size_t index = 0; index < layer.depth.size(); ++index) {
        covered += layer.depth[index] > 0 ? 1 : 0;
        lit += layer.boost[index] != 0 ? 1 : 0;
    }
    require(covered > layer.depth.size() / 4, "sand, rock and wood fill the lower tank");
    require(lit > 500, "the sand takes the rippling light");

    ambient::Stage stage(scene, look, foliage);
    stage.adopt(std::move(layer));
    std::vector<ambient::Creature> creatures = ambient::creatures_from(scene.actors);
    stage.update_sway(10.0);
    stage.compose(10.0, 10.0, creatures);
    const std::vector<std::uint32_t> frame = stage.frame();
    std::size_t green = 0;
    for (const std::uint32_t pixel : frame) {
        const int r = static_cast<int>((pixel >> 16U) & 255U);
        const int g = static_cast<int>((pixel >> 8U) & 255U);
        green += g > 70 && g > r + 15 ? 1 : 0;
    }
    require(green > frame.size() / 10, "the grass is there and green");
    require(stage.counters().actor_fragments > 50, "the fish are drawn");

    // Same inputs, same frame.
    stage.compose(10.0, 10.0, creatures);
    require(differing(stage.frame(), frame) == 0, "composing is reproducible");
    // Only the light's clock: some sand changes, the grass does not.
    stage.compose(10.0, 13.0, creatures);
    const int by_light = differing(stage.frame(), frame);
    require(by_light > 100 && by_light < static_cast<int>(frame.size()) / 3, "the light moves on the sand only");
    // Creatures' clock: a little of the picture changes.
    stage.compose(10.5, 10.0, creatures);
    const int by_fish = differing(stage.frame(), frame);
    require(by_fish > 30 && by_fish < static_cast<int>(frame.size()) / 4, "fish swim, the tank stays");
    // The current: the grass moves.
    stage.update_sway(14.0);
    stage.compose(10.0, 10.0, creatures);
    require(differing(stage.frame(), frame) > 200, "the grass sways in the current");

    // A tap beside a fish startles it, and the startled route starts where it was.
    const ambient::Projection& projection = stage.projection();
    const ambient::Pose before = ambient::creature_pose(creatures[3], 12.0);
    const ambient::ScreenPoint at = ambient::to_screen(
        projection, ambient::to_view(projection, {static_cast<float>(before.position.x),
                                                  static_cast<float>(before.position.y),
                                                  static_cast<float>(before.position.z)}));
    const ambient::ScreenTap tap{at.x / width, at.y / height, static_cast<double>(width) / height, 0.05};
    require(ambient::startle(creatures, projection, tap, 12.0, ambient::Bounds{}) >= 1, "a tap startles the fish beside it");
    std::printf("stillwater: %d checks passed\n", checks);
    return 0;
}
