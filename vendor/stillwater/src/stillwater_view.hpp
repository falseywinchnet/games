#pragma once
// Stillwater in PlaySuite: the ambient engine's view with Riverscape's archive,
// look, sounds and help. Everything about running a scene lives in the engine
// (engines/ambient/ui/ambient_view.hpp); this file only says which scene.
#include "ambient_view.hpp"

namespace sw {

class StillwaterView final : public ambient::AmbientView {
  public:
    StillwaterView(ambient::gf::StableId id, ambient::ViewOptions options);
};

// The scene's setup, shared by the view and tests.
ambient::SceneSetup stillwater_setup();

} // namespace sw
