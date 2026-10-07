#pragma once
// A scene the SceneView can host. The view owns everything every scene needs (the
// window surface, the governed clock, pause, detail, help, settings, sound
// routing, input); a Scenery owns what makes one scene itself: its world, its
// pictures and its responses. Stillwater's 3D tank (Diorama) is one; a top-down
// lawn is another.
//
// Every call is on the UI thread. A scenery may run its own background work and
// report it through poll(); it must wait for that work in its destructor.
#include "cadence.hpp"
#include "present.hpp"
#include "settings.hpp"
#include "sound_desk.hpp"
#include "suite.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ambient {

// What the view lends a scenery for one call.
struct SceneContext {
    Governor& governor;
    SoundDesk& sound;
    Settings& settings;    // scene-specific choices live in settings.extra; call persist() after changing them
    double time{};         // seconds of animation shown so far (it stops while paused)
    bool reduced{};        // the Motion master asks for less motion
    bool paused{};
    bool sound_on{};       // the Sound master and the scene is in front
    double charged{};      // seconds of this call's work the scenery already charged to the governor
    bool persist{};        // set true to have the view save the settings
};

class Scenery {
  public:
    virtual ~Scenery() = default;

    // Polls background work (loading, rebuilding); true while any is still pending.
    virtual bool poll(SceneContext& context) = 0;
    // True once a picture can be drawn.
    [[nodiscard]] virtual bool ready() const = 0;
    // Why the scene could not be opened, or empty.
    [[nodiscard]] virtual std::string failure() const {
        return {};
    }
    // The raster this scene wants for a view; the engine's sizing by default.
    [[nodiscard]] virtual SceneSize size_for(int device_width, int device_height, double scale, Detail detail) const {
        const SceneSize result = scene_size(device_width, device_height, scale, detail);
        return result;
    }
    // The view wants pictures of `size`. `settled` is false while a resize is still
    // under way, so expensive rebuilds can wait for it to stop.
    virtual void resize(SceneSize size, bool settled) = 0;
    // Advances the scene by `seconds` (never called while paused, hidden or occluded).
    virtual void advance(double seconds, SceneContext& context) = 0;
    // Draws the current picture: width * height pixels, 0xFFRRGGBB, row-major.
    virtual const std::vector<std::uint32_t>& draw(SceneContext& context, int& width, int& height) = 0;

    // Input, in fractions of the view (0..1, y down). press/drag/release make one gesture.
    virtual void press(double, double, SceneContext&) {}
    virtual void drag(double, double, SceneContext&) {}
    virtual void release(double, double, SceneContext&) {}
    // True where the pointer may grab something (the view shows a hand).
    [[nodiscard]] virtual bool grabbable(double, double) const {
        return false;
    }
    // Commands (actions) beyond Pause and Help.
    virtual void add_commands(std::vector<games::GameCommand>&, const Settings&) const {}
    virtual bool run_command(std::string_view, SceneContext&) {
        return false;
    }
    // Settings beyond Detail, for the PlaySuite Settings screen: choices that persist
    // (keep them in settings.extra and set context.persist).
    virtual void add_settings(std::vector<games::GameSetting>&, const Settings&) const {}
    virtual bool change_setting(std::string_view, double, SceneContext&) {
        return false;
    }
    // The command a key runs, if the scene gives it one (a gf::PhysicalKey); empty for none.
    // A key held or let go, before the view's own keys: true if the scene took it (a
    // mower steered by the arrow keys while they are held, say).
    virtual bool hold_key(std::uint32_t, bool, SceneContext&) {
        return false;
    }
    [[nodiscard]] virtual std::string key_command(std::uint32_t) const {
        return {};
    }
    // Development scripts: actions the view does not know.
    virtual bool scripted_action(std::string_view, SceneContext&) {
        return false;
    }
    // Sound for this tick: `running` is false while paused or before the scene is ready.
    virtual void sound(bool, SceneContext&) {}
};

} // namespace ambient
