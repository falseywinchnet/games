#pragma once
// Specimen Cabinet as a PlaySuite control: the only file that knows about GUI.Forms and the
// shell. It turns the pointer and the keys into a stone in hand (taken from the pile, the
// felt or a socket, carried, and set in a socket or laid on the felt); it turns the tumbler
// while it drops a stone onto the pile; it draws through the render engine's live surface;
// it offers its commands to the capsule and saves after every change.
//
// A settled table does no work: the timer stops once nothing moves (the clock aside, which
// shows a new second once a second while a cabinet is open), and starts again on input, a
// command, a resize or a return from the shelf.
#include "suite.hpp"

#include "rules.hpp"
#include "scene.hpp"
#include "stage.hpp"
#include "surface.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/threading.hpp"
#include "gui_forms/timer.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sc {
namespace gf = gui_forms;

struct Options {
    bool hosted = false;  // inside PlaySuite: the capsule carries the commands
    bool dev = false;     // a separate save file, for tests and development
};

class SpecimenCabinetView final : public gf::Control, public games::CommandSource {
public:
    SpecimenCabinetView(gf::StableId id, Options options);
    ~SpecimenCabinetView() override;
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
    [[nodiscard]] std::vector<games::GameSetting> settings() const override;
    void change_setting(std::string_view id, double value) override;
    // Development scripts (--dev only): "take", "set <cell>", "felt <x> <y>" (each 0..1),
    // "solve [count]" (that many stones, or all, set where the answer has them), "size <n>".
    bool scripted_action(std::string_view action);

private:
    Options options_;
    Session session_;
    Scene scene_;
    Layout layout_;
    render::Surface surface_;
    bool surface_attached_ = false;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subscriptions_;
    std::chrono::steady_clock::time_point last_tick_{};
    double scale_ = 1;
    double width_ = 0, height_ = 0;  // points
    int device_width_ = 0;
    int device_height_ = 0;
    bool laid_out_ = false;
    bool render_dirty_ = true;
    bool front_ = true;
    bool music_ = true;
    bool sound_ = true;
    bool reduced_ = false;
    bool felt_given_ = false;

    Panel panel_ = Panel::none;
    int choosing_ = 1;            // the chooser's size, an index into sizes
    bool new_best_ = false;
    int pressed_button_ = -1;
    bool help_ = false;
    int shown_second_ = -1;       // the clock's second last drawn

    // The stone in hand. Pressed and dragged, it goes where it is let go; clicked, it is
    // carried until the next click.
    int held_ = -1;
    bool carrying_ = false;
    bool pressing_ = false;       // the button is down on the held stone, not yet a drag
    bool dragging_ = false;
    double press_x_ = 0, press_y_ = 0;
    double grip_x_ = 0, grip_y_ = 0;    // the pointer's offset from the stone's centre
    double held_x_ = 0, held_y_ = 0;    // the held stone's centre, points
    int target_ = -1;             // the socket it is over
    int cursor_ = -1;             // the keyboard's socket
    bool keyboard_ = false;       // the keyboard was used last

    // The tumbler: stones the rules have dropped onto the pile, shown falling one by one.
    std::vector<int> waiting_;
    double turn_ = 0;             // the drum's turn, radians
    double drop_time_ = 0;        // into the current drop: turning, then falling
    double drop_wait_ = -1;       // a new cabinet's drops, held until its stones are drawn

    std::unique_ptr<gf::Worker> studio_;
    std::atomic<bool> studio_done_{false};

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void window_activation(bool active);
    void tick();
    void keep_studio_busy();
    void stop_studio() noexcept;
    static void run_studio(const gf::CancellationFlag& cancelled, void* context);
    void request_frame();
    void publish();
    void play(const std::string& name, float gain, float rate);
    void sync_music();
    [[nodiscard]] Frame frame() const;
    [[nodiscard]] bool clock_running() const;
    [[nodiscard]] bool dropping() const;
    void step_drops(double seconds);

    // The game.
    void take(int stone, double x, double y);
    void let_go(double x, double y);
    void put_back();
    void set_held_in(int cell);
    void lay_held_on_felt(double x, double y);
    void show_outcome(const Outcome& outcome);
    void take_from_pile();
    void move_cursor(int rows, int columns);
    void aim_at_cursor();
    void open_chooser();
    void set_help(bool open);
    void begin_game(int n);
    [[nodiscard]] int panel_button(double x, double y) const;
    void press_panel_button(int button);
    void cancel_press();

    void persist();
    [[nodiscard]] bool restore();
    [[nodiscard]] std::string_view save_name() const;
};

}  // namespace sc
