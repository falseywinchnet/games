#pragma once
#include "suite.hpp"
// The Paradoxiary as a GUI.Forms control: the aviary, its birds flying in to settle on
// their perches, a bird's line in a popup when it is clicked (a question bird's questions
// on offer instead, until one is asked), marks the player puts on birds with a right
// click (truthful green, liar red), the accusation (the comic JE ACCUSE!, a crosshair and
// a paintball), the verdict and its points, the next table, and autosave.
#include "aviary.hpp"
#include "logic.hpp"
#include "perching.hpp"
#include "script.hpp"
#include "platform/present.hpp"
#include "pixel_surface.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"

#include <chrono>
#include <cstdint>
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
    // Ticks the game has run: the count advances whether or not the picture changes.
    std::uint64_t ticks() const { return ticks_; }

private:
    enum class Phase { arrive, play, aim, shot, verdict };
    enum class Panel { none, help, records };
    // a bird's mark: what the player thinks it is
    enum Mark : int { unmarked = 0, mark_truthful = 1, mark_liar = 2 };
    struct Button { std::string id, label; int x, y, w, h; bool enabled = true; Col tint{0, 0, 0, 0}; };
    struct HiText { std::string s; int font; double size; int wrap; int x, y; Col c; };
    // A popup: what a bird has just said, typed out beside it, or its questions on offer.
    struct Popup {
        int who = -1;
        std::string text;
        double age = 0;
        int shown = 0;
        bool questions = false;   // the question bird's offers, not a line
    };
    // A bird's way in: from high in the dome, perhaps to a perch it tries and leaves, to its own.
    struct Flight {
        V3 from{}, via{}, to{};
        int first_try = -1;
        double start = 0;         // seconds after the table opens
        double leg1 = 1.6, rest = 0, leg2 = 0;
    };

    Options opt_;
    // the table
    int level_ = 1;
    std::uint64_t seed_ = 1;
    Layout layout_;
    std::vector<Bird> flock_;
    std::vector<int> at_;             // each bird's place
    Puzzle puz_;
    Script script_;
    std::vector<std::string> names_;
    std::vector<Manner> manners_;
    std::vector<int> species_;
    int crime_ = 0;
    int asked_ = -1;                  // the offer asked, or -1
    bool answer_ = false;
    std::vector<int> marks_;
    std::vector<int> heard_;          // per bird: lines heard so far (a playing bird's first is a claim)
    std::vector<int> next_claim_;     // per bird: which claim comes round next
    int target_ = -1;                 // the bird the paintball is fired at
    double shot_t_ = 0;               // seconds since the paintball left
    V3 splat_{};                      // where it hit, on the bird's breast (bird space: up from the feet)
    bool splatted_ = false;
    int table_points_ = 0, liars_right_ = 0;
    Phase phase_ = Phase::arrive;
    double phase_t_ = 0;
    bool won_ = false;
    std::string opener_;
    std::vector<std::string> offer_text_;
    std::vector<Flight> flights_;
    Popup popup_;
    // records
    int solved_ = 0, played_ = 0, streak_ = 0, best_streak_ = 0, points_ = 0, best_points_ = 0;
    bool sound_ = true, music_ = true;

    Aviary aviary_;
    AviaryState st_;
    Canvas frame_;
    render::PixelSurface pixels_;     // the window end: the small frame enlarged, and the text
    bool pixels_attached_ = false;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subs_;
    std::chrono::steady_clock::time_point last_{};
    double t_ = 0, save_t_ = 0;
    std::uint64_t ticks_ = 0;
    bool dirty_ = false;
    std::uint64_t drawn_sig_ = 0;     // what the last frame showed: an unchanged scene draws none
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
    mutable double ov_size_ = 11.5;

    int pixel_ = 2;
    int top_h_ = 24;            // the keeper's band (bottom edge): one line, or two in a narrow window
    int top0_ = 0;              // hosted: room left clear above the band for the floating capsule and Help
    bool compact_ = false;
    double bs_ = 2;
    int pw_ = 0, ph_ = 0, phys_w_ = 0, phys_h_ = 0;
    std::vector<HiText> texts_;

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void publish();
    // game
    void new_table(int level, std::uint64_t seed, bool fresh);
    void settle_now();                // skip the flight: every bird on its perch
    void pose_birds(double dt);
    void click_bird(int bird);
    void speak(int who, const std::string& text);
    void ask(int option);
    void cycle_mark(int bird);
    void fire(int bird);
    void accuse(int bird);
    int weekday() const;
    void persist();
    bool load();
    void load_old_records();
    void play(const std::string& name, float gain = 1, float rate = 1);
    void action(const std::string& id);
    void open(Panel p);
    void layout_buttons();
    void layout_game_buttons();
    void popup_box(int& x, int& y, int& w, int& h, int& text_h, int& options_y) const;
    void panel_box(int& wx, int& wy, int& ww, int& wh) const;
    std::vector<std::string> panel_lines() const;
    void run_script();
    double rand01();
    // drawing
    void compose();
    void draw_top();
    void draw_labels();
    void draw_popup();
    void draw_jaccuse();
    void draw_shot();
    void jaccuse_box(int& x, int& y, int& w, int& h) const;
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
