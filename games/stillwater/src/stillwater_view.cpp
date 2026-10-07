#include "stillwater_view.hpp"

#include "island_voice.hpp"
#include "tank_voice.hpp"
#include "tanks.hpp"

#include <random>

namespace sw {
namespace {

std::unique_ptr<ambient::Look> planted_look(const ambient::SceneData& scene) {
    return make_tank_look(Tank::planted, scene);
}
std::unique_ptr<ambient::Look> reef_look(const ambient::SceneData& scene) {
    return make_tank_look(Tank::reef, scene);
}
std::unique_ptr<ambient::Look> pool_look(const ambient::SceneData& scene) {
    return make_tank_look(Tank::pool, scene);
}
void dress_planted(ambient::SceneData& scene) {
    dress_tank(Tank::planted, scene);
}
void dress_reef(ambient::SceneData& scene) {
    dress_tank(Tank::reef, scene);
}

ambient::DioramaScene tank_scene(const char* id, const char* name, const char* archive, ambient::LookFactory look,
                                 ambient::SceneDressing dress) {
    ambient::DioramaScene scene{};
    scene.id = id;
    scene.name = name;
    scene.archive = archive;
    scene.make_look = look;
    scene.dress = dress;
    scene.bounds = ambient::Bounds{};
    return scene;
}

#ifdef GUI_FORMS_AUDIO_GENERATOR
// The tank's water, synthesized as it plays (tank_voice.hpp), under the Sound master.
class LiveTank final : public gui_forms::AudioGenerator {
  public:
    void render(std::span<float> stereo) noexcept override {
        std::fill(stereo.begin(), stereo.end(), 0.0F);
        tank_.render_add(stereo, 1.0);
    }

  private:
    TankVoice tank_{static_cast<std::uint32_t>(std::random_device{}())};
};

// The island band on its old record, making up tunes as it plays (island_voice.hpp), under the
// Music master.
class LiveIsland final : public gui_forms::AudioGenerator {
  public:
    void render(std::span<float> stereo) noexcept override {
        std::fill(stereo.begin(), stereo.end(), 0.0F);
        band_.render_add(stereo, 0.25);
    }

  private:
    IslandVoice band_{static_cast<std::uint32_t>(std::random_device{}())};
};

std::shared_ptr<gui_forms::AudioGenerator> make_tank() {
    std::shared_ptr<gui_forms::AudioGenerator> voice = std::make_shared<LiveTank>();
    return voice;
}

std::shared_ptr<gui_forms::AudioGenerator> make_island() {
    std::shared_ptr<gui_forms::AudioGenerator> voice = std::make_shared<LiveIsland>();
    return voice;
}
#endif

} // namespace

ambient::SceneSetup stillwater_setup() {
    ambient::SceneSetup setup{};
    setup.id = "stillwater";
    setup.settings_file = "stillwater-v1.txt";
    setup.archive = "scene/riverscape.ambient";
    // The planted tank first: it is the default, and what saves from before there was
    // a choice open on.
    setup.scenes = {tank_scene("planted", "Planted", "scene/riverscape.ambient", &planted_look, &dress_planted),
                    tank_scene("reef", "Reef", "scene/reef.ambient", &reef_look, &dress_reef),
                    tank_scene("pool", "River pool", "scene/pool.ambient", &pool_look, nullptr)};
    setup.accessible_name = "Stillwater. A living aquarium: a planted tank, a coral reef or a river pool. "
                            "Click the glass to startle the fish; S changes the scene.";
    // The water is sound (the Sound master); the island band is music (the Music
    // master). Both are live where the toolkit plays generators, otherwise loops.
    setup.ambience = "sw_ambience";
    setup.music = "sw_island";
#ifdef GUI_FORMS_AUDIO_GENERATOR
    setup.live_ambience = &make_tank;
    setup.live_music = &make_island;
#endif
    setup.tap_sound = "sw_tap";
    setup.help_title = "Stillwater";
    setup.help_paragraphs = {
        "A planted tank: tall grass leaning in a slow current, driftwood, stones, sixteen "
        "tetras, two crabs on the sand and a little sunken treasure chest, with sunlight "
        "rippling over it all.",
        "S changes the scene: the planted tank, a coral reef or a dim river pool. Stillwater "
        "remembers the one you chose.",
        "Click the glass near a fish to startle it. It darts away, then settles into a new loop.",
        "Space pauses and resumes. While paused the tank costs nothing at all. "
        "Q changes the detail; Light is kindest to an older computer.",
        "The water and the glass are Sound; the island band is Music. "
        "M turns the band off and on.",
        "The tank slows itself to stay light on the processor, and slows further while "
        "another window is in front."};
    setup.make_look = &planted_look;
    setup.dress = &dress_planted;
    setup.bounds = ambient::Bounds{};
    // Tetras cruise about twenty pixels a second at Balanced: twenty frames move
    // them a pixel at a time. The current is slower still.
    setup.limits = ambient::CadenceLimits{.frames_preferred = 20,
                                          .frames_lowest = 12,
                                          .sway_preferred = 6,
                                          .sway_lowest = 3,
                                          .budget = 0.10};
    setup.backdrop = 0x1C362D;
    return setup;
}

StillwaterView::StillwaterView(ambient::gf::StableId id, ambient::ViewOptions options)
    : AmbientView(std::move(id), stillwater_setup(), options) {}

} // namespace sw
