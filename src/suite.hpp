#pragma once
#include "gui_forms/basic_controls.hpp"
#include "game_entries.hpp"
#include <array>
#include <string>
#include <string_view>
#include <vector>
namespace games {
namespace gf = gui_forms;

struct EntryInfo {
    const char* title;
    const char* kind;
    const char* blurb;
    gf::Color cover_top, cover_bottom, accent;
};
const EntryInfo& entry_info(Entry entry);
const char* suite_name();

// A command a game offers in the PlaySuite capsule. Primary commands stay visible
// while the capsule is folded; the rest appear when it opens. Commands are actions
// (New game, Undo, Hint, a panel); a choice that persists is a GameSetting.
struct GameCommand {
    std::string id;
    std::string label;
    bool enabled = true;
    bool checked = false;
    bool primary = false;
};
// One entry of a game's section on the shared Settings screen. The game owns and
// saves the value; the screen shows `value` and reports changes to change_setting().
struct GameSetting {
    enum class Kind { toggle, choice, slider };
    std::string id;
    std::string label;
    Kind kind = Kind::toggle;
    double value = 0;                 // toggle: 0 or 1; choice: index; slider: minimum..maximum
    std::vector<std::string> choices; // choice only, in order
    double minimum = 0, maximum = 1, step = .1; // slider only
    std::string note;                 // optional; e.g. "Changing it deals again."
};
// What the PlaySuite shell asks of a game, beyond its view. The game declares its
// commands and settings; the shell supplies the rest: Back, Help, the Music, Sound
// and Motion switches, and the Settings screen with the volume sliders (and the
// card back, for a game that draws card backs).
class CommandSource {
  public:
    virtual ~CommandSource() = default;
    [[nodiscard]] virtual std::vector<GameCommand> commands() const = 0;
    virtual void run_command(std::string_view id) = 0;
    [[nodiscard]] virtual std::vector<GameSetting> settings() const {
        return {};
    }
    virtual void change_setting(std::string_view, double) {}
    // True for games that draw the shared card backs (SuiteSettings::card_back).
    [[nodiscard]] virtual bool uses_card_backs() const {
        return false;
    }
};

gf::Color mix_color(gf::Color a, gf::Color b, double t);
gf::Color with_alpha(gf::Color c, int alpha);
void fill_vertical(gf::Painter& p, gf::Rect r, gf::Color top, gf::Color bottom);
void paint_entry_emblem(gf::Painter& p, gf::Rect bounds, Entry entry);

// The paymenottowork house command surface: split gloss, slate border, gold when chosen.
enum class GlossTone { chrome, gold, navy, smoke };
struct GlossState {
    bool hot = false, down = false, enabled = true, focus = false;
};
void paint_gloss(gf::Painter& p, gf::Rect r, double radius, GlossTone tone, GlossState state);
gf::Color gloss_ink(GlossTone tone, bool enabled = true);

enum class Glyph { none, back, play, music, sound, motion, more, settings };
void paint_glyph(gf::Painter& p, gf::Rect r, Glyph glyph, gf::Color ink, bool crossed = false);

class SuiteButton : public gf::Button {
  public:
    SuiteButton(gf::StableId id, std::string text, GlossTone tone = GlossTone::chrome);
    void set_tone(GlossTone tone);
    void set_glyph(Glyph glyph, bool crossed = false);
    void set_radius(double radius);
    void set_checked(bool checked);
    [[nodiscard]] bool crossed() const {
        return crossed_;
    }
    [[nodiscard]] bool checked() const {
        return checked_;
    }
    [[nodiscard]] double preferred_width() const;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;

  private:
    GlossTone tone_;
    Glyph glyph_ = Glyph::none;
    bool crossed_ = false, checked_ = false;
    double radius_ = 4;
};
} // namespace games
