// Stillwater's scene checks against the shipped archive: it loads and validates,
// the look stays in range, the picture is the tank (not water alone), animation
// changes what it should and nothing else, and frames are reproducible.
//
//   stillwater_rules_tests <path to assets/scene/riverscape.ambient>
#include "archive.hpp"
#include "motion.hpp"
#include "riverscape_look.hpp"
#include "island_voice.hpp"
#include "stage.hpp"
#include "tank_voice.hpp"
#include "tanks.hpp"
#include "treasure.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <thread>
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
    // The caustics: a pattern in 0..1 that moves with time.
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
    require(lowest == 0 && highest > 0.8F, "the light forms bright lines between dark water");
    // The pattern loops: one period on, the same light.
    const ambient::CausticField& field = *look.caustics();
    const float period = static_cast<float>(sw::tank_style(sw::Tank::planted).caustics.period);
    for (int k = 0; k < 200; ++k) {
        const ambient::Vec3 at{-6.0F + 0.06F * static_cast<float>(k), 0.3F, 0.5F};
        require(std::abs(field.at(at, 3.0F) - field.at(at, 3.0F + period)) < 1e-4F, "the caustics loop");
    }
    require(changed > 200, "the light pattern moves");
}

void test_tank_voice();

// The live voices: never silent, never clipping, finite, and the band keeps moving
// through verses. Each is far faster than real time.
void test_voices() {
    std::vector<float> block(48000 * 2 * 40, 0.0F);
    sw::IslandVoice band(7);
    for (std::size_t at = 0; at < block.size(); at += 512)
        band.render_add(std::span<float>(block.data() + at, std::min<std::size_t>(512, block.size() - at)), 0.25);
    double square = 0;
    float peak = 0;
    bool finite = true;
    for (const float sample : block) {
        finite = finite && std::isfinite(sample);
        peak = std::max(peak, std::abs(sample));
        square += static_cast<double>(sample) * sample;
    }
    const double rms = std::sqrt(square / static_cast<double>(block.size()));
    require(finite, "the band's samples are finite");
    require(peak < 0.5F && rms > 0.01, "the band plays below the ceiling");
    // An island section is eight slow bars and a vamp, about twenty seconds.
    require(band.verses_played() >= 2, "the band moves from section to section");
    std::vector<float> again(block.size(), 0.0F);
    sw::IslandVoice twin(7);
    for (std::size_t at = 0; at < again.size(); at += 512)
        twin.render_add(std::span<float>(again.data() + at, std::min<std::size_t>(512, again.size() - at)), 0.25);
    require(again == block, "a seed plays the same tunes");

    test_tank_voice();
}

// The tank's water and taps (tank_voice.hpp): the native Stillwater's recipe and level.
double rms_of(const std::vector<float>& samples, float& peak) {
    double square = 0;
    peak = 0;
    for (const float sample : samples) {
        peak = std::max(peak, std::abs(sample));
        square += static_cast<double>(sample) * sample;
    }
    const double result = std::sqrt(square / static_cast<double>(std::max<std::size_t>(1, samples.size())));
    return result;
}

std::vector<float> render_tank(sw::TankVoice& voice, std::size_t seconds, double gain) {
    std::vector<float> out(48000 * 2 * seconds, 0.0F);
    for (std::size_t at = 0; at < out.size(); at += 512)
        voice.render_add(std::span<float>(out.data() + at, std::min<std::size_t>(512, out.size() - at)), gain);
    return out;
}

