#pragma once
// The game as a GUI.Forms control: the first-person maze in a small low-res
// frame the compositor magnifies, wrapped in Windows-95 chrome gone vaporwave
// (pink-to-teal title bars): the briefing that names what you seek, the speech
// box with its speaker's portrait, the status bar, the win dialog, the trophy
// shelf and help. Keys, mouse, autosave, music and sound.
#include "maze.hpp"
#include "save.hpp"
#include "session.hpp"
#include "soft3d.hpp"
#include "suite.hpp"
#include "platform/canvas.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"

#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace mz {
namespace gf = gui_forms;

struct Options {
    bool hosted = true;
    bool dev = false;  // separate save file; MZ_SCRIPT allowed
};

class MazeView final : public gf::Control, public games::CommandSource {
public:
    MazeView(gf::StableId id, Options opt);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree() {}
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& p, gf::Rect damage) override;
    void on_pointer(gf::PointerEvent& e) override;
    void on_key(gf::KeyEvent& e) override;
    void on_key_bubble(gf::KeyEvent& e) override { on_key(e); }
    void activate();
    void set_cabinet(bool foreground, bool music, bool sound, bool reduced = false);
    std::vector<games::GameCommand> commands() const override;
    void run_command(std::string_view id) override;
    bool scripted_action(std::string_view code);
    std::uint64_t published_frames() const { return published_frames_; }
    std::uint64_t timer_callbacks() const { return timer_callbacks_; }
    int current_level() const { return level_; }
    int walked_steps() const { return sess_.play.steps; }
    bool controls_fit() const;


private:
    enum class Panel { none, briefing, won, shelf, help, credits };
    struct Button { std::string id, label; int x, y, w, h; };
    struct HiText { std::string s; int font; double size; int wrap; int x, y; Col c; };

    Options opt_;
    SaveData save_;
    Session sess_;
    Soft3D r_;
    Canvas frame_;
    std::shared_ptr<gf::LiveSurface> surface_;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subs_;
    std::chrono::steady_clock::time_point last_{};
    double t_ = 0, save_t_ = 0;
    bool dirty_ = false, awarded_ = false;

    int level_ = 1, reward_ = 0;
    std::vector<std::string> news_;   // what's new in this maze (for the briefing)
    int autowalk_ = 0;                // dev: steps still to walk along a best route (re-planned each step)
    Panel panel_ = Panel::none;
    int credits_page_ = 0, trophy_page_ = 0;
    std::vector<Button> buttons_;
    std::string pressed_, hover_;
    double mouse_x_ = 0, mouse_y_ = 0;
    bool cab_front_ = true, cab_music_ = true, cab_sound_ = true;
    std::vector<std::pair<double, std::string>> script_;

    double pixel_ = 3;
    double bs_ = 2;
    int pw_ = 0, ph_ = 0, phys_w_ = 0, phys_h_ = 0;
    std::vector<int> xmap_;
    bool direct_ = false, cab_reduced_ = false, render_dirty_ = true;
    std::uint64_t published_frames_ = 0, timer_callbacks_ = 0;
    std::vector<HiText> texts_;

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void request_frame();
    void on_visible_changed(bool visible);
    void publish();
    void start_level(int n);
    void finish_level();
    void persist();
    void play(const std::string& name, float gain = 1, float rate = 1);
    void action(const std::string& id);
    void open(Panel p);
    void layout_buttons();
    void run_script();
    // drawing
    void compose();
    void draw_status();
    void draw_speech();
    void draw_panel();
    void window(int x, int y, int w, int h, const std::string& title);
    void bevel(int x, int y, int w, int h, bool sunken);
    void blit(const Tex32& t, int x, int y, int w, int h, float dim = 1);
    void draw_button(const Button& b);
    const Mask& tmask(const std::string& s, int font, double size, int wrap_game) const;
    int text(const std::string& s, int x, int y, Col c, double size = 9, int font = 0, int wrap = 0);
    int text_w(const std::string& s, double size, int font) const;
    int text_h(const std::string& s, double size, int font, int wrap = 0) const;
    void blit_texts(std::uint32_t* dst, size_t stride_px, double k);
};

}  // namespace mz
