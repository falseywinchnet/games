#pragma once
#include "suite.hpp"
// A Mac table for Koi-Koi, for playing and testing ahead of its place on the PlaySuite card
// table: green felt, the house's cream hanafuda, the opponent's hand along the top, the field
// in the middle beside the draw pile, your hand along the bottom, each side's captures sorted
// by kind; a scoreboard of the twelve months; cards that travel (a played card lands on its
// match before both go to the captures; the drawn card turns over on the pile first); and the
// koi-koi decision, rounds and matches, hints, rules and autosave.
#include "koikoi.hpp"
#include "platform/image.hpp"
#include "platform/present.hpp"
#include "paint/carpet.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace kk {
namespace gf = gui_forms;
using namespace games;

struct Options {
    bool hosted = false;
    bool dev = false;  // separate save; KK_SCRIPT allowed
};

class KoiView final : public gf::Control, public games::CommandSource {
public:
    KoiView(gf::StableId id, Options opt);
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
    enum class Panel { none, rules, sets, score };
    struct Sprite { double x = 0, y = 0, s = 1, face = 0; double tx = 0, ty = 0, ts = 1, tface = 0; bool placed = false; int z = 0; };
    struct Hold { int card; double x, y, s; bool face; double until; };
    struct Button { std::string id, label; double x, y, w, h; bool enabled = true; int style = 0; };
    struct Banner { std::string text; double age = 0, life = 1.8; };

    Options opt_;
    koi::KoiState s_;
    int next_opponent_ = 0;
    bool sound_ = true, music_ = true;
    bool cab_reduced_ = false;
    bool cab_front_ = true, cab_music_ = true, cab_sound_ = true;
    std::array<Sprite, 48> sp_{};
    std::array<int, 21> slots_{};      // the field's places, card id or -1
    std::vector<Hold> holds_;
    double wait_ = 0;                   // the computer's thinking, or a pause in the flow
    int hover_card_ = -1;
    int hint_ = -1;
    std::vector<Banner> banners_;
    Panel panel_ = Panel::none;
    std::vector<Button> buttons_;
    std::string pressed_, hover_btn_;
    double mx_ = 0, my_ = 0;
    std::vector<std::pair<double, std::string>> script_;
    bool auto_ = false;
    double speed_ = 1;

    // art
    std::array<Canvas, 48> art_;
    Canvas art_back_;
    std::array<Canvas, 48> big_, small_;
    Canvas big_back_, small_back_, felt_, carpet_;
    Canvas art_shadow_, shadow_big_, shadow_small_, finish_big_, finish_small_;
    int built_w_ = 0;

    // geometry (points)
    double W_ = 1100, H_ = 760, bs_ = 2;
    double cw_ = 80, ch_ = 112, small_k_ = .47;
    double panel_x_ = 0;
    bool wide_ = true;     // room for the scoreboard down the side; otherwise a bar along the top
    double top_ = 0;       // the top bar's height (0 with the side scoreboard)
    mutable double panel_size_ = 12;
    double mid() const { return top_ + (H_ - top_) / 2; }

    Canvas frame_;
    std::shared_ptr<gf::LiveSurface> surface_;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subs_;
    std::chrono::steady_clock::time_point last_{};
    double t_ = 0, save_t_ = 0;
    std::uint64_t ticks_ = 0;
    bool dirty_ = false;
    int phys_w_ = 0, phys_h_ = 0;
    bool direct_ = false;

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void request_frame();
    bool visual_work() const;
    bool render_dirty_ = true;
    void publish();
    // the game
    void new_match();
    void step(double dt);              // the computer's moves and the automatic steps
    void player_play(int hand_index);
    void player_choose(int field_card);
    void player_decide(bool koikoi);
    void after_move(int card, const std::vector<int>& matched, bool from_deck);
    void announce_sets(int player, int before);
    void persist();
    bool load();
    void play(const std::string& name, float gain = 1, float rate = 1);
    void action(const std::string& id);
    void run_script();
    void layout_buttons();
    void layout_game_buttons();
    // the table
    void build_art();
    void place_targets();
    void sync_slots();
    void slot_pos(int slot, double& x, double& y) const;
    void hand_pos(int player, int i, int n, double& x, double& y) const;
    void capture_pos(int player, int card, double& x, double& y) const;
    void deck_pos(double& x, double& y) const;
    int card_at(double x, double y) const;
    // drawing
    void compose();
    void draw_card(int id, const Sprite& s, bool lift, bool glow, bool dim);
    void draw_scoreboard();
    void draw_topbar();
    void draw_months(double x, double y, double w);
    double draw_sets_lines(double x, double y, double w);
    std::vector<std::string> panel_lines() const;
    void draw_captures_labels();
    void draw_dialog();
    void draw_panel();
    void draw_buttons();
    void text(const std::string& s, double x, double y, Col c, double size, int font = 0, double wrap = 0, int align = 0);  // align: 0 left, 1 centre, 2 right
    double text_w(const std::string& s, double size, int font) const;
    double text_h(const std::string& s, double size, int font, double wrap) const;
    void rrect(double x, double y, double w, double h, double r, Col fill, Col line = {0, 0, 0, 0}, double lw = 0);
};

}  // namespace kk
