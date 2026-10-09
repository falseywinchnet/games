#pragma once
#include "suite.hpp"
// The game as a GUI.Forms control: the parlor, the parrots' performance, the
// notebook of everything said, marking beaks honest or lying with
// contradictions shown as they appear, questions put to the tight-beaked, the
// accusation and the verdict, the next table, and autosave.
#include "logic.hpp"
#include "parlor.hpp"
#include "script.hpp"
#include "platform/present.hpp"
#include "pixel_surface.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"

#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace pt {
namespace gf = gui_forms;

struct Options {
    bool hosted = false;
    bool dev = false;  // separate save; PT_SCRIPT allowed
};

class TableView final : public gf::Control, public games::CommandSource {
public:
    TableView(gf::StableId id, Options opt);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree() {}
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& p, gf::Rect damage) override;
    void on_pointer(gf::PointerEvent& e) override;
    void on_key(gf::KeyEvent& e) override;
    void on_key_bubble(gf::KeyEvent& e) override { on_key(e); }
    void activate();
    std::vector<games::GameCommand> commands() const override;
    void run_command(std::string_view id) override;
    void set_cabinet(bool foreground, bool music, bool sound, bool reduced = false);

private:
    enum class Phase { intro, play, verdict };
    enum class Panel { none, ask, accuse, help, records };
    struct Button { std::string id, label; int x, y, w, h; bool enabled = true; };
    struct HiText { std::string s; int font; double size; int wrap; int x, y; Col c; };
    struct Line { int who; std::string text; bool answer = false; int index = -1; };  // index: statement or answer number
    struct Bubble { int who; std::string text; double age = 0; double life = 4; int shown = 0; };

    Options opt_;
    // the table
    int level_ = 1;
    std::uint64_t seed_ = 1;
    Puzzle puz_;
    Script script_;
    std::vector<std::string> names_;
    std::vector<Manner> manners_;
    std::vector<int> species_;
    int crime_ = 0;
    std::vector<Asked> asked_;
    std::vector<int> marks_;          // per bird: 0 none, 1 honest, 2 liar
    int suspect_ = -1;                // chosen in the accusation dialog
    Phase phase_ = Phase::intro;
    double phase_t_ = 0;
    int intro_next_ = 0;
    bool won_ = false;
    std::vector<Line> notebook_;
    std::deque<Bubble> bubbles_;
    std::string opener_;
    std::vector<std::vector<std::string>> offer_text_;  // the questions on offer, worded once per table
    // records
    int solved_ = 0, played_ = 0, streak_ = 0, best_streak_ = 0;
    bool sound_ = true, music_ = true;

    Parlor parlor_;
    ParlorState st_;
    Canvas frame_;
    render::PixelSurface pixels_;     // the window end: the small frame enlarged, and the text
    bool pixels_attached_ = false;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subs_;
    std::chrono::steady_clock::time_point last_{};
    double t_ = 0, save_t_ = 0;
    bool dirty_ = false;
    std::uint64_t rng_ = 1;

    Panel panel_ = Panel::none;
    std::vector<Button> buttons_;
    std::string pressed_, hover_;
    int hover_bird_ = -1;
    double mouse_x_ = 0, mouse_y_ = 0;
    bool cab_reduced_ = false;
    bool cab_front_ = true, cab_music_ = true, cab_sound_ = true;
    std::vector<std::pair<double, std::string>> script_cmds_;
    std::vector<double> talk_;        // per bird: seconds of beak-flapping left
    std::vector<double> shake_;
    std::vector<double> glow_;
    mutable std::string contra_key_;
    mutable std::vector<bool> contra_;

    int pixel_ = 2, panel_h_ = 100;
    int top_h_ = 24;            // the host's band: one line, or two in a narrow window
    bool compact_ = false;
    int nb_first_ = 0;          // the notebook's first visible line, when it has more than fits
    bool nb_follow_ = true;     // keep the newest line in view (the introductions, an answer)
    int nb_name_w_ = 46;        // the speakers' column
    mutable double ov_size_ = 11.5;
    double bs_ = 2;
    int pw_ = 0, ph_ = 0, phys_w_ = 0, phys_h_ = 0;

    std::vector<TextSprite> sprites_;
    std::vector<HiText> texts_;

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void publish();
    // game
    void new_table(int level, std::uint64_t seed, bool fresh);
    void say(int who, const std::string& text, double life = 0);
    void mark(int bird);
    void ask(int bird, int option);
    void accuse(int bird);
    std::vector<bool> contradictions() const;  // per notebook line: impossible under the marks (and the suspect)
    bool count_wrong() const;
    void persist();
    bool load();
    void play(const std::string& name, float gain = 1, float rate = 1);
    void action(const std::string& id);
    void open(Panel p);
    void layout_buttons();
    void layout_game_buttons();
    void panel_box(int& wx, int& wy, int& ww, int& wh) const;
    std::vector<std::string> panel_lines() const;
    void scroll_notebook(int lines);
    void run_script();
    double rand01();
    // drawing
    void compose();
    void draw_top();
    void draw_notebook();
    void draw_bubbles();
    void draw_panel();
    void draw_button(const Button& b);
    const Mask& tmask(const std::string& s, int font, double size, int wrap_game) const;
    int text(const std::string& s, int x, int y, Col c, double size = 11, int font = 0, int wrap = 0);
    int text_w(const std::string& s, double size, int font) const;
    int text_h(const std::string& s, double size, int font, int wrap = 0) const;
    // The texts over the frame at window resolution: what they show and where, and drawn
    // inside `clip`.
    [[nodiscard]] std::vector<render::PixelSurface::Text> text_marks(double k) const;
    void blit_texts(const render::Target& target, render::Rect clip, double k);
};

}  // namespace pt
