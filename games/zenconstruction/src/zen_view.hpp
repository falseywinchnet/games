#pragma once
// The Zen Construction window: the worksite in software 3D, the crane under
// the player's hands, the duck in the cab, the sites you've built, and saving.
#include "run.hpp"
#include "suite.hpp"
#include "site.hpp"
#include "platform/present.hpp"
#include "platform/render.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/timer.hpp"

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace zc {

namespace gf = gui_forms;

struct Options {
    bool hosted = false;
    bool dev = false;   // a separate save; ZC_SCRIPT allowed
};

class ZenView final : public gf::Control, public games::CommandSource {
public:
    ZenView(gf::StableId id, Options options);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree() {}
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void on_pointer(gf::PointerEvent& event) override;
    void on_key(gf::KeyEvent& event) override;
    void on_key_bubble(gf::KeyEvent& event) override { on_key(event); }
    void on_text_input(gf::TextInputEvent& event) override;
    void activate();
    // in the cabinet: whether this game is in front, and the master switches
    void set_cabinet(bool foreground, bool music, bool sound, bool reduced = false);
    std::vector<games::GameCommand> commands() const override;
    void run_command(std::string_view id) override;
    void on_focus_changed(bool focused) override;

private:
    enum class Panel { none, new_site, sites, help };
    struct Button {
        std::string id, label;
        double x = 0, y = 0, w = 0, h = 0;
        bool enabled = true;
        int style = 0;      // 0 plain, 1 primary, 2 hold-to-confirm
    };
    struct SavedSite {
        std::string text;   // the run's own save
        std::string company;
        std::uint32_t seed = 0;
        double best = 0;
    };
    struct Say {
        std::string text;
        double age = 0, life = 3.2;
    };

    Options options_;
    // the sites
    std::vector<SavedSite> sites_;
    int current_ = -1;
    std::unique_ptr<Run> run_;
    std::future<std::unique_ptr<Run>> pending_;
    bool pending_new_ = false;          // the pending site is a new one (else a start-over)
    std::string last_company_ = "Pebble & Sons";
    bool music_ = true, sound_ = true;
    bool cab_front_ = true, cab_music_ = true, cab_sound_ = true, cab_reduced_ = false;
    // the scene
    Site site_;
    OrbitCamera camera_;
    phys::Vec3 camera_goal_;
    bool camera_dirty_ = true;
    double scene_pixel_ = 1.5;          // points per scene pixel
    double since_render_ = 1;
    bool want_render_ = true;
    bool caret_on_ = false;     // the name field's caret, as last drawn
    // input
    bool keys_[256] = {};
    double mx_ = 0, my_ = 0;
    bool dragging_ = false, drag_moved_ = false;
    double drag_x_ = 0, drag_y_ = 0;
    int hover_rock_ = -1;
    bool hover_ok_ = false;
    std::string pressed_, hover_button_;
    double hold_reset_ = 0;             // seconds Start over has been held
    // the operator
    OperatorMood mood_ = OperatorMood::idle;
    double mood_time_ = 0;
    Say say_;
    double say_cooldown_ = 0;
    std::uint32_t lines_said_ = 0;
    bool told_controls_ = false, told_slack_ = false;
    // panels
    Panel panel_ = Panel::none;
    std::string name_entry_;
    std::uint32_t seed_entry_ = 1;
    std::vector<Button> buttons_;
    // timing
    double t_ = 0, step_accumulator_ = 0, save_t_ = 0;
    bool dirty_save_ = false;
    std::chrono::steady_clock::time_point last_{};
    std::vector<std::pair<double, std::string>> script_;
    // sound: the motor's pitch and level from the crane's motion; sounds
    // waiting their moment; birds now and then
    struct Later {
        double at = 0;
        std::string name;
        float gain = 1;
    };
    std::vector<Later> later_;
    phys::Vec3 last_hook_;
    double last_wires_ = 0;
    double motor_level_ = 0;
    double bird_timer_ = 20;
    double splash_timer_ = 1, pebble_timer_ = 4, frog_timer_ = 12;
    std::uint32_t sound_seed_ = 12345u;
    int knock_variant_ = 0;
    double last_knock_ = -1;
    // presentation
    double W_ = 1100, H_ = 760, bs_ = 2;
    int phys_w_ = 0, phys_h_ = 0;
    Canvas frame_;
    std::shared_ptr<gf::LiveSurface> surface_;
    std::unique_ptr<gf::Timer> timer_;
    std::vector<gf::SubscriptionToken> subs_;
    bool direct_ = false;
    std::vector<int> xmap_, ymap_;

    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void publish();
    // sites
    void start_new_site(const std::string& company, std::uint32_t seed);
    void start_over();
    void adopt_pending();
    void switch_site(int index);
    void store_current();
    void persist();
    bool load();
    // play
    void step_play(double dt);
    CraneInput gather_input() const;
    void react(const RunEvents& events);
    void say(const std::string& text);
    void say_one(const char* const* lines, int count);
    void set_mood(OperatorMood mood, double seconds);
    void update_camera(double dt);
    void update_hover();
    void action(const std::string& id);
    void run_script();
    void play(const std::string& name, float gain = 1.f, float rate = 1.f, float pan = 0.f);
    void step_sound(double dt);
    // drawing
    void layout_buttons();
    void compose();
    void present_scene();
    // Hosted, the capsule floats over the top-left corner and Help over the top-right: the
    // site's card and the top-right buttons start below them.
    [[nodiscard]] double top_inset() const { return options_.hosted ? 58 : 10; }
    void draw_hud();
    void draw_height_marks();
    void draw_panel();
    void draw_loading();
    void draw_buttons();
    void paint_sign();
    void text(const std::string& s, double x, double y, Col c, double size, int font = 0, double wrap = 0, int align = 0);
    double text_w(const std::string& s, double size, int font) const;
    double text_h(const std::string& s, double size, int font, double wrap) const;
    void rrect(double x, double y, double w, double h, double r, Col fill, Col line = Col{0, 0, 0, 0}, double width = 0);
    bool compact() const { return W_ < 760 || H_ < 520; }
};

}  // namespace zc
