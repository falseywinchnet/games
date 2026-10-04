#pragma once
// The game as a PlaySuite control. This is the only file that knows about
// GUI.Forms and the shell. It turns input into moves, keeps the visual state
// moving while there is something to animate, publishes finished frames to a
// LiveSurface, and offers its commands to the command capsule.
//
// It does no work while nothing changes: the timer stops once the picture has
// settled and starts again on input, a command, a resize or a return from the
// shelf. See new-games/guide/05-performance.md.
#include "suite.hpp"

#include "platform/raster.hpp"
#include "rules.hpp"
#include "stage.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tg {
namespace gf = gui_forms;

struct Options {
    bool hosted = false;  // inside PlaySuite: the capsule carries the commands
    bool dev = false;     // a separate save file, for tests and development
};

class TemplateView final : public gf::Control, public games::CommandSource {
public:
    TemplateView(gf::StableId id, Options options);
    ~TemplateView() override;
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree() {}
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void on_pointer(gf::PointerEvent& event) override;
    void on_key(gf::KeyEvent& event) override;
    void on_key_bubble(gf::KeyEvent& event) override {
        on_key(event);
    }

    // The shell calls these. activate: the game has just been shown; take the keyboard.
    void activate();
    // foreground false means the shelf or another game is showing: stop everything.
    void set_cabinet(bool foreground, bool music, bool sound, bool reduced = false);
    [[nodiscard]] std::vector<games::GameCommand> commands() const override;
    void run_command(std::string_view id) override;

private:
    Options options_;
    Session session_;
    Layout layout_;
    Visual visual_;
    Canvas frame_;
    std::shared_ptr<gf::LiveSurface> surface_;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subscriptions_;
    std::chrono::steady_clock::time_point last_tick_{};
    double scale_ = 1;        // device pixels per point
    int device_width_ = 0;    // the surface, in device pixels
    int device_height_ = 0;
    bool render_dirty_ = true;
    bool direct_ = false;     // the host presents the surface without a repaint
    bool front_ = true;
    bool music_ = true;
    bool sound_ = true;
    bool reduced_ = false;

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void request_frame();
    void publish();
    void play(const std::string& name, float gain, float rate);
    void sync_music();

    void press_cell(int cell);
    void undo_press();
    void new_board();
    void cycle_size();
    void set_help(bool open);
    void move_cursor(int rows, int columns);
    void persist();
    [[nodiscard]] bool restore();
    [[nodiscard]] std::filesystem::path save_path() const;
};

}  // namespace tg
