#pragma once
#include "gui_forms/basic_controls.hpp"
#include <array>
#include <string>
#include <string_view>
#include <vector>
namespace games {
namespace gf = gui_forms;

// PlaySuite lists every game as its own box. Values are persisted; append only.
enum class Entry : int {
    solitaire,
    spider,
    freecell,
    hearts,
    sudoku,
    gems,
    cube,
    untangle,
    atom,
    pegs,
    switchbox,
    solve,
    eggy,
    koikoi,
    parrots,
    liarsdice,
    penthesheep,
    rockstack,
};
inline constexpr int entry_count = 18;

struct EntryInfo {
    const char* title;
    const char* kind;
    const char* blurb;
    gf::Color cover_top, cover_bottom, accent;
};
const EntryInfo& entry_info(Entry entry);
const char* suite_name();

// A command a game offers in the PlaySuite capsule. Primary commands stay visible
// while the capsule is folded; the rest appear when it opens.
struct GameCommand {
    std::string id;
    std::string label;
    bool enabled = true;
    bool checked = false;
    bool primary = false;
};
class CommandSource {
  public:
    virtual ~CommandSource() = default;
    [[nodiscard]] virtual std::vector<GameCommand> commands() const = 0;
    virtual void run_command(std::string_view id) = 0;
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

enum class Glyph { none, back, play, music, sound, motion, more };
void paint_glyph(gf::Painter& p, gf::Rect r, Glyph glyph, gf::Color ink, bool crossed = false);

class SuiteButton : public gf::Button {
  public:
    SuiteButton(gf::StableId id, std::string text, GlossTone tone = GlossTone::chrome);
    void set_tone(GlossTone tone);
    void set_glyph(Glyph glyph, bool crossed = false);
    void set_radius(double radius);
    void set_checked(bool checked);
    [[nodiscard]] double preferred_width() const;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;

  private:
    GlossTone tone_;
    Glyph glyph_ = Glyph::none;
    bool crossed_ = false, checked_ = false;
    double radius_ = 4;
};
} // namespace games
