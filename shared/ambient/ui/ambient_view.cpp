#include "ambient_view.hpp"

namespace ambient {
namespace {

HostSetup host_from(const SceneSetup& setup) {
    HostSetup host{};
    host.id = setup.id;
    host.settings_file = setup.settings_file;
    host.accessible_name = setup.accessible_name;
    host.help_title = setup.help_title;
    host.help_paragraphs = setup.help_paragraphs;
    host.limits = setup.limits;
    host.backdrop = setup.backdrop;
    return host;
}

std::unique_ptr<Scenery> diorama_from(const SceneSetup& setup) {
    DioramaSetup diorama{};
    diorama.id = setup.id;
    diorama.archive = setup.archive;
    diorama.ambience = setup.ambience;
    diorama.music = setup.music;
    diorama.tap_sound = setup.tap_sound;
    diorama.make_look = setup.make_look;
    diorama.dress = setup.dress;
#ifdef GUI_FORMS_AUDIO_GENERATOR
    diorama.live_ambience = setup.live_ambience;
    diorama.live_music = setup.live_music;
#endif
    diorama.bounds = setup.bounds;
    std::unique_ptr<Scenery> scenery = std::make_unique<Diorama>(diorama);
    return scenery;
}

} // namespace

AmbientView::AmbientView(gf::StableId id, const SceneSetup& setup, ViewOptions options)
    : SceneView(std::move(id), host_from(setup), diorama_from(setup), options) {}

} // namespace ambient
