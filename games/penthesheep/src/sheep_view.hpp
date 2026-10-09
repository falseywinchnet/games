#pragma once
#include "suite.hpp"
// The game as a GUI.Forms control: the meadow, a stone where you click, the
// sheep's answering hop (or munch, or escape), penned and sulking or off and
// away; undo, restart, a hint, stars against par, new meadows at Easy, Medium
// or Hard made in the background, and autosave.
#include "pasture.hpp"
#include "platform/present.hpp"
#include "pixel_surface.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace sh {
namespace gf = gui_forms;

struct Options {
    bool hosted = false;
    bool dev = false;  // separate save; SH_SCRIPT allowed
};

class SheepView final : public gf::Control, public games::CommandSource {
public:
    SheepView(gf::StableId id, Options opt);
    ~SheepView() override;
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree() {}
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& p, gf::Rect damage) override;
    void on_pointer(gf::PointerEvent& e) override;
    void on_key(gf::KeyEvent& e) override;
    void on_key_bubble(gf::KeyEvent& e) override { on_key(e); }
    void activate();
    bool ready_for_play() const { return phase_ != Phase::loading; }
    std::vector<games::GameCommand> commands() const override;
    void run_command(std::string_view id) override;
    std::vector<games::GameSetting> settings() const override;
    void change_setting(std::string_view id, double value) override;
    void set_cabinet(bool foreground, bool music, bool sound, bool reduced = false);
    // Ticks the game has run: the count advances whether or not the picture changes.
    std::uint64_t ticks() const { return ticks_; }

private:
    enum class Phase { loading, play, answer, won, lost };
    enum class Panel { none, help };
    struct Button { std::string id, label; int x, y, w, h; bool enabled = true; };
    struct Box { int x, y, w, h; };
    struct HiText { std::string s; int font; double size; int wrap; int x, y; Col c; };

    Options opt_;
    // progress
    int difficulty_ = 0;               // the meadow in play: 0 easy, 1 medium, 2 hard
    int next_difficulty_ = 0;          // the Level setting, for the next new meadow
    std::uint64_t seed_ = 1;           // the meadow in play
    std::vector<int> moves_;           // stones placed this level, in order (for resume and undo)
    bool sound_ = true, music_ = true;
    // the meadow
    Level lvl_;
    Meadow m_;
    std::vector<Meadow> undo_;
    std::future<Level> pending_;
    bool hinted_ = false;
    int hint_ = -1;
    Phase phase_ = Phase::loading;
    double phase_t_ = 0;
    SheepMove last_;
    int from_cell_ = 0;
    std::string say_;                  // a line from the meadow (the sheep's thoughts)
    double say_t_ = 0;

    Pasture pas_;
    PastureState st_;
    Canvas frame_;
    render::PixelSurface pixels_;     // the window end: the small frame enlarged, and the text
    bool pixels_attached_ = false;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subs_;
    std::chrono::steady_clock::time_point last_t_{};
    double t_ = 0, save_t_ = 0;
    std::uint64_t ticks_ = 0;
    bool dirty_ = false;
    Panel panel_ = Panel::none;
    std::vector<Button> buttons_;
    std::string pressed_, hover_;
    double mouse_x_ = 0, mouse_y_ = 0;
    bool cab_reduced_ = false;
    bool cab_front_ = true, cab_music_ = true, cab_sound_ = true;
    std::vector<std::pair<double, std::string>> script_;

    int pixel_ = 2, top_h_ = 30, bottom_h_ = 34, btn_h_ = 18;
    bool compact_ = false;  // a small window: slimmer bars, shorter labels
    mutable double help_size_ = 11;
    double bs_ = 2;
    int pw_ = 0, ph_ = 0, phys_w_ = 0, phys_h_ = 0;

    std::vector<TextSprite> sprites_;
    std::vector<HiText> texts_;

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void publish();
    // game
    // A fresh meadow at the Level setting.
    void new_meadow();
    // Makes the meadow for difficulty_ and seed_ in the background; `resume` replays moves_.
    void start_meadow(bool resume);
    void begin_level();
    void stone(int cell);
    void undo();
    void restart();
    void hint();
    void finish(bool won);
    int star_count() const;
    void persist();
    bool load();
    void play(const std::string& name, float gain = 1, float rate = 1);
    void action(const std::string& id);
    void open(Panel p);
    void layout_buttons();
    void layout_game_buttons();
    Box result_box() const;
    Box help_box() const;
    std::vector<std::string> help_lines() const;
    void run_script();
    void animate(double dt);
    // drawing
    void compose();
    void draw_bars();
    void draw_result();
    void draw_panel();
    void draw_button(const Button& b);
    void draw_star(double cx, double cy, double r, bool lit);
    const Mask& tmask(const std::string& s, int font, double size, int wrap_game) const;
    int text(const std::string& s, int x, int y, Col c, double size = 11, int font = 0, int wrap = 0);
    int text_w(const std::string& s, double size, int font) const;
    int text_h(const std::string& s, double size, int font, int wrap = 0) const;
    // The texts over the frame at window resolution: what they show and where, and drawn
    // inside `clip`.
    [[nodiscard]] std::vector<render::PixelSurface::Text> text_marks(double k) const;
    void blit_texts(const render::Target& target, render::Rect clip, double k);
};

}  // namespace sh
