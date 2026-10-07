#pragma once
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/timer.hpp"
#include "sudoku.hpp"
#include "suite.hpp"
#include <future>
namespace games {
namespace gf = gui_forms;
class SudokuView final : public gf::Control, public CommandSource {
  public:
    explicit SudokuView(gf::StableId id);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void on_pointer(gf::PointerEvent& event) override;
    void on_key(gf::KeyEvent& event) override;
    void on_key_bubble(gf::KeyEvent& event) override;
    void on_text_input(gf::TextInputEvent& event) override;
    void activate();
    [[nodiscard]] std::vector<GameCommand> commands() const override;
    void run_command(std::string_view id) override;
    // The next puzzle's level and the Day or Night board, on the Settings screen.
    [[nodiscard]] std::vector<GameSetting> settings() const override;
    void change_setting(std::string_view id, double value) override;
    Sudoku game;

  private:
    std::array<std::shared_ptr<gf::Button>, 20> buttons_;
    std::shared_ptr<gf::TextBox> name_;
    std::vector<gf::SubscriptionToken> subscriptions_;
    std::unique_ptr<gf::Timer> timer_;
    std::future<Sudoku> generation_;
    gf::Rect board_{}, popup_{};
    double cell_ = 50;
    int selected_ = 0, hover_ = -1, digit_ = 1, panel_ = 0, difficulty_ = 0;
    bool notes_ = false, busy_ = false;
    bool sound_ = true, music_ = true, reduced_ = false;
    std::vector<int> celebrated_;
    gf::FrameTime celebration_start_{};
    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void action(gf::ButtonBase& button);
    void new_game();
    void tick();
    void edit(int digit, bool note);
    void panel(int kind);
    void persist();
    void refresh_pad();
    void text(gf::Painter& p, double x, double y, const std::string& value, double size,
              gf::Color color);
};
} // namespace games
