#pragma once
// The game as a GUI.Forms control: frame loop, input, HUD, speech, dialogs,
// autosave, music. Rendering is published through a LiveSurface.
#include "lines.hpp"
#include "present.hpp"
#include "save.hpp"
#include "scene.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eggy {
namespace gf = gui_forms;

struct Options {
    bool dev = false;          // separate save file, warp allowed
    double warp_v = -1;        // start at this row (dev)
    bool summit = false;       // warp next to the summit (dev)
    bool storm = false;        // bring a storm in a few seconds (dev)
};

class EggyView final : public gf::Control {
public:
    EggyView(gf::StableId id, Options opt);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree() {}
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& p, gf::Rect damage) override;
    void on_pointer(gf::PointerEvent& e) override;
    void on_key(gf::KeyEvent& e) override;
    void on_key_bubble(gf::KeyEvent& e) override { on_key(e); }
    void on_text_input(gf::TextInputEvent& e) override;
    void activate();

private:
    enum class Panel { none, title, help, scores, confirm, away, finale };
    struct Button { std::string id, label; int x, y, w, h; };
    struct Bubble { std::string text; double age = 0, dur = 0; bool officer = false; int shown = 0; };

    Options opt_;
    std::unique_ptr<Sim> sim_;
    Scene scene_;
    SaveData save_;
    LineBank lines_;
    Canvas frame_;
    std::shared_ptr<gf::LiveSurface> surface_;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subs_;
    std::chrono::steady_clock::time_point last_{};
    double t_ = 0, save_t_ = 0, chatter_t_ = 20, speech_cool_ = 0, banner_t_ = 0, music_hold_ = 0;
    std::string banner_, music_track_;
    Biome music_biome_ = Biome::meadow;
    Bubble bubble_;
    std::vector<Bubble> queue_;
    Panel panel_ = Panel::title;
    std::vector<Button> buttons_;
    std::string pressed_;
    double away_m_ = 0, away_s_ = 0;
    bool keys_[5] = {};        // up down left right hop-held
    bool hop_edge_ = false;
    bool mouse_down_ = false;
    double mouse_x_ = 0, mouse_y_ = 0;  // physical pixels
    double bs_ = 2;            // backing scale
    int pw_ = 0, ph_ = 0;      // framebuffer size in game pixels
    int phys_w_ = 0, phys_h_ = 0;
    std::vector<int> xmap_;
    std::string name_entry_;
    bool score_saved_ = false;
    double title_card_ = 0;    // the "ELITE SPECIAL SOLDIER" banner timer

    bool direct_ = false;
    NativePresenter presenter_;
    std::vector<TextSprite> sprites_;
    void register_surface();
    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void handle(const Event& e);
    void say(const std::string& category, double chance = 1.0);
    void say_text(const std::string& text, bool officer = false, double dur = 0);
    std::string context_category();
    std::string surroundings();
    std::string self_category();
    std::map<std::string, double> noticed_;
    double notice_t_ = 6;
    void persist();
    void intro_tick(double dt);
    void skip_intro();
    int intro_ = -1;
    double intro_t_ = 0;
    bool intro_pending_ = false;
    void new_climb();
    void action(const std::string& id);
    void open(Panel p);
    void layout_buttons();
    // drawing
    void compose();
    void draw_hud();
    void draw_bubble();
    void draw_panel();
    void draw_window(int x, int y, int w, int h, const std::string& title);
    void draw_button(const Button& b);
    struct HiText { std::string s; bool bold; double size; int wrap; int x, y; Col c; int big; };
    std::vector<HiText> texts_;
    const Mask& tmask(const std::string& s, bool bold, double size, int wrap_game) const;
    int text(const std::string& s, int x, int y, Col c, double size = 10, bool bold = false, int wrap = 0, int big = 1);
    int text_w(const std::string& s, double size, bool bold, int big = 1) const;
    int text_h(const std::string& s, double size, bool bold, int wrap = 0, int big = 1) const;
    void blit_texts(std::uint32_t* dst, size_t stride_px, double k);
};

}  // namespace eggy
