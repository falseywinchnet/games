#include "hud.hpp"

#include <algorithm>
#include <cmath>

namespace zc {

namespace {
constexpr gf::Color kTimber = gf::Color::rgba(0x8A, 0x64, 0x40);

gf::Color faded(gf::Color c, double fade) {
    return gf::Color::rgba(c.red, c.green, c.blue, static_cast<std::uint8_t>(std::lround(c.alpha * std::clamp(fade, 0.0, 1.0))));
}
}  // namespace

Paper::Paper(gf::StableId id, double radius, gf::Color fill, double edge, bool passive)
    : Control(std::move(id)), radius_(radius), edge_(edge), fill_(fill), passive_(passive) {
    set_paint_plane(gf::PaintPlane::overlay);
}

void Paper::set_fade(double fade) {
    fade = std::clamp(fade, 0.0, 1.0);
    if (std::abs(fade - fade_) < 0.004) {
        return;
    }
    fade_ = fade;
    invalidate(gf::Dirty::paint);
}

void Paper::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect r = client_rectangle();
    p.fill_rounded_rect(r, radius_, faded(fill_, fade_));
    if (edge_ > 0) {
        const gf::Rect inner{r.x + edge_ * .5, r.y + edge_ * .5, r.width - edge_, r.height - edge_};
        p.stroke_rounded_rect(inner, radius_, faded(kTimber, fade_), edge_);
    }
}

bool Paper::hit_test_local(gf::Point local) const {
    return !passive_ && Control::hit_test_local(local);
}

SlingMeter::SlingMeter(gf::StableId id) : Control(std::move(id)) {
    set_paint_plane(gf::PaintPlane::overlay);
}

void SlingMeter::set_tension(double tension) {
    tension = std::clamp(tension, 0.0, 1.2);
    const bool slack = tension < 0.1;
    // a hundredth of the bar is below what the eye follows
    if (slack == slack_ && std::abs(tension - shown_) < 0.01) {
        return;
    }
    shown_ = tension;
    slack_ = slack;
    invalidate(gf::Dirty::paint);
}

void SlingMeter::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect r = client_rectangle();
    const gf::FontSpec f{gf::FontRole::content, 10.5, 400, false};
    p.draw_text_utf8({0, 11}, slack_ ? "slings slack: resting" : "slings taut", f, gf::Color::rgba(255, 255, 255, 230));
    const gf::Rect bar{0, r.height - 14, r.width, 14};
    p.fill_rounded_rect(bar, 7, gf::Color::rgba(0, 0, 0, 89));
    const double fill = (bar.width - 4) * std::min(1.0, std::max(0.0, shown_));
    if (fill > 0.5) {
        p.fill_rounded_rect({2, bar.y + 2, fill, 10}, 5, slack_ ? gf::Color::rgba(0x9A, 0xD4, 0x8A) : gf::Color::rgba(0xF7, 0xBC, 0x1E));
    }
}

HoldButton::HoldButton(gf::StableId id, std::string text) : SuiteButton(std::move(id), text, games::GlossTone::chrome), rest_(std::move(text)) {}

void HoldButton::set_progress(double progress) {
    progress = std::clamp(progress, 0.0, 1.0);
    if (progress == progress_) {
        return;
    }
    const bool was = progress_ > 0;
    progress_ = progress;
    if (was != (progress_ > 0)) {
        set_text(progress_ > 0 ? std::string("Keep holding...") : rest_);
    }
    invalidate(gf::Dirty::paint);
}

void HoldButton::on_paint(gf::Painter& p, gf::Rect damage) {
    SuiteButton::on_paint(p, damage);
    if (progress_ <= 0) {
        return;
    }
    // the hold fills the face from the left, the words still showing through
    const gf::Rect b = client_rectangle();
    const gf::Rect face{2, 2, b.width - 4, b.height - 5};
    const double radius = std::min(4.0, face.height * .5);
    p.save();
    p.clip_rounded_rect(face, radius);
    p.fill_rect({face.x, face.y, face.width * progress_, face.height}, gf::Color::rgba(0xE8, 0x82, 0x6A, 120));
    p.restore();
}

}  // namespace zc
