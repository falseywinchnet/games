#include "stillwater_view.hpp"

#include "riverscape_look.hpp"

namespace sw {
namespace {

std::unique_ptr<ambient::Look> make_look(const ambient::SceneData& scene) {
    std::unique_ptr<ambient::Look> look = std::make_unique<RiverscapeLook>(scene);
    return look;
}

} // namespace

ambient::SceneSetup stillwater_setup() {
    ambient::SceneSetup setup{};
    setup.id = "stillwater";
    setup.settings_file = "stillwater-v1.txt";
    setup.archive = "scene/riverscape.ambient";
    setup.accessible_name = "Stillwater. A planted aquarium of tetras. Click the glass to startle the fish.";
    setup.ambience = "sw_ambience";
    setup.tap_sound = "sw_tap";
    setup.help_title = "Stillwater";
    setup.help_paragraphs = {
        "A planted tank: tall grass leaning in a slow current, driftwood, stones, sixteen "
        "tetras and two crabs on the sand.",
        "Click the glass near a fish to startle it. It darts away, then settles into a new loop.",
        "Space pauses and resumes. While paused the tank costs nothing at all. "
        "D changes the detail; Light is kindest to an older computer.",
        "The tank slows itself to stay light on the processor, and slows further while "
        "another window is in front."};
    setup.make_look = &make_look;
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
