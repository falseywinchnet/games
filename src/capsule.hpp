#pragma once
#include "suite.hpp"
#include "suite_settings.hpp"
#include "text_sprites.hpp"
#include <functional>
#include <memory>
namespace games {

// The PlaySuite command capsule floats at the top of every game. Folded, it shows the
// way back to the shelf, the game's name and its primary commands. It opens smoothly
// while the pointer rests on it (or when pinned with the "more" button), revealing the
// rest of the game's commands, the master music, sound and motion switches and the
// Settings button. The switches observe the SettingsStore: whatever changes a master,
// every switch shows it at once, slashed in red when off.
class CommandCapsule final : public gf::Control {
  public:
    CommandCapsule(gf::StableId id, TextSprites& sprites);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    [[nodiscard]] bool hit_test_local(gf::Point local) const override;

    void set_game(std::string title, std::vector<GameCommand> commands);
    // Shows the masters; the capsule calls it itself whenever the store changes.
    void set_preferences(bool music, bool sound, bool reduced);
    // Widest the capsule may grow; it wraps its commands onto more rows beyond this.
    void set_maximum_width(double width);
    // The rectangle the capsule wants inside the given area, at the current opening.
    [[nodiscard]] gf::Rect placement(gf::Rect area) const;
    // The drawn pill at the current opening, excluding the shadow margin.
    [[nodiscard]] gf::Size pill_size() const;
    // Advances the opening animation; returns true while moving.
    bool step(double dt, bool pointer_inside, bool reduced);
    [[nodiscard]] bool open() const {
        return open_ > .001;
    }
    [[nodiscard]] bool unsettled(bool inside) const {
        return open_ != (inside || pinned_ ? 1.0 : 0.0);
    }
    void fold();

    std::function<void()> back;
    std::function<void(const std::string&)> command;
    std::function<void(int)> toggle; // 0 music, 1 sound, 2 motion, 3 open Settings

  private:
    TextSprites& sprites_;
    std::string title_;
    std::vector<GameCommand> commands_;
    std::shared_ptr<SuiteButton> back_, more_;
    std::vector<std::shared_ptr<SuiteButton>> buttons_;
    std::array<std::shared_ptr<SuiteButton>, 4> switches_{};
    SettingsObservation masters_{};
    std::vector<gf::SubscriptionToken> subscriptions_;
    std::vector<gf::SubscriptionToken> command_subscriptions_;
    double open_ = 0, idle_ = 0, max_width_ = 1000;
    bool pinned_ = false;
    struct Slot {
        std::shared_ptr<SuiteButton> button;
        gf::Rect rect;
        bool folded;
    };
    std::vector<Slot> slots_;
    gf::Size folded_size_{}, open_size_{};
    double title_width_ = 0;
    void layout_slots();
    void clicked(gf::ButtonBase& button);
    void masters_changed();
};
} // namespace games
