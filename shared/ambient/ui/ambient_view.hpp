#pragma once
// A retained 3D scene as a PlaySuite control: a SceneView hosting a Diorama. A scene
// game supplies a SceneSetup (its archive, look, sounds and words) and derives a
// one-line view from AmbientView. Other kinds of scene derive from SceneView with
// their own Scenery.
#include "diorama.hpp"
#include "scene_view.hpp"

#include <string>
#include <vector>

namespace ambient {

struct SceneSetup {
    std::string id;               // the game id: assets are read from <assets>/<id>/
    std::string settings_file;    // in the state directory, e.g. "my_scene-v1.txt"; dev uses "...-dev.txt"
    std::string archive;          // below the game's asset folder, e.g. "scene/riverscape.ambient"
    std::string accessible_name;  // read by assistive technology
    std::string ambience;         // looping sound stem (the Sound master); empty for none
    std::string music;            // looping music stem (the Music master); empty for none
    std::string tap_sound;        // effect stem for a tap on the glass; empty for none
    std::string help_title;
    std::vector<std::string> help_paragraphs;  // the standalone help card
    LookFactory make_look{};
    SceneDressing dress{};        // the game's own props, added to the archive as it loads
#ifdef GUI_FORMS_AUDIO_GENERATOR
    SceneVoiceFactory live_ambience{};  // synthesized in place of `ambience` (and the tap) where possible
    VoiceFactory live_music{};     // synthesized in place of `music` where possible
#endif
    Bounds bounds{};              // where startled creatures may go
    // Several scenes the player can choose between with the Scene command (S); the
    // first is the default. When empty, archive/make_look/dress/bounds are the scene.
    std::vector<DioramaScene> scenes{};
    CadenceLimits limits{};
    std::uint32_t backdrop{0x0B1A14};  // 0xRRGGBB shown until the scene is ready
};

class AmbientView : public SceneView {
  public:
    AmbientView(gf::StableId id, const SceneSetup& setup, ViewOptions options);
};

} // namespace ambient
