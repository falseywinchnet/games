#pragma once
#include "game_text.hpp"
#include "pixel_surface.hpp"
// The game as a GUI.Forms control: frame loop, input (fire emitters, mark
// squares, pull the lever), the probe and reveal sequences, the score,
// dialogs and autosave. The frame is a small pixel-art image the compositor
// magnifies; text is drawn crisply on top as cached layers.
#include "box.hpp"
#include "chamber.hpp"
#include "save.hpp"
#include "platform/render.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace ap {
namespace gf = gui_forms;

struct Options {
    bool dev = false;  // separate save file; AP_SCRIPT allowed
    bool hosted = false;  // a host shell supplies New, Help, Top scores and the audio switches
};

class AtomProbeView final : public gf::Control {
public:
    bool editing_name() const { return panel_ == Panel::name; }
    AtomProbeView(gf::StableId id, Options opt);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree() {}
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& p, gf::Rect damage) override;
    void on_pointer(gf::PointerEvent& e) override;
    void on_key(gf::KeyEvent& e) override;
    void on_key_bubble(gf::KeyEvent& e) override { on_key(e); }
    void on_text_input(gf::TextInputEvent& e) override;
    void activate();
    // Cabinet hosting: the collection's master switches gate this game's own
    // music and sound; `foreground` is false while another game shows.
    void set_cabinet(bool foreground, bool music, bool sound, bool reduced_motion);
    // Host commands: "new", "help" and "scores" toggle like the in-frame buttons.
    void host_command(const std::string& id);
    // "help", "scores" or "" for the panel a host should show as active.
    [[nodiscard]] std::string host_panel() const;

private:
    enum class Panel { none, help, scores, name };
    struct Button { std::string id, label; int x, y, w, h; bool big = false; };
    struct HiText { std::string s; bool bold; double size; int wrap; int x, y; Col c; };
    struct Shot {
        int port = -1;      // -1: nothing in flight
        Probe pr;
        double t = 0;
        int stage = 0;      // 0 charging, 1 in the fog, 2 answered
    };

    // Stable render input while the live simulation and audio advance.
    struct RenderState {
        Box box_{1};
        SaveData save_{};
        ChamberState st_{};
        std::string message_, name_entry_, pressed_, hover_;
        Col message_col_{};
        std::vector<Button> buttons_;
        Panel panel_ = Panel::none;
        bool result_ = false;
        int pending_score_ = 0;
        double message_t_ = 0, t_ = 0;
    };
    std::unique_ptr<RenderState> rendering_;
    bool rendering_pending_ = false;
    std::unique_ptr<games::GameText> game_text_;
    void capture_render_state();
    void layout_render_buttons();

    Options opt_;
    SaveData save_;
    Box box_;
    Chamber ch_;
    ChamberState st_;
    Canvas frame_;
    render::PixelSurface pixels_;     // the window end: the small frame enlarged, and the text
    bool pixels_attached_ = false;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subs_;
    std::chrono::steady_clock::time_point last_{};
    double t_ = 0, save_t_ = 0;
    bool dirty_ = false;

    Shot shot_;
    double reveal_t_ = -1;     // seconds since the lever was pulled (-1: sealed)
    int replays_ = 0;          // beams replayed so far in the reveal
    int locks_ = 0;            // markers judged so far
    bool result_ = false;      // the reveal has finished: the result card shows
    int pending_score_ = 0;
    std::string message_;
    Col message_col_{1, 1, 1, 1};
    double message_t_ = 0;
    double look_x_ = 0, look_y_ = 0;
    int last_hover_port_ = -1;

    double mouse_x_ = 0, mouse_y_ = 0;
    bool mouse_in_ = false;

    Panel panel_ = Panel::none;
    std::vector<Button> buttons_;
    std::string pressed_, hover_;
    std::string name_entry_;
    bool cab_front_ = true, cab_music_ = true, cab_sound_ = true;
    bool cab_reduced_{};
    std::vector<std::pair<double, std::string>> script_;  // dev: AP_SCRIPT="t:code,..."

    int pixel_ = 2;
    double bs_ = 2;
    int pw_ = 0, ph_ = 0, phys_w_ = 0, phys_h_ = 0;
    std::vector<HiText> texts_;

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void cancel_press();
    void active_changed(bool active);
    void on_focus_changed(bool focused) override;
    std::string hit_button() const;
    void publish();
    void new_box();
    void sync_ports();
    void fire(int port);
    void mark(int cell, bool cross);
    void pull_lever();
    void shot_tick(double dt);
    void reveal_tick(double dt);
    void bolts_tick(double dt);
    void say(const std::string& s, Col c);
    void persist();
    void play(const std::string& name, float gain = 1, float rate = 1);
    void action(const std::string& id);
    void open(Panel p);
    void layout_buttons();
    void run_script();
    // drawing
    void compose();
    void draw_overlay();
    void draw_result();
    void draw_panel();
    void draw_button(const Button& b);
    games::TextImage tmask(const std::string& s, bool bold, double size, int wrap_game);
    int text(const std::string& s, int x, int y, Col c, double size = 11, bool bold = false, int wrap = 0);
    int text_w(const std::string& s, double size, bool bold);
    int text_h(const std::string& s, double size, bool bold, int wrap = 0);
    // The texts over the frame at window resolution: what they show and where, and drawn
    // inside `clip`.
    [[nodiscard]] std::vector<render::PixelSurface::Text> text_marks(double k);
    void blit_texts(const render::Target& target, render::Rect clip, double k);
};

}  // namespace ap
