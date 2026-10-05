#pragma once
// An ambient scene as a PlaySuite control. A scene game supplies a SceneSetup (its
// archive, look, sounds and words) and derives a one-line view from AmbientView;
// everything else is here:
//
//   * loading the archive and building the fixed layer off the UI thread, and
//     rebuilding it after a resize while the previous picture keeps showing;
//   * a governed timer: frames and foliage updates at rates that keep the scene
//     inside its processor budget (cadence.hpp), half the budget while the window
//     is not active, and no timer at all while paused, hidden or occluded;
//   * the Motion master (reduced motion holds the foliage and the light still),
//     tapping the glass, Pause / Detail / Help commands, remembered settings, and
//     a help card when no shell is there to show help.
#include "suite.hpp"

#include "archive.hpp"
#include "cadence.hpp"
#include "motion.hpp"
#include "present.hpp"
#include "scene_audio.hpp"
#include "settings.hpp"
#include "stage.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ambient {
namespace gf = gui_forms;

struct ViewOptions {
    bool hosted = false;  // inside PlaySuite: the capsule carries the commands
    bool dev = false;     // separate settings, and scripted actions are accepted
};

// Creates the scene's look once its archive has loaded (on a worker thread).
using LookFactory = std::unique_ptr<Look> (*)(const SceneData& scene);

struct SceneSetup {
    std::string id;               // the game id: assets are read from <assets>/<id>/
    std::string settings_file;    // in the state directory, e.g. "my_scene-v1.txt"; dev uses "...-dev.txt"
    std::string archive;          // below the game's asset folder, e.g. "scene/riverscape.ambient"
    std::string accessible_name;  // read by assistive technology
    std::string ambience;         // looping sound stem; empty for none
    std::string tap_sound;        // effect stem for a tap on the glass; empty for none
    std::string help_title;
    std::vector<std::string> help_paragraphs;  // the standalone help card
    LookFactory make_look{};
    Bounds bounds{};              // where startled creatures may go
    CadenceLimits limits{};
    std::uint32_t backdrop{0x0B1A14};  // 0xRRGGBB shown until the scene is ready
};

class AmbientView : public gf::Control, public games::CommandSource {
  public:
    AmbientView(gf::StableId id, SceneSetup setup, ViewOptions options);
    ~AmbientView() override;
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree() {}
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void on_pointer(gf::PointerEvent& event) override;
    void on_key(gf::KeyEvent& event) override;
    void on_key_bubble(gf::KeyEvent& event) override {
        on_key(event);
    }

    void activate();
    void set_cabinet(bool foreground, bool music, bool sound, bool reduced = false);
    [[nodiscard]] std::vector<games::GameCommand> commands() const override;
    void run_command(std::string_view id) override;
    // Development scripts: "tap <x> <y>" (0..1 of the view), "pause", "resume",
    // "detail light|balanced|fine". Refused outside --dev and while hidden.
    bool scripted_action(std::string_view action);

    // For tests and measurement.
    [[nodiscard]] bool scene_ready() const;
    [[nodiscard]] bool paused() const {
        return settings_.paused;
    }
    [[nodiscard]] Rates rates() const;

  private:
    struct Bundle {
        SceneData scene{};
        std::unique_ptr<Look> look{};
        ShadowMap shadows{};
        Foliage foliage{};
        std::string error{};
    };
    static std::unique_ptr<Bundle> load_bundle(std::filesystem::path archive, LookFactory make_look);

    SceneSetup setup_;
    ViewOptions options_;
    Settings settings_{};
    std::future<std::unique_ptr<Bundle>> loading_{};
    std::unique_ptr<Bundle> bundle_{};  // must outlive stage_
    std::future<FixedLayer> building_{};
    SceneSize building_size_{};
    std::unique_ptr<Stage> stage_{};
    std::vector<Creature> creatures_{};
    Governor governor_{};
    games::SceneAudio audio_{};
    std::shared_ptr<gf::LiveSurface> surface_{};
    std::unique_ptr<gf::Timer> timer_{};
    std::vector<gf::SubscriptionToken> subscriptions_{};
    std::vector<std::uint32_t> columns_{};  // device column -> scene column
    int columns_width_{};                    // the scene width columns_ was built for
    std::chrono::steady_clock::time_point last_tick_{};
    std::chrono::steady_clock::time_point last_arrange_{};
    double scene_time_{};   // seconds of animation shown so far
    double light_time_{};   // the animated light's clock; held under reduced motion
    double last_sway_{-1};  // scene time of the last foliage update
    double scale_{1};
    int device_width_{};
    int device_height_{};
    SceneSize wanted_{};
    bool render_dirty_{true};
    bool direct_{};
    bool front_{true};
    bool music_{true};
    bool sound_{true};
    bool reduced_{};
    bool help_open_{};
    bool window_active_{true};

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void on_window_active(bool active);
    void tick();
    void request_frame();
    void poll_workers();
    void start_build();
    [[nodiscard]] bool animating() const;
    // Returns the seconds spent updating foliage, if it was due.
    double render();
    void publish();
    void draw_help(std::byte* pixels, std::size_t row_bytes);
    void tap(double x, double y);
    void set_paused(bool paused);
    void set_detail(Detail detail);
    void set_help(bool open);
    void sync_sound();
    void update_limits();
    void persist() const;
    void restore();
    [[nodiscard]] std::filesystem::path settings_path() const;
};

} // namespace ambient
