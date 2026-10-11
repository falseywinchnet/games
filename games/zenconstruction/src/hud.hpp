#pragma once
// Rock Stack's controls over the worksite: paper cards, the slings' meter and
// the hold-to-confirm button. They float over the live scene as GUI.Forms
// controls; the scene itself carries only what belongs to the world (the
// height marks beside the stack, the dimming behind a panel).
#include "suite.hpp"

#include "gui_forms/basic_controls.hpp"

namespace zc {

namespace gf = gui_forms;

// A rounded card of paper edged in timber. fade (0..1) scales the whole card,
// for a line of speech that comes and goes. A passive card lets the pointer
// through to the worksite.
class Paper final : public gf::Control {
public:
    Paper(gf::StableId id, double radius, gf::Color fill, double edge, bool passive = false);
    void set_fade(double fade);
    [[nodiscard]] double fade() const { return fade_; }
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    [[nodiscard]] bool hit_test_local(gf::Point local) const override;

private:
    double radius_, edge_, fade_ = 1;
    gf::Color fill_;
    bool passive_;
};

// Words over the worksite that the pointer passes through.
class Words final : public gf::Label {
public:
    Words(gf::StableId id, std::string text = {}) : Label(std::move(id), std::move(text)) {}
    [[nodiscard]] bool hit_test_local(gf::Point) const override { return false; }
};

// The slings' tension, a bar that fills, with what it means above it.
class SlingMeter final : public gf::Control {
public:
    explicit SlingMeter(gf::StableId id);
    // Repaints only when the shown amount or the words change.
    void set_tension(double tension);
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    [[nodiscard]] bool hit_test_local(gf::Point) const override { return false; }

private:
    double shown_ = -1;
    bool slack_ = true;
};

// Start over: it acts only after it has been held down, filling as it is held.
class HoldButton final : public games::SuiteButton {
public:
    HoldButton(gf::StableId id, std::string text);
    [[nodiscard]] bool holding() const { return enabled() && pressed_visual() && hovered_visual(); }
    // 0 while not held; 1 when the hold is complete.
    void set_progress(double progress);
    void on_paint(gf::Painter& painter, gf::Rect damage) override;

private:
    std::string rest_;
    double progress_ = 0;
};

}  // namespace zc
