#pragma once
// The game as a GUI.Forms control: frame loop, input, HUD, speech, dialogs,
// autosave, sound. The frame is a small pixel-art image the compositor
// magnifies; text is drawn crisply on top as cached layers.
#include "actor.hpp"
#include "mole.hpp"
#include "platform/lines.hpp"
#include "gui_forms/host/cursor_interaction/cursor_interaction.hpp"
#include "gui_forms/window.hpp"
#include "save.hpp"
#include "stage.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace sbx {
namespace gf = gui_forms;

struct Options {
    bool dev = false;  // separate save file
};

class SwitchboxView final : public gf::Control {
public:
    SwitchboxView(gf::StableId id, Options opt);
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
    // music and sound settings; `foreground` is false while another game shows.
    void set_cabinet(bool foreground, bool music, bool sound, bool reduced_motion);

private:
    enum class Panel { none, help, scores, name };
    struct Button { std::string id, label; int x, y, w, h; };
    struct Bubble { std::string text; double age = 0, dur = 0; int shown = 0; bool muffled = false, special = false; };
    struct HiText { std::string s; bool bold; double size; int wrap; int x, y; Col c; };

    Options opt_;
    SaveData save_;
    Puzzle puzzle_;
    Actor actor_;
    Stage stage_;
    StageState st_;
    LineBank lines_;
    Canvas frame_;
    std::shared_ptr<gf::LiveSurface> surface_;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subs_;
    std::chrono::steady_clock::time_point last_{};
    double t_ = 0, save_t_ = 0;
    Bubble bubble_;
    Panel panel_ = Panel::none;
    std::vector<Button> buttons_;
    std::string pressed_, hover_;
    double mouse_x_ = 0, mouse_y_ = 0;  // game pixels
    bool mouse_in_ = false;
    int pixel_ = 2;            // points per game pixel
    double bs_ = 2;            // backing scale
    int pw_ = 0, ph_ = 0;      // frame size in game pixels
    int phys_w_ = 0, phys_h_ = 0;
    std::vector<int> xmap_;
    std::array<double, kSwitches> lamp_delay_{};  // ripple timing when lamps go out
    int last_steps_ = 0;       // steps of the combination just cracked
    int pending_score_ = 0;    // a cracked combination waiting for the celebration to end
    std::string name_entry_;
    bool dirty_ = false;
    // mischief
    int hold_sw_ = -1;          // the switch the mouse button is holding down
    bool hold_blocked_ = false; // she forced it down: ignore the hold until the button comes up
    bool mouse_down_ = false;
    int same_sw_ = -1, same_count_ = 0;  // repeated flips of one switch: she steals it
    Mole mole_;
    double mole_pending_ = -1;  // she is hiding before the whack-a-mole begins
    double mole_after_ = 0;     // switches settling back into place afterwards
    double result_t_ = 0;       // the whack-a-mole score stays on screen a moment
    int result_hits_ = 0;
    bool cursor_hidden_ = false;
    gf::CursorHiddenLease cursor_lease_{};
    bool cab_front_ = true, cab_music_ = true, cab_sound_ = true;
    bool cab_reduced_ = false;
    std::vector<std::pair<double, int>> script_;  // dev: SBX_SCRIPT="t:switch,..." flips (-1 head, 7 next correct, 8 whack, 9 a wrong one, 10 the stolen hole)

    bool direct_ = false;
    std::vector<HiText> texts_;

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void on_focus_changed(bool focused) override;
    void cursor_active_changed(bool active);
    void cursor_capture_changed(const gf::PointerCaptureChange& change);
    void cancel_cursor_interaction();
    void cursor_refused(gf::CursorStatus status);
    void tick();
    void publish();
    void flip(int sw);
    void persist();
    void play(const std::string& name, float gain = 1, float rate = 1);
    void say(const std::string& category);
    void say_text(const std::string& text, bool special = false);
    void mischief(double dt);
    void mole_tick(double dt);
    void draw_mole_hud();
    void react(Reaction r);
    void action(const std::string& id);
    void open(Panel p);
    void layout_buttons();
    // drawing
    void compose();
    void draw_hud();
    void draw_bubble();
    void draw_panel();
    void draw_button(const Button& b);
    const Mask& tmask(const std::string& s, bool bold, double size, int wrap_game) const;
    int text(const std::string& s, int x, int y, Col c, double size = 9, bool bold = false, int wrap = 0);
    int text_w(const std::string& s, double size, bool bold) const;
    int text_h(const std::string& s, double size, bool bold, int wrap = 0) const;
    void blit_texts(std::uint32_t* dst, size_t stride_px, double k);
};

}  // namespace sbx
