#pragma once
#include "gui_forms/timer.hpp"
#include "suite.hpp"
#include "suite_model.hpp"
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
    void on_focus_changed(bool focused) override;
    std::function<void(Entry)> focused;

  private:
    Entry entry_;
    TextSprites& sprites_;
    double lift_ = 0;
};

// One of the shelf's scroll arrows: a plump candy arrow in the game bar's smoky blue,
// with a pale bevelled rim, lit from above (the October 1 shelf proposal).
class ShelfArrow final : public gf::Button {
  public:
    ShelfArrow(gf::StableId id, bool up);
    void on_paint(gf::Painter& painter, gf::Rect damage) override;

  private:
    bool up_;
};

// The scrolling part of the shelf owns box layout and the planks beneath them. It
// clips its boxes while the surrounding sign stays fixed. It has no scroll bar: the
// wheel scrolls it, the keyboard reveals the chosen box, and two candy arrows hover
// one above the other at its right side, over the wall beside the boxes; each press
// glides one shelf, and an arrow dims when there is no more shelf its way.
class ShelfRows final : public gf::Control {
  public:
    explicit ShelfRows(gf::StableId id);
    void add_box(const std::shared_ptr<ShelfBox>& box);
    // Adds the two arrows, after the boxes so they float above them.
    void add_arrows();
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void on_pointer(gf::PointerEvent& event) override;
    void on_pointer_bubble(gf::PointerEvent& event) override;
    void reveal(Entry entry);
    // Advances a glide started by an arrow; returns true while still moving.
    bool step(double dt, bool reduced);
    [[nodiscard]] int columns() const { return columns_; }
    [[nodiscard]] int page_rows() const;
    [[nodiscard]] bool scrollable() const { return limit() > 0; }
    [[nodiscard]] double scroll_offset() const { return offset_; }
    // Asks the shelf for animation frames while a glide runs.
    std::function<void()> wake;

  private:
    std::vector<std::shared_ptr<ShelfBox>> boxes_;
    std::vector<gf::Rect> slots_;
    std::vector<double> shelf_lines_;
    std::shared_ptr<ShelfArrow> up_, down_;
    int columns_ = 1;
    double pitch_ = 1, content_ = 0, offset_ = 0, target_ = 0;
    [[nodiscard]] double limit() const;
    void scroll_shelves(int shelves);
    void jump_to(double offset);
    void arrange_boxes();
    void wheel(gf::PointerEvent& event);
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
    Entry entry = entries.front();
    double progress = 0;
};

// The PlaySuite main menu: a lit wall of shelves holding every game as a box,
// a sign with the master switches and the Settings button (the switches observe the
// SettingsStore), and a ticket describing the box under the pointer.
// One click on a box opens its game.
class ShelfView final : public gf::Control {
  public:
    ShelfView(gf::StableId id, TextSprites& sprites);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void on_key_bubble(gf::KeyEvent& e) override;
    void on_pointer_preview(gf::PointerEvent& e) override;
    void select(Entry entry);
    [[nodiscard]] Entry selection() const {
        return selection_;
    }
    // Binds the sign's switches to the masters; the model must outlive this use of it.
    void bind_masters(SuiteModel& model);
    void set_reduced(bool reduced);
    void set_progress(Entry entry, bool started);
    void focus_selection();
    std::function<void(Entry)> open;

  private:
    TextSprites& sprites_;
    std::array<std::shared_ptr<ShelfBox>, entry_count> boxes_{};
    std::shared_ptr<ShelfRows> rows_;
    std::shared_ptr<LaunchCurtain> curtain_;
    std::shared_ptr<gf::Label> credits_;
    bool launching_ = false;
    double launch_t_ = 0;
    void launch(Entry entry);
    std::array<std::shared_ptr<SuiteButton>, 4> switches_{};
    std::unique_ptr<gf::Timer> timer_;
    std::chrono::steady_clock::time_point last_{};
    Entry selection_ = entries.front();
    Entry shown_ = entries.front(); // the box the ticket describes: hovered, else chosen
    std::array<bool, entry_count> started_{};
    bool reduced_ = false;
    gf::Rect header_{}, ticket_{};
    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void request_animation();
    void focused_box(Entry entry);
};
} // namespace games
