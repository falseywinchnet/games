#pragma once
// The game as a GUI.Forms control: the meadow, a stone where you click, the
// sheep's answering hop (or munch, or escape), penned and sulking or off and
// away; undo, restart, a hint, stars against par, the map of meadows played,
// endless new meadows made in the background, and autosave.
#include "pasture.hpp"
#include "platform/present.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace sh {
namespace gf = gui_forms;

struct Options {
    bool dev = false;  // separate save; SH_SCRIPT allowed
};

class SheepView final : public gf::Control {
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
    void set_cabinet(bool foreground, bool music, bool sound) { cab_front_ = foreground; cab_music_ = music; cab_sound_ = sound; }

private:
    enum class Phase { loading, play, answer, won, lost };
    enum class Panel { none, help, map };
    struct Button { std::string id, label; int x, y, w, h; bool enabled = true; };
    struct Box { int x, y, w, h; };
    struct HiText { std::string s; int font; double size; int wrap; int x, y; Col c; };

    Options opt_;
    // progress
    int level_ = 1, reached_ = 1;
    std::vector<int> stars_;           // per level (index level-1): 0 not penned yet, 1..3
    std::vector<int> moves_;           // stones placed this level, in order (for resume and undo)
    bool sound_ = true, music_ = true;
    // the meadow
    Level lvl_;
    Meadow m_;
    std::vector<Meadow> undo_;
    std::future<Level> pending_;
    int pending_level_ = 0;
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
    std::shared_ptr<gf::LiveSurface> surface_;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subs_;
    std::chrono::steady_clock::time_point last_t_{};
    double t_ = 0, save_t_ = 0;
    bool dirty_ = false;
    Panel panel_ = Panel::none;
    std::vector<Button> buttons_;
    std::string pressed_, hover_;
    double mouse_x_ = 0, mouse_y_ = 0;
    bool cab_front_ = true, cab_music_ = true, cab_sound_ = true;
    std::vector<std::pair<double, std::string>> script_;

    int pixel_ = 2, top_h_ = 30, bottom_h_ = 34, btn_h_ = 18;
    bool compact_ = false;  // a small window: slimmer bars, shorter labels
    int map_page_ = -1;          // -1: the page holding the current meadow
    mutable double help_size_ = 11;
    double bs_ = 2;
    int pw_ = 0, ph_ = 0, phys_w_ = 0, phys_h_ = 0;
    std::vector<int> xmap_;
    bool direct_ = false;
    NativePresenter presenter_;
    std::vector<TextSprite> sprites_;
    std::vector<HiText> texts_;

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void publish();
    // game
    void load_level(int level, bool resume);
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
    void blit_texts(std::uint32_t* dst, size_t stride_px, double k);
};

}  // namespace sh
