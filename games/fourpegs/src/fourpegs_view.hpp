#pragma once
// The game as a GUI.Forms control: frame loop, input (drag or click pegs),
// the test sheet of guesses, his speech and voice, the adaptive score, dialogs
// and autosave. The frame is a small pixel-art image the compositor
// magnifies; text is drawn crisply on top as cached layers.
#include "board.hpp"
#include "lair.hpp"
#include "save.hpp"
#include "vactor.hpp"
#include "game_text.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"
#include "gui_forms/window.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace fp {
namespace gf = gui_forms;

struct Options {
    bool dev = false;  // separate save file; FP_SCRIPT allowed
    bool hosted = false;  // a host shell supplies New, Help, Top scores and the audio switches
};

class FourPegsView final : public gf::Control {
public:
    bool editing_name() const { return panel_ == Panel::name; }
    FourPegsView(gf::StableId id, Options opt);
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
    [[nodiscard]] const Board& board() const { return board_; }

private:
    enum class Panel { none, help, scores, name, gameover };
    struct Button { std::string id, label; int x, y, w, h; };
    struct HiText { std::string s; bool bold; double size; int wrap; int x, y; Col c; };

    // Pending rendering owns a stable copy; simulation and audio keep advancing.
    struct RenderState {
        Board board_{1};
        SaveData save_{};
        LairState st_{};
        std::string plan_, say_text_, name_entry_, pressed_, hover_;
        std::vector<Button> buttons_;
        Panel panel_ = Panel::none;
        bool checking_ = false;
        int sheet_rows_ = 0, say_shown_ = 0, pending_score_ = 0;
        double blackout_ = 0, t_ = 0;
    };
    std::unique_ptr<RenderState> rendering_;
    bool rendering_pending_ = false;
    std::unique_ptr<games::GameText> game_text_;
    void capture_render_state();
    void layout_render_buttons();

    Options opt_;
    SaveData save_;
    Board board_;
    VillainActor actor_;
    Lair lair_;
    LairState st_;
    Canvas frame_;
    std::shared_ptr<gf::LiveSurface> surface_;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subs_;
    std::chrono::steady_clock::time_point last_{};
    double t_ = 0, save_t_ = 0;
    bool dirty_ = false;

    // the guess in flight: shown in the sockets until its pins have lit
    bool checking_ = false;
    Code shown_{};
    double pins_t_ = -1;
    int pins_played_ = 0;
    int sheet_rows_ = 0;      // rows visible on the test sheet (a judged row appears once its pins light)
    int tier_ = 1;
    int pending_score_ = 0;
    double shake_ = 0;
    double doom_t_ = -1;       // seconds into the collapse after he wins (-1: not happening)
    double blackout_ = 0;      // 0..1 the screen going dark before GAME OVER
    int crashes_ = 0;          // crash sounds played so far in the collapse

    // pointer and dragging
    double mouse_x_ = 0, mouse_y_ = 0, down_x_ = 0, down_y_ = 0;
    bool mouse_in_ = false, mouse_down_ = false, dragging_ = false;
    int drag_color_ = -1, drag_from_ = -1;  // drag_from_: the socket it left, or -1 from the palette
    int selected_color_ = -1, selected_slot_ = -1, auto_slot_ = -1;

    // speech
    std::string say_text_;
    double say_age_ = 0;
    int say_shown_ = 0;

    Panel panel_ = Panel::none;
    std::vector<Button> buttons_;
    std::string pressed_, hover_;
    std::string name_entry_;
    bool cab_front_ = true, cab_music_ = true, cab_sound_ = true;
    bool cab_reduced_ = false;
    std::vector<std::pair<double, std::string>> script_;  // dev: FP_SCRIPT="t:code,..."

    int pixel_ = 2;
    double bs_ = 2;
    int pw_ = 0, ph_ = 0, phys_w_ = 0, phys_h_ = 0;
    std::vector<int> xmap_;
    bool direct_ = false;
    std::vector<HiText> texts_;

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void on_focus_changed(bool focused) override;
    void active_changed(bool active);
    void capture_changed(const gf::PointerCaptureChange& change);
    void cancel_drag();
    void tick();
    void publish();
    void new_game();
    void place(int slot, int color);
    void clear_slot(int slot);
    void check();
    void judged_tick(double dt);
    void speech_tick(double dt);
    void music_tick();
    void doom_tick(double dt);
    void draw_gameover();
    void persist();
    void play(const std::string& name, float gain = 1, float rate = 1);
    void action(const std::string& id);
    void open(Panel p);
    void layout_buttons();
    void run_script();
    // drawing
    void compose();
    void draw_sheet();
    void draw_bubble();
    void draw_panel();
    void draw_button(const Button& b);
    games::TextImage tmask(const std::string& s, bool bold, double size, int wrap_game);
    int text(const std::string& s, int x, int y, Col c, double size = 11, bool bold = false, int wrap = 0);
    int text_w(const std::string& s, double size, bool bold);
    int text_h(const std::string& s, double size, bool bold, int wrap = 0);
    void blit_texts(std::uint32_t* dst, size_t stride_px, double k);
};

}  // namespace fp
