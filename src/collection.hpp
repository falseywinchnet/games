#pragma once
#include "eggy_view.hpp"
#include "fourpegs_view.hpp"
#include "atomprobe_view.hpp"
#include "switchbox_view.hpp"
#include "presentation.hpp"
#include "puzzle_view.hpp"
#include "sudoku_view.hpp"
#include "table.hpp"
#include "gui_forms/timer.hpp"
namespace games {
class Collection final : public gf::Control {
  public:
    explicit Collection(gf::StableId id);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void activate();

  private:
    std::shared_ptr<Table> cards_;
    std::shared_ptr<eggy::EggyView> eggy_;
    std::shared_ptr<sbx::SwitchboxView> switchbox_{};
    std::shared_ptr<fp::FourPegsView> fourpegs_{};
    std::shared_ptr<ap::AtomProbeView> atomprobe_{};
    std::shared_ptr<GameButton> play_;
    std::array<std::shared_ptr<GameButton>, 4> categories_;
    std::shared_ptr<SudokuView> sudoku_;
    std::array<std::shared_ptr<PuzzleView>, 8> puzzles_;
    std::shared_ptr<LibrarySurface> library_;
    std::array<std::shared_ptr<gf::Button>, 10> tiles_;
    std::array<std::shared_ptr<GameButton>, 4> controls_;
    std::vector<gf::SubscriptionToken> subscriptions_;
    int active_ = 0, selected_ = 0;
    bool choosing_ = false;
    std::unique_ptr<gf::Timer> audio_timer_{};
    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void poll_audio();
    void choose(gf::ButtonBase& button);
    void open_selected(gf::ButtonBase& button);
    void category(gf::ButtonBase& button);
    void start_selected();
    void command(gf::ButtonBase& button);
    void visibility();
    void preferences();
};
} // namespace games
