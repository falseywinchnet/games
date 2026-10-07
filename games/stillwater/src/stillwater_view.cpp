#include "stillwater_view.hpp"

#include "riverscape_look.hpp"
#include "island_voice.hpp"
#include "tank_voice.hpp"
#include "treasure.hpp"

#include <random>

namespace sw {
namespace {

std::unique_ptr<ambient::Look> make_look(const ambient::SceneData& scene) {
    std::unique_ptr<ambient::Look> look = std::make_unique<RiverscapeLook>(scene);
    return look;
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
    setup.accessible_name = "Stillwater. A planted aquarium of tetras. Click the glass to startle the fish.";
    // The water is sound (the Sound master); the shanties are music (the Music
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
        "tetras, two crabs on the sand and a little sunken treasure chest.",
        "Click the glass near a fish to startle it. It darts away, then settles into a new loop.",
        "Space pauses and resumes. While paused the tank costs nothing at all. "
        "Q changes the detail; Light is kindest to an older computer.",
        "The water and the glass are Sound; the shanties from below decks are Music. "
        "M turns the shanties off and on.",
        "The tank slows itself to stay light on the processor, and slows further while "
        "another window is in front."};
    setup.make_look = &make_look;
    setup.dress = &add_treasure;
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
