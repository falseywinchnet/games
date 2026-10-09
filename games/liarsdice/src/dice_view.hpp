#pragma once
#include "suite.hpp"
// The game as a GUI.Forms control. The Keeper's wagers; the crew sitting
// down and greeting you; cups shaken; bids round the table (theirs spoken,
// with a tell now and then; yours composed in the panel); "Liar!", the cups
// lifted and the dice counted one by one; a die lost to the Keeper's jar; the
// game won or lost and the ledger settled. The logbook of the crew, records,
// help, and autosave.
#include "cabin.hpp"
#include "save.hpp"
#include "platform/present.hpp"
#include "pixel_surface.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace ld {
namespace gf = gui_forms;

struct Options {
    bool hosted = false;
    bool dev = false;  // separate save; LD_SCRIPT allowed
};

class DiceView final : public gf::Control, public games::CommandSource {
public:
    DiceView(gf::StableId id, Options opt);
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
    // Ticks the game has run: the count advances whether or not the picture changes.
    std::uint64_t ticks() const { return ticks_; }

private:
    enum class Phase { wagers, greet, shake, turn, act, reveal, result, freedom };
    enum class Panel { none, help, logbook, records };
    struct Button { std::string id, label; int x, y, w, h; bool enabled = true; int style = 0; };  // style: 0 plain, 1 danger, 2 die face, 3 card
    struct HiText { std::string s; int font; double size; int wrap; int x, y; Col c; };
    struct Bubble { int seat; std::string text; double age = 0; double life = 3; int shown = 0; };  // seat -1: the Keeper; 0: you

    Options opt_;
    Ledger L_;
    std::vector<Wager> offers_;
    Rng rng_{1};
    Phase phase_ = Phase::wagers;
    double phase_t_ = 0;
    Panel panel_ = Panel::none;
    // the game
    std::vector<bool> hist_tell_;         // per bid this round: whether the bidder showed their tell
    int greet_next_ = 0;
    double think_ = 0;                    // an opponent's thinking time left
    Reveal rv_;
    std::vector<std::pair<int, int>> count_list_;  // the dice that count, (seat, index), in the order they're counted
    int counted_ = 0;
    int lifted_ = 0;
    bool sunk_ = false, settled_ = false;
    bool won_ = false;
    std::string result_line_;
    int sel_qty_ = 1, sel_face_ = 2;
    std::vector<double> tell_t_;          // per seat: seconds of tell left
    std::deque<Bubble> bubbles_;
    std::string status_;

    Cabin cab_;
    CabinState st_;
    Canvas frame_;
    render::PixelSurface pixels_;     // the window end: the small frame enlarged, and the text
    bool pixels_attached_ = false;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subs_;
    std::chrono::steady_clock::time_point last_{};
    double t_ = 0, save_t_ = 0, speed_ = 1;
    std::uint64_t ticks_ = 0;
    bool dirty_ = false;
    bool auto_ = false;                   // dev: you play yourself

    std::vector<Button> buttons_;
    std::string pressed_, hover_;
    int hover_seat_ = -1;
    double mouse_x_ = 0, mouse_y_ = 0;
    bool cab_reduced_ = false;
    bool cab_front_ = true, cab_music_ = true, cab_sound_ = true;
    std::vector<std::pair<double, std::string>> script_;

    int pixel_ = 2, panel_h_ = 104;
    int top_h_ = 0;             // two-row mode: a band at the top for the bid and the corner buttons
    bool wide_ = true;          // room for the one-row bid panel; otherwise two rows and corner buttons
    bool tabbed_ = false;       // the wagers as tabs over one card, when three cards side by side won't fit
    int sel_wager_ = 0;         // the wager shown when tabbed
    int log_page_ = 0;          // the logbook's page, when it can't show all 32 at once
    mutable double ov_size_ = 11;
    double bs_ = 2;
    int pw_ = 0, ph_ = 0, phys_w_ = 0, phys_h_ = 0;

    std::vector<TextSprite> sprites_;
    std::vector<HiText> texts_;

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void publish();
    // game
    Match& m() { return L_.match; }
    const Character& ch(int seat) const { return cast()[static_cast<size_t>(L_.wager.seats[static_cast<size_t>(seat - 1)])]; }
    void show_wagers();
    void take_wager(int i);
    void setup_table(bool fresh_round);
    void begin_round();
    void start_turn();
    void ai_move();
    void player_bid();
    void call_liar();
    void step_reveal(double dt);
    void settle_reveal();
    void end_match(bool won);
    void say(int seat, const std::string& text, double life = 0);
    void reset_composer();
    void persist();
    void play(const std::string& name, float gain = 1, float rate = 1);
    void action(const std::string& id);
    void open(Panel p);
    void layout_buttons();
    void layout_game_buttons();
    struct Box { int x, y, w, h; };
    Box overlay_box() const;                     // the help/records/logbook card, sized to the window and its words
    std::vector<std::string> overlay_lines() const;
    int logbook_rows() const;                    // rows per column, and columns, the logbook fits
    int logbook_cols() const;
    int wager_card_h(const Wager& w, int cw) const;
    void wager_details(const Wager& w, int x, int y, int cw, bool brief);
    void run_script();
    void animate(double dt);
    // drawing
    void compose();
    void draw_plates();
    void draw_bid_plaque();
    void draw_panel_match();
    void draw_wagers();
    void draw_bubbles();
    void draw_overlay_panel();
    void draw_button(const Button& b);
    void draw_die_icon(int x, int y, int size, int value, Col body, Col pip, bool lit = false);
    const Mask& tmask(const std::string& s, int font, double size, int wrap_game) const;
    int text(const std::string& s, int x, int y, Col c, double size = 11, int font = 0, int wrap = 0);
    int text_w(const std::string& s, double size, int font) const;
    int text_h(const std::string& s, double size, int font, int wrap = 0) const;
    // The texts over the frame at window resolution: what they show and where, and drawn
    // inside `clip`.
    [[nodiscard]] std::vector<render::PixelSurface::Text> text_marks(double k) const;
    void blit_texts(const render::Target& target, render::Rect clip, double k);
};

}  // namespace ld
