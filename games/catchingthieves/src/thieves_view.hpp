#pragma once
// The game as a GUI.Forms control: frame loop, input (arrows or WASD, or
// click where the bear should go), undo, restart and hints, the difficulty
// and dealing gardens at it (hand-made lessons, verified tables and fresh
// gardens grown on a worker), speech bubbles, dialogs and autosave. The frame
// is a small pixel-art image the compositor magnifies; text is drawn crisply on
// top as cached layers.
#include "field.hpp"
#include "pixel_surface.hpp"
#include "garden.hpp"
#include "level.hpp"
#include "levelset.hpp"
#include "save.hpp"
#include "show.hpp"
#include "solver.hpp"
#include "suite.hpp"
#include "tiers.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ct {
namespace gf = gui_forms;

struct Options {
    bool hosted = true;
    bool dev = false;  // separate save file; CT_SCRIPT allowed
};

class ThievesView final : public gf::Control, public games::CommandSource {
public:
    ThievesView(gf::StableId id, Options opt);
    ~ThievesView() override;
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree() {}
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& p, gf::Rect damage) override;
    void on_pointer(gf::PointerEvent& e) override;
    void on_key(gf::KeyEvent& e) override;
    void on_key_bubble(gf::KeyEvent& e) override { on_key(e); }
    void activate();
    // Cabinet hosting: the collection's master switches gate this game's own
    // music and sound; `foreground` is false while another game shows.
    void set_cabinet(bool foreground, bool music, bool sound, bool reduced = false);
    std::vector<games::GameCommand> commands() const override;
    void run_command(std::string_view id) override;
    // Only dev views accept scripted gameplay. No filesystem or arbitrary command access.
    bool scripted_action(std::string_view code);
    int table_size() const { return static_cast<int>(table_.size()); }
    int current_level() const { return save_.level; }  // the garden's table id, or -1 for a fresh one
    int difficulty() const { return save_.difficulty; }
    int garden_tier() const { return save_.garden_tier; }
    // The difficulty, declared to the shell's Settings screen.
    std::vector<games::GameSetting> settings() const override;
    void change_setting(std::string_view id, double value) override;
    std::string move_history() const { return board_.history(); }
    bool solved() const { return won_; }
    bool reduced_motion() const { return cab_reduced_; }
    std::uint64_t published_frames() const { return published_frames_; }
    std::uint64_t timer_callbacks() const { return timer_callbacks_; }
    bool controls_fit() const;

private:
    enum class Panel { none, help, menu };
    struct Button { std::string id, label; int x, y, w, h; int style = 0; bool enabled = true; };
    struct HiText { std::string s; int font; double size; int wrap; int x, y; Col c; };

    Options opt_;
    SaveData save_;
    std::vector<LevelEntry> table_;   // the verified gardens: lessons, then Easy, Medium and Hard
    std::map<int, size_t> by_id_;
    LevelEntry current_;        // the garden in play (from the table, or freshly grown)
    Board board_;
    Garden garden_;
    Show show_;
    Canvas frame_;
    render::PixelSurface pixels_;     // the window end: the small frame enlarged, and the text
    bool pixels_attached_ = false;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subs_;
    std::chrono::steady_clock::time_point last_{};
    double t_ = 0;
    bool dirty_ = false;

    std::deque<int> queue_;     // moves waiting for the bear
    bool won_ = false;
    double won_t_ = 0;
    bool stuck_ = false;
    std::string message_;
    double message_t_ = 99;

    // hints and fresh gardens come from worker threads; the next garden grows while this one is played
    struct Worker {
        std::thread th;
        std::atomic<bool> done{false};
        CancellationSource cancellation;
        std::mutex m;
        SolveResult solve;
        LevelEntry level;
        int tier = 0;
    };
    std::unique_ptr<Worker> hint_, gen_;
    // the field round the garden grows on its own worker when the season changes
    struct FieldWorker {
        std::thread th;
        std::atomic<bool> done{false};
        std::atomic<bool> cancel{false};
        Season season = Season::spring;
        std::shared_ptr<const FieldArt> art;
    };
    std::unique_ptr<FieldWorker> field_;
    bool field_shown_ = false;
    Season field_season_ = Season::spring;
    void grow_field(Season season);
    void join_field();
    static void field_worker(FieldWorker* worker);
    std::string hint_for_;      // the board history the hint was asked for
    void start_hint();
    void start_gen(int tier);
    void poll_workers();
    void join(std::unique_ptr<Worker>& w);
    static void generate_worker(Worker* worker, int tier, std::uint64_t seed);
    static void hint_worker(Worker* worker, Board board);
    void request_frame();
    void commit_queued_moves();
    std::string hit_button() const;
    bool same_position(const Board& board) const;

    double mouse_x_ = 0, mouse_y_ = 0;
    bool mouse_in_ = false;
    int hover_cell_ = -1;
    Panel panel_ = Panel::none;
    std::vector<Button> buttons_;
    std::string pressed_, hover_;
    bool cab_front_ = true, cab_music_ = true, cab_sound_ = true, cab_reduced_ = false;
    bool render_dirty_ = true;
    std::uint64_t rng_ = 1;
    std::uint64_t published_frames_ = 0, timer_callbacks_ = 0;
    std::vector<std::pair<double, std::string>> script_;  // dev: CT_SCRIPT="t:code,..."

    int pixel_ = 2;
    double bs_ = 2;
    int pw_ = 0, ph_ = 0, phys_w_ = 0, phys_h_ = 0, hud_w_ = 128;

    std::vector<HiText> texts_;

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void publish();
    // game
    void load_tables();
    void deal();                                   // a new garden at the chosen difficulty
    bool enter(int id, const std::string& history = {});
    void enter_fresh(const LevelEntry& e, const std::string& history = {});
    void set_difficulty(int difficulty);
    void choose_difficulty(int difficulty);
    int pick_season();
    int random(int n);
    void begin_level();
    void step(int dir);
    void walk_to(int cell);
    void undo();
    void restart();
    void next();
    void after_move();
    void finish();
    int medal(int pushes) const;  // for this garden: 0 none, 1 bronze, 2 silver, 3 gold
    std::vector<int> path_to(int cell) const;
    void say(const std::string& s, Col c = {1, 1, 1, 1});
    void persist();
    void play(const std::string& name, float gain = 1, float rate = 1);
    void action(const std::string& id);
    void open(Panel p);
    void layout_buttons();
    void run_script();
    // drawing
    void compose();
    void draw_hud();
    void draw_bubbles();
    void draw_win_card();
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

}  // namespace ct
