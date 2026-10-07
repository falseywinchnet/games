#pragma once
// Nature Cube as a PlaySuite control: turns pointer input into presses and steps on the
// rules, advances the motion while anything moves, draws the lake, the cube and the top
// scores card into a framebuffer and publishes it to a LiveSurface, and offers its
// commands and its Level setting to the shell. Boards for the next game are dealt in the
// background while the player plays, so a Hard board is ready when it is wanted.
//
// It does no work while nothing changes: the timer stops once the picture has settled
// and starts again on input, a command, a resize or a return from the shelf.
#include "suite.hpp"

#include "generator.hpp"
#include "picture.hpp"
#include "raster3d.hpp"
#include "session.hpp"
#include "stage.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ps_cube {
namespace gf = gui_forms;

struct Options {
    bool hosted = false;  // inside PlaySuite: the capsule carries the commands
    bool dev = false;     // a separate save file and scripted actions, for tests
};

class CubeView final : public gf::Control, public games::CommandSource {
public:
    CubeView(gf::StableId id, Options options);
    ~CubeView() override;
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree() {}
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void on_pointer(gf::PointerEvent& event) override;
    void on_key(gf::KeyEvent& event) override;
    void on_key_bubble(gf::KeyEvent& event) override {
        on_key(event);
    }
    void on_text_input(gf::TextInputEvent& event) override;

    void activate();
    void set_cabinet(bool foreground, bool music, bool sound, bool reduced = false);
    [[nodiscard]] std::vector<games::GameCommand> commands() const override;
    void run_command(std::string_view id) override;
    [[nodiscard]] std::vector<games::GameSetting> settings() const override;
    void change_setting(std::string_view id, double value) override;
    // --dev only: "deal <level> <seed>", "portal <level> <seed>", "lines <count>",
    // "through" (the lines through portals), "finish" (draws every line), "unranked".
    bool scripted_action(std::string_view action);
    [[nodiscard]] bool editing_name() const {
        return panel_ == Panel::name;
    }

    // For tests: where a cell is drawn, in the view's points, or nothing if hidden.
    [[nodiscard]] std::optional<gf::Point> cell_point(int cell) const;
    [[nodiscard]] const Session& session() const {
        return session_;
    }
    [[nodiscard]] bool settled() const;

private:
    enum class Panel { none, scores, name, help };
    struct Job {
        std::future<Puzzle> board;
        std::shared_ptr<std::atomic<bool>> cancel;
        int level = 0;
    };

    Options options_;
    Session session_;
    Motion motion_;
    Layout layout_;
    Raster3D raster_;
    Panel panel_ = Panel::none;
    std::string name_entry_;
    Job next_;
    std::vector<Job> abandoned_;
    bool waiting_ = false;      // a new board is wanted and still being dealt
    bool tracing_ = false;
    bool seeking_ = false;      // after a hop: waiting for the pointer to reach the far side
    bool finish_on_show_ = false;
    bool unranked_ = false;  // --dev only
    int hover_ = -1;

    Picture lake_;
    std::vector<std::uint8_t> backdrop_;  // the lake at the surface's size, BGRA
    std::vector<std::uint8_t> frame_;     // the picture being published, BGRA premultiplied
    int device_width_ = 0;
    int device_height_ = 0;
    bool backdrop_dirty_ = true;
    bool frame_has_panel_ = false;
    std::shared_ptr<gf::LiveSurface> surface_;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subscriptions_;
    std::chrono::steady_clock::time_point last_tick_{};
    long long interval_ = 0;  // the timer's current interval in milliseconds, 0 when stopped
    double scale_ = 1;
    bool render_dirty_ = true;
    bool direct_ = false;
    bool front_ = true;
    bool music_ = true;
    bool sound_ = true;
    bool reduced_ = false;
    bool environment_loaded_ = false;

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void request_frame();
    void pace(long long milliseconds);
    void render_frame();
    void paint_panel();
    // Draws text into the frame at (x, y) points; returns its width in points.
    double draw_text(const std::string& words, double x, double y, double size, bool bold,
                     std::uint32_t color, double wrap = 0);
    void publish(gf::Rect damage);
    void set_help(bool open);

    void start_next();
    void abandon_next();
    void take_board(Puzzle puzzle);
    void deal_next();
    void new_board();
    void finished();
    void after_step(std::size_t before, int pair);
    void close_panel();
    void confirm_name();
    [[nodiscard]] int cell_under(gf::Point local) const;
    void lean_toward(gf::Point local);

    void persist();
    [[nodiscard]] bool restore();
    [[nodiscard]] std::filesystem::path save_path() const;
};

}  // namespace ps_cube