void test_tank_voice() {
    float peak = 0;
    sw::TankVoice tank(3);
    const std::vector<float> water = render_tank(tank, 10, 1.0);
    // The original's ambience: RMS 0.0036 (-48.8 dBFS), peak 0.012 (-38.4 dBFS).
    const double original = rms_of(water, peak);
    require(original > 0.0028 && original < 0.0046 && peak < 0.02F, "the tank sits at the original's level");
    sw::TankVoice suite(3);
    const double played = rms_of(render_tank(suite, 10, sw::TankVoice::suite_level), peak);
    require(played > 0.006 && played < 0.02 && peak < 0.08F, "the suite plays the tank faintly");
    sw::TankVoice twin(3);
    require(render_tank(twin, 10, 1.0) == water, "a seed plays the same water");

    // The tank chosen before the voice is first heard is taken at once; later, glided to.
    sw::TankVoice pool(5, 2);
    sw::TankVoice told(5);
    told.set_tank(2);
    require(render_tank(told, 2, 1.0) == render_tank(pool, 2, 1.0), "the first tank is taken at once");
    sw::TankVoice planted(5);
    sw::TankVoice reef(5, 1);
    const double planted_rms = rms_of(render_tank(planted, 6, 1.0), peak);
    const double reef_rms = rms_of(render_tank(reef, 6, 1.0), peak);
    require(reef_rms > planted_rms * 1.01, "the reef's water moves more");

    // A tap is played once, near the original's level, panned toward its side.
    sw::TankVoice quiet(9);
    sw::TankVoice tapped(9);
    tapped.knock(0.8);
    const std::vector<float> plain = render_tank(quiet, 1, 1.0);
    const std::vector<float> knocked = render_tank(tapped, 1, 1.0);
    require(tapped.knocks_played() == 1, "a tap is played");
    double left = 0;
    double right = 0;
    float tap_peak = 0;
    for (std::size_t index = 0; index < plain.size(); index += 2) {
        const double l = static_cast<double>(knocked[index]) - plain[index];
        const double r = static_cast<double>(knocked[index + 1]) - plain[index + 1];
        left += l * l;
        right += r * r;
        tap_peak = std::max(tap_peak, static_cast<float>(std::max(std::abs(l), std::abs(r))));
    }
    // The original's centred tap peaks at -42.7 dBFS (0.0073).
    require(tap_peak > 0.003F && tap_peak < 0.02F, "the tap is a dull, quiet fingertip");
    require(right > left * 4, "the tap sounds from where the glass was touched");
    sw::TankVoice late(9);
    late.knock(0.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    static_cast<void>(render_tank(late, 1, 1.0));
    require(late.knocks_played() == 0, "a tap the device could not play in time is dropped");
}

// The treasure chest: four placements and three bubbles, in front of the camera,
// on the sand.
void test_treasure(const ambient::SceneData& original) {
    ambient::SceneData scene = original;
    const std::size_t placements = scene.placements.size();
    const std::size_t meshes = scene.static_meshes.size();
    const std::size_t risers = scene.risers.size();
    sw::add_treasure(scene);
    require(scene.placements.size() == placements + 4 && scene.static_meshes.size() == meshes + 4,
            "the chest adds its wood, iron and gold");
    require(scene.risers.size() == risers + 3, "bubbles escape the chest now and then");
    std::size_t triangles = 0;
    for (std::size_t index = meshes; index < scene.static_meshes.size(); ++index) {
        const ambient::Mesh<ambient::StaticVertex>& mesh = scene.static_meshes[index];
        triangles += mesh.indices.size() / 3;
        for (const std::uint32_t vertex : mesh.indices)
            require(vertex < mesh.vertices.size(), "chest indices are in range");
    }
    require(triangles > 300 && triangles < 4000, "the chest is a small prop");
    const ambient::Projection projection = ambient::make_projection(scene.camera, 590, 380);
    const ambient::StaticPlacementRecord& chest = scene.placements[placements];
    const ambient::ScreenPoint at = ambient::to_screen(
        projection, ambient::to_view(projection, {chest.transform[3], chest.transform[7], chest.transform[11]}));
    require(at.x > 100 && at.x < 490 && at.y > 250 && at.y < 380, "the chest sits on the sand in view");
    require(std::abs(sw::ground_height(chest.transform[3], chest.transform[11]) - chest.transform[7]) < 0.3F,
            "the chest is sunk in the sand");
}

// Another tank's archive: it loads, keeps within its budget, draws its own colours,
// its fish, its foliage and its caustics.
void test_tank(const std::string& path, sw::Tank tank) {
    ambient::SceneData scene{};
    std::string error{};
    require(ambient::load_scene(path, scene, error), "the tank's archive loads and validates");
    require(scene.textures.size() == 6 && scene.actors.size() >= 15, "the tank has its fish");
    require(scene.sway.indices.size() / 3 > 2000 && scene.sway.indices.size() / 3 < 60000,
            "the tank's foliage is within budget");
    sw::dress_tank(tank, scene);
    const sw::RiverscapeLook look(scene, sw::tank_style(tank));
    require(look.caustics() != nullptr && !(*look.caustics()).empty(), "the tank has caustics");
    const ambient::ShadowMap shadows = ambient::build_shadow_map(scene, look, 512);
    const ambient::Foliage foliage = ambient::prepare_foliage(scene, look, shadows);
    ambient::FixedLayer layer = ambient::build_fixed_layer(scene, look, shadows, foliage, 236, 152, 2, 0.5F);
    std::size_t lit = 0;
    for (const std::uint32_t boost : layer.boost)
        lit += boost != 0 ? 1 : 0;
    require(lit > 2000, "the tank's floor and stones take the caustics");
    ambient::Stage stage(scene, look, foliage);
    stage.adopt(std::move(layer));
    std::vector<ambient::Creature> creatures = ambient::creatures_from(scene.actors);
    stage.update_sway(10.0);
    stage.compose(10.0, 10.0, creatures);
    const std::vector<std::uint32_t> frame = stage.frame();
    require(stage.counters().actor_fragments > 200, "the tank's fish are drawn");
    // The water's colour, from the open water in the upper third of the picture.
    double red = 0;
    double green = 0;
    double blue = 0;
    const std::size_t upper = static_cast<std::size_t>(stage.width()) * static_cast<std::size_t>(stage.height() / 3);
    for (std::size_t index = 0; index < upper && index < frame.size(); ++index) {
        const std::uint32_t pixel = frame[index];
        red += static_cast<double>((pixel >> 16U) & 255U);
        green += static_cast<double>((pixel >> 8U) & 255U);
        blue += static_cast<double>(pixel & 255U);
    }
    if (tank == sw::Tank::reef)
        require(blue > green * 1.15 && blue > red * 1.5, "the reef's water is blue");
    else
        require(red > blue * 1.2 && green > blue * 1.2, "the river pool's water is brown");
    stage.compose(10.0, 13.0, creatures);
    require(differing(stage.frame(), frame) > 300, "the tank's caustics move");
    stage.update_sway(14.0);
    stage.compose(10.0, 10.0, creatures);
    require(differing(stage.frame(), frame) > 50, "the tank's foliage sways");
    stage.update_sway(10.0);
    stage.compose(10.0, 10.0, creatures);
    require(differing(stage.frame(), frame) == 0, "the tank's frames are reproducible");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s riverscape.ambient (reef.ambient and pool.ambient beside it)\n", argv[0]);
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

    const sw::RiverscapeLook look(scene, sw::tank_style(sw::Tank::planted));
    test_look_ranges(look);
    test_voices();
    test_treasure(scene);

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
    // Only the light's clock: the caustics move over sand, stone, leaves and fish,
    // and setting the clock back gives the same picture.
    stage.compose(10.0, 13.0, creatures);
    const int by_light = differing(stage.frame(), frame);
    require(by_light > 1000 && by_light < static_cast<int>(frame.size()) * 3 / 4, "the caustics move");
    stage.compose(10.0, 10.0, creatures);
    require(differing(stage.frame(), frame) == 0, "the caustics are a function of their clock");
    // Creatures' clock: a little of the picture changes.
    stage.compose(10.5, 10.0, creatures);
    const int by_fish = differing(stage.frame(), frame);
    require(by_fish > 30 && by_fish < static_cast<int>(frame.size()) / 4, "fish swim, the tank stays");
    // The current: the grass moves.
    stage.update_sway(14.0);
    stage.compose(10.0, 10.0, creatures);
    require(differing(stage.frame(), frame) > 200, "the grass sways in the current");
    // A foliage update built beside the frames shows only once it is finished.
    const std::vector<std::uint32_t> swayed = stage.frame();
    stage.build_sway(18.0);
    stage.compose(10.0, 10.0, creatures);
    require(differing(stage.frame(), swayed) == 0, "a foliage update in progress does not show");
    stage.show_sway();
    stage.compose(10.0, 10.0, creatures);
    const std::vector<std::uint32_t> later = stage.frame();
    require(differing(later, swayed) > 200, "a finished foliage update shows");
    stage.update_sway(18.0);
    stage.compose(10.0, 10.0, creatures);
    require(differing(stage.frame(), later) == 0, "building beside the frames draws what updating draws");
    // Foliage the fixed layer hides is left out, and the picture is the same without it.
    require(stage.counters().hidden_triangles > 1000, "hidden foliage is left out of the sway updates");
    {
        ambient::FixedLayer every = ambient::build_fixed_layer(scene, look, shadows, foliage, width, height, 2, 0.5F);
        every.hidden.assign(every.hidden.size(), 0);
        ambient::Stage unculled(scene, look, foliage);
        unculled.adopt(std::move(every));
        unculled.update_sway(18.0);
        unculled.compose(10.0, 10.0, creatures);
        require(unculled.counters().hidden_triangles == 0 && differing(unculled.frame(), later) == 0,
                "leaving out hidden foliage changes no pixel");
    }

    // A tap beside a fish startles it, and the startled route starts where it was.
    const ambient::Projection& projection = stage.projection();
    const ambient::Pose before = ambient::creature_pose(creatures[3], 12.0);
    const ambient::ScreenPoint at = ambient::to_screen(
        projection, ambient::to_view(projection, {static_cast<float>(before.position.x),
                                                  static_cast<float>(before.position.y),
                                                  static_cast<float>(before.position.z)}));
    const ambient::ScreenTap tap{at.x / width, at.y / height, static_cast<double>(width) / height, 0.05};
    require(ambient::startle(creatures, projection, tap, 12.0, ambient::Bounds{}) >= 1, "a tap startles the fish beside it");
    // The other tanks, beside the planted tank's archive.
    const std::string folder = std::string(argv[1]).substr(0, std::string(argv[1]).find_last_of("/\\") + 1);
    test_tank(folder + "reef.ambient", sw::Tank::reef);
    test_tank(folder + "pool.ambient", sw::Tank::pool);
    std::printf("stillwater: %d checks passed\n", checks);
    return 0;
}
