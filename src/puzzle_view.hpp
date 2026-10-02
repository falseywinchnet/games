#pragma once
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/timer.hpp"
#include "puzzle_render.hpp"
#include "suite.hpp"
#include <chrono>
#include <memory>
#include <optional>
namespace games {
namespace gf = gui_forms;
class PuzzleView final : public gf::Control, public CommandSource {
  public:
    PuzzleView(gf::StableId id, PuzzleKind kind);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& p, gf::Rect damage) override;
    void on_pointer(gf::PointerEvent& e) override;
    void on_key(gf::KeyEvent& e) override;
    void on_key_bubble(gf::KeyEvent& e) override;
    void on_text_input(gf::TextInputEvent& e) override;
    void activate();
    [[nodiscard]] std::vector<GameCommand> commands() const override;
    void run_command(std::string_view id) override;
    [[nodiscard]] gf::Rect board() const {
        return board_;
    }
    // Where a cube cell is drawn (the centroid of its pixels), or nothing if hidden.
    [[nodiscard]] std::optional<gf::Point> cube_cell_point(int cell) const;
    PuzzleGame game;

  private:
    std::array<std::shared_ptr<gf::Button>, 9> buttons_;
    std::shared_ptr<gf::TextBox> name_;
    std::vector<gf::SubscriptionToken> subscriptions_;
    std::unique_ptr<gf::Timer> timer_;
    gf::ImageId image_{}, nature_{}, curator_{};
    gf::Size image_size_{};
    PuzzleRaster raster_;
    gf::Rect board_{}, popup_{};
    int panel_ = 0, hover_ = -1, drag_ = -1, selection_ = 0, rotation_ = 0, palette_ = 1,
        swap_a_ = -1, swap_b_ = -1, cascade_step_ = -1;
    bool flip_ = false, sound_ = true, music_ = true, reduced_ = false, invalid_swap_ = false,
         tracing_ = false, orbit_ = false;
    double yaw_ = .75, pitch_ = -.56, target_yaw_ = .75, target_pitch_ = -.56;
    gf::Point last_pointer_{};
    Point2 drag_position_{};
    // Puzzle Solve: the dragged piece follows the pointer at its grab point, in board units.
    gf::Point solve_pointer_{};
    Point2 grab_{};
    bool lifted_ = false, lift_flip_ = false;
    int lift_x_ = 0, lift_y_ = 0, lift_rotation_ = 0;
    std::array<int, 7> piece_rotation_{};
    std::array<bool, 7> piece_flip_{};
    void solve_recenter();
    bool solve_snap(int& x, int& y) const;
    bool solve_lift(int piece);
    void solve_select(int piece);
    void layout_solve(gf::Rect bounds);
    [[nodiscard]] double untangle_radius() const;
    struct GemEffect {
        enum Kind { shard, ring, shock, beam_row, beam_column, bolt } kind;
        double x, y, vx, vy, spin, life, size;
        gf::Color color;
        double age = 0;
    };
    std::vector<GemEffect> effects_;
    std::chrono::steady_clock::time_point fx_clock_{};
    std::uint32_t fx_seed_ = 0x9E3779B9u;
    double shake_ = 0, callout_life_ = 0;
    std::string callout_;
    void spawn_effects(const std::array<int, 96>& before, const std::array<int, 96>& after,
                       int depth);
    void step_effects();
    gf::Rect target_{}, tray_{};
    gf::Point press_point_{};
    bool tray_turn_ = false;
    gf::FrameTime animation_start_{}, clock_start_{};
    double animation_duration_ = 0;
    std::array<int, 4> guess_{};
    std::array<int, 96> before_swap_{};
    std::vector<gf::Rect> cells_;
    std::array<gf::Rect, 21> clues_{};
    std::filesystem::path path() const;
    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window&) noexcept override;
    void action(gf::ButtonBase& button);
    void tick();
    void render();
    void fit_raster();
    bool coarse_ = false;
    void persist();
    void changed(const std::string& sound);
    void panel(int kind);
    void new_game();
    void text(gf::Painter&, double, double, const std::string&, double, gf::Color);
    void paint_gems(gf::Painter&);
    void paint_untangle(gf::Painter&);
    void paint_pegs(gf::Painter&);
    void paint_atoms(gf::Painter&);
    void paint_solve(gf::Painter&);
    void paint_sticks(gf::Painter&);
    void paint_help(gf::Painter&);
    int hit(gf::Point) const;
    double elapsed() const;
};
} // namespace games
