#pragma once
#include "atomprobe_view.hpp"
#include "capsule.hpp"
#include "dice_view.hpp"
#include "eggy_view.hpp"
#include "fourpegs_view.hpp"
#include "gui_forms/timer.hpp"
#include "koi_view.hpp"
#include "presentation.hpp"
#include "puzzle_view.hpp"
#include "sheep_view.hpp"
#include "shelf.hpp"
#include "sudoku_view.hpp"
#include "switchbox_view.hpp"
#include "table.hpp"
#include "table_view.hpp"
#include "zen_view.hpp"

#include "text_sprites.hpp"
namespace games {

// The PlaySuite shell: the shelf of boxed games, every game view, and the command
// capsule that floats over whichever game is open.
class Collection final : public gf::Control {
  public:
    explicit Collection(gf::StableId id);
    ~Collection() override;
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void on_pointer_preview(gf::PointerEvent& event) override;
    void on_key_preview(gf::KeyEvent& event) override;
    void activate();
    void open_entry(Entry entry);
    void show_shelf();
    [[nodiscard]] bool shelf_open() const {
        return shelf_open_;
    }
    [[nodiscard]] Entry active() const {
        return active_;
    }
    // Space reserved above games that present their own live surfaces.
    static constexpr double rail_height = 50;

  private:
    TextSprites sprites_;
    std::shared_ptr<ShelfView> shelf_;
    std::shared_ptr<CommandCapsule> capsule_;
    std::shared_ptr<Table> cards_;
    std::shared_ptr<SudokuView> sudoku_;
    std::array<std::shared_ptr<PuzzleView>, 8> puzzles_{};
    std::shared_ptr<eggy::EggyView> eggy_;
    std::shared_ptr<sbx::SwitchboxView> switchbox_{};
    std::shared_ptr<fp::FourPegsView> fourpegs_{};
    std::shared_ptr<ap::AtomProbeView> atomprobe_{};
    std::shared_ptr<kk::KoiView> koikoi_;
    std::shared_ptr<pt::TableView> parrots_;
    std::shared_ptr<ld::DiceView> liarsdice_;
    std::shared_ptr<sh::SheepView> penthesheep_;
    std::shared_ptr<zc::ZenView> rockstack_;
    std::vector<gf::SubscriptionToken> subscriptions_;
    std::unique_ptr<gf::Timer> timer_{};
    std::chrono::steady_clock::time_point last_tick_{};
    double refresh_ = 0;
    Entry active_ = Entry::solitaire;
    bool shelf_open_ = true, reduced_ = false;
    std::uint32_t opened_ = 0; // entries that have been played, for Play / Continue
    gf::Point pointer_{-1000, -1000};
    double capsule_width_limit_ = 0;
    double current_rail_height_ = rail_height;
    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void wake();
    void ensure_view(Entry entry);
    void visibility();
    void preferences();
    void persist() const;
    void refresh_commands();
    void run_command(const std::string& id);
    void toggle(int which);
    [[nodiscard]] bool uses_rail(Entry entry) const;
    [[nodiscard]] std::shared_ptr<gf::Control> view(Entry entry) const;
    [[nodiscard]] CommandSource* source(Entry entry) const;
};
} // namespace games
