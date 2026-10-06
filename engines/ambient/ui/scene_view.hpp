#pragma once
// A living scene as a PlaySuite control. The view hosts one Scenery and owns what
// every scene shares:
//
//   * the window surface, enlarged from the scene's raster;
//   * a governed clock: frames at rates that keep the scene inside its processor
//     budget (cadence.hpp), half the budget while the window is not active, and no
//     timer at all while paused, hidden or occluded;
//   * Pause, Detail and Help commands, the Motion master, remembered settings,
//     pointer gestures (press, drag, release), sound routing (SoundDesk), and a
//     help card when no shell is there to show help.
#include "suite.hpp"

#include "cadence.hpp"
#include "present.hpp"
#include "scenery.hpp"
#include "settings.hpp"
#include "sound_desk.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"

#include <chrono>
#include <filesystem>
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

// What the view needs to know about the game hosting a scene.
struct HostSetup {
    std::string id;               // the game id
    std::string settings_file;    // in the state directory, e.g. "my_scene-v1.txt"; dev uses "...-dev.txt"
    std::string accessible_name;  // read by assistive technology
    std::string help_title;
    std::vector<std::string> help_paragraphs;  // the standalone help card
    CadenceLimits limits{};
    std::uint32_t backdrop{0x0B1A14};  // 0xRRGGBB shown until the scene is ready
};

class SceneView : public gf::Control, public games::CommandSource {
  public:
    SceneView(gf::StableId id, HostSetup setup, std::unique_ptr<Scenery> scenery, ViewOptions options);
    ~SceneView() override;
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
    // Development scripts: "pause", "resume", "detail light|balanced|fine", then
    // anything the scenery accepts. Refused outside --dev and while hidden.
    bool scripted_action(std::string_view action);

    [[nodiscard]] bool scene_ready() const;
    [[nodiscard]] bool paused() const {
        return settings_.paused;
    }
    [[nodiscard]] Rates rates() const;

  private:
    HostSetup setup_;
    std::unique_ptr<Scenery> scenery_;
    ViewOptions options_;
    Settings settings_{};
    Governor governor_{};
    SoundDesk sound_{};
    std::shared_ptr<gf::LiveSurface> surface_{};
    std::unique_ptr<gf::Timer> timer_{};
    std::vector<gf::SubscriptionToken> subscriptions_{};
    std::vector<std::uint32_t> columns_{};  // device column -> scene column
    int columns_width_{};                    // the scene width columns_ was built for
    std::chrono::steady_clock::time_point last_tick_{};
    std::chrono::steady_clock::time_point last_arrange_{};
    double scene_time_{};
    double scale_{1};
    int device_width_{};
    int device_height_{};
    SceneSize wanted_{};
    bool render_dirty_{true};
    bool direct_{};
    bool rgba_{};         // the live surface is RGBA (the host's own order), not BGRA
    bool front_{true};
    bool music_{true};
    bool sound_on_{true};
    bool sound_muted_{};  // the scene's own menu mutes
    bool music_muted_{};
    bool reduced_{};
    bool help_open_{};
    bool window_active_{true};
    bool dragging_{};

    // Pacing and presentation counters for measurement, printed to the error stream
    // when the view detaches if GAMES_SCENE_TRACE is set (see Stillwater's HANDOFF.md).
    struct Trace {
        bool on{};
        std::uint64_t frames{};        // pictures drawn
        std::uint64_t lease_misses{};  // pictures drawn but not presented (surface busy)
        std::uint64_t late_frames{};   // frames more than half an interval late
        double gap_sum{};              // seconds between drawn frames
        double gap_square{};
        double gap_max{};
        double draw_sum{};             // the scenery's drawing, seconds
        double draw_max{};
        double present_sum{};          // enlarging and publishing, seconds
        double present_max{};
        std::chrono::steady_clock::time_point last_frame{};
    };
    Trace trace_{};
    void report_trace() const;

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void on_window_active(bool active);
    void tick();
    void request_frame();
    [[nodiscard]] bool animating() const;
    [[nodiscard]] SceneContext context();
    void finish(const SceneContext& context);
    void publish(const std::vector<std::uint32_t>* picture, int width, int height);
    void draw_help(std::byte* pixels, std::size_t row_bytes);
    [[nodiscard]] std::uint32_t in_order(std::uint32_t rgb) const;
    void set_paused(bool paused);
    void set_detail(Detail detail);
    void set_help(bool open);
    void update_limits();
    void persist() const;
    void restore();
    [[nodiscard]] std::filesystem::path settings_path() const;
};

} // namespace ambient
