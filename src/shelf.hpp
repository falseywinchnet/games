#pragma once
#include "gui_forms/timer.hpp"
#include "suite.hpp"
#include "text_sprites.hpp"
#include <functional>
#include <memory>
namespace games {

// One boxed game on the shelf. It lifts toward the viewer when hovered or chosen.
class ShelfBox final : public gf::Button {
  public:
    ShelfBox(gf::StableId id, Entry entry, TextSprites& sprites);
    [[nodiscard]] Entry entry() const {
        return entry_;
    }
    [[nodiscard]] bool hot() const {
        return hovered_visual() || pressed_visual();
    }
    // Returns true while still moving.
    bool step(double dt, bool reduced);
    void on_paint(gf::Painter& painter, gf::Rect damage) override;

  private:
    Entry entry_;
    TextSprites& sprites_;
    double lift_ = 0;
};

// Paints the launch transition above the shelf: the chosen box's art grows to fill
// the window, then the game opens.
class LaunchCurtain final : public gf::Control {
  public:
    explicit LaunchCurtain(gf::StableId id) : Control(std::move(id)) {
        set_hit_test_transparent(true);
    }
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    gf::Rect from{};
    Entry entry = Entry::solitaire;
    double progress = 0;
};

// The PlaySuite main menu: a lit wall of shelves holding every game as a box,
// a sign with the master switches, and a ticket describing the box under the pointer.
// One click on a box opens its game.
class ShelfView final : public gf::Control {
  public:
    ShelfView(gf::StableId id, TextSprites& sprites);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void on_key_bubble(gf::KeyEvent& e) override;
    void select(Entry entry);
    [[nodiscard]] Entry selection() const {
        return selection_;
    }
    void set_preferences(bool music, bool sound, bool reduced);
    void set_progress(Entry entry, bool started);
    void focus_selection();
    std::function<void(Entry)> open;
    std::function<void(int)> toggle; // 0 music, 1 sound, 2 motion

  private:
    TextSprites& sprites_;
    std::array<std::shared_ptr<ShelfBox>, entry_count> boxes_{};
    std::shared_ptr<LaunchCurtain> curtain_;
    bool launching_ = false;
    double launch_t_ = 0;
    void launch(Entry entry);
    std::array<std::shared_ptr<SuiteButton>, 3> switches_{};
    std::vector<gf::SubscriptionToken> subscriptions_;
    std::unique_ptr<gf::Timer> timer_;
    std::chrono::steady_clock::time_point last_{};
    Entry selection_ = Entry::solitaire;
    Entry shown_ = Entry::solitaire; // the box the ticket describes: hovered, else chosen
    std::array<bool, entry_count> started_{};
    bool reduced_ = false;
    int columns_ = 7;
    gf::Rect header_{}, ticket_{};
    std::vector<double> shelf_lines_; // y of each plank top
    double box_width_ = 100;
    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void clicked_box(gf::ButtonBase& button);
    void clicked_switch(gf::ButtonBase& button);
};
} // namespace games
