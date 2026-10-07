#pragma once
#include "gui_forms/controls/button_base/check_box/check_box.hpp"
#include "gui_forms/controls/range_control/track_bar/track_bar.hpp"
#include "gui_forms/controls/scrollable_control/scrollable_control.hpp"
#include "help_book.hpp"
#include "suite.hpp"
#include "suite_settings.hpp"
#include <array>
#include <functional>
namespace games {

// A TrackBar that names the setting it moves.
class SettingSlider final : public gf::TrackBar {
  public:
    SettingSlider(gf::StableId id, std::string setting);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    // Sets the value without reporting it as the player's change.
    void show_value(double value);
    // The shell's look: a gold groove and a round gloss knob. Input, keys (arrows,
    // Page Up/Down, Home/End) and accessibility stay the TrackBar's.
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] const std::string& setting() const {
        return setting_;
    }
    std::function<void(const std::string&, double)> moved;

  private:
    std::string setting_;
    gf::SubscriptionToken subscription_{};
    bool quiet_ = false, focused_ = false;
    void changed(double value);
};

// A CheckBox in the shell's look: a gold box, and a soft gold outline for focus that
// shows only when the keyboard moved it (the toolkit's focus cue). Behaviour and
// accessibility stay the CheckBox's.
class SettingCheck final : public gf::CheckBox {
  public:
    SettingCheck(gf::StableId id, std::string text);
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
};

// One of the shared card backs, drawn as it appears on the table.
class CardBackChoice final : public gf::Button {
  public:
    CardBackChoice(gf::StableId id, std::string name, int back);
    void set_image(gf::ImageId image);
    void set_chosen(bool chosen);
    [[nodiscard]] int back() const {
        return back_;
    }
    void on_paint(gf::Painter& painter, gf::Rect damage) override;

  private:
    int back_;
    gf::ImageId image_{};
    bool chosen_ = false;
};

// The scrolling body of the Settings screen.
class SettingsPage final : public gf::ScrollableControl {
  public:
    explicit SettingsPage(gf::StableId id);
    void arrange(gf::Rect bounds) override;
    void on_key_bubble(gf::KeyEvent& event) override;
    // Rebuilds the rows: the masters, then card backs and the game's own section.
    void build(CommandSource* game, const std::string& game_title);
    // Re-reads every value; rebuilds only when the game's entries changed shape.
    void refresh();
    void set_card_back_images(const std::array<gf::ImageId, card_back_count>& images);
    // The height the rows need at this width, for a sheet no taller than its content.
    [[nodiscard]] double content_height(double width) const;

  private:
    enum class Shape { heading, volume, check, chips, slider, backs };
    struct Row {
        Shape shape = Shape::heading;
        std::string setting;                            // game setting id; empty for the masters
        std::shared_ptr<gf::Control> lead;              // heading, label or check box
        std::vector<std::shared_ptr<gf::Control>> items; // slider, chips or card backs
        std::shared_ptr<gf::Label> readout;             // a slider's value
        std::shared_ptr<gf::Label> note;
    };
    CommandSource* game_ = nullptr;
    std::string game_title_;
    std::vector<GameSetting> shape_;   // the game's entries as last built
    std::vector<Row> rows_;
    std::vector<gf::SubscriptionToken> subscriptions_;
    std::array<gf::ImageId, card_back_count> back_images_{};
    using Placement = std::vector<std::pair<std::shared_ptr<gf::Control>, gf::Rect>>;
    // Lays the rows out at a width; returns the total height.
    double plan(double width, Placement& layout) const;
    int built_ = 0;                    // makes stable ids unique across rebuilds
    std::shared_ptr<gf::Label> heading(const std::string& text);
    std::shared_ptr<gf::Label> text_label(const std::string& text, double size, int weight);
    std::shared_ptr<gf::CheckBox> check(const std::string& key, const std::string& text);
    std::shared_ptr<SettingSlider> slider(const std::string& key, const std::string& setting,
                                          const std::string& name, double minimum,
                                          double maximum, double step);
    void add_row(Row row);
    void clicked(gf::ButtonBase& button);
    void slid(const std::string& setting, double value);
    [[nodiscard]] static bool same_shape(const std::vector<GameSetting>& a,
                                         const std::vector<GameSetting>& b);
    [[nodiscard]] static std::string percent(double value);
    [[nodiscard]] static std::string slider_text(const GameSetting& setting, double value);
};

// The shared Settings screen, opened from the cog in the capsule or on the shelf.
// The masters (Music and Sound with their volumes, Motion) for everyone, the card
// back for games that draw card backs, then the open game's declared settings.
// Escape or the close mark leaves it; Tab walks its controls, arrows move sliders.
class SettingsSheet final : public gf::Control {
  public:
    explicit SettingsSheet(gf::StableId id);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void on_key_bubble(gf::KeyEvent& event) override;
    // `game` is null on the shelf. The sheet keeps the pointer while it is shown.
    void show_for(const std::string& title, CommandSource* game);
    void refresh();
    // The first control, for keyboard focus when the sheet opens.
    [[nodiscard]] std::shared_ptr<gf::Control> first_control() const;
    // The paper, in the sheet's coordinates. The sheet itself covers the whole window
    // and dims what is behind the paper; a press outside the paper closes it.
    [[nodiscard]] gf::Rect paper() const {
        return paper_;
    }
    std::function<void()> close;

  private:
    std::shared_ptr<SettingsPage> page_;
    std::shared_ptr<HelpGlyph> close_;
    gf::SubscriptionToken close_subscription_{};
    std::array<gf::ImageId, card_back_count> backs_{};
    std::string title_;
    gf::Rect paper_{};
    void clicked_close(gf::ButtonBase& button);
    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
};
} // namespace games
