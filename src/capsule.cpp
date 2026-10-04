#include "capsule.hpp"
#include <algorithm>
#include <cmath>
namespace games {
namespace {
constexpr double kPad = 5, kGap = 4, kButton = 32, kIcon = 34;
// Room around the pill for its soft shadow; the control is larger than what it draws.
constexpr double kMargin = 14;
constexpr gf::Color rgb(int r, int g, int b, int a = 255) {
    return gf::Color::rgba(static_cast<unsigned char>(r), static_cast<unsigned char>(g),
                           static_cast<unsigned char>(b), static_cast<unsigned char>(a));
}
double ease(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
SpriteSpec title_spec(const std::string& title) {
    return {title, Face::condensed, true, 19, rgb(255, 230, 172)};
}
} // namespace
CommandCapsule::CommandCapsule(gf::StableId id, TextSprites& sprites)
    : Control(std::move(id)), sprites_(sprites) {
    set_accessible_name("Game commands");
    set_paint_plane(gf::PaintPlane::overlay);
}
void CommandCapsule::initialize_control_tree() {
    back_ = gf::make_control<SuiteButton>(gf::StableId("capsule.back"), "", GlossTone::smoke);
    (*back_).set_glyph(Glyph::back);
    (*back_).set_radius(15);
    (*back_).set_accessible_name("Back to the shelf");
    add_child(back_);
    more_ = gf::make_control<SuiteButton>(gf::StableId("capsule.more"), "", GlossTone::smoke);
    (*more_).set_glyph(Glyph::more);
    (*more_).set_radius(15);
    (*more_).set_accessible_name("Keep the commands open");
    add_child(more_);
    const char* names[] = {"Music", "Sound", "Motion"};
    const Glyph glyphs[] = {Glyph::music, Glyph::sound, Glyph::motion};
    for (int i = 0; i < 3; ++i) {
        switches_[i] = gf::make_control<SuiteButton>(
            gf::StableId(std::string("capsule.") + names[i]), "", GlossTone::smoke);
        (*switches_[i]).set_glyph(glyphs[i]);
        (*switches_[i]).set_radius(15);
        (*switches_[i]).set_accessible_name(names[i]);
        add_child(switches_[i]);
    }
    for (const std::shared_ptr<SuiteButton>& b :
         {back_, more_, switches_[0], switches_[1], switches_[2]})
        subscriptions_.push_back((*b).clicked().subscribe(
            *this,
            gf::Delegate<gf::ButtonBase&>::bind<CommandCapsule, &CommandCapsule::clicked>(*this)));
}
void CommandCapsule::set_game(std::string title, std::vector<GameCommand> commands) {
    bool same = title == title_ && commands.size() == commands_.size();
    for (std::size_t i = 0; same && i < commands.size(); ++i)
        same = commands[i].id == commands_[i].id && commands[i].label == commands_[i].label &&
               commands[i].primary == commands_[i].primary;
    if (same) {
        for (std::size_t i = 0; i < commands.size(); ++i) {
            if ((*buttons_[i]).enabled() != commands[i].enabled)
                (*buttons_[i]).set_enabled(commands[i].enabled);
            (*buttons_[i]).set_checked(commands[i].checked);
        }
        commands_ = std::move(commands);
        return;
    }
    for (const std::shared_ptr<SuiteButton>& b : buttons_)
        static_cast<void>(remove_child((*b).runtime_id()));
    buttons_.clear();
    command_subscriptions_.clear();
    title_ = std::move(title);
    commands_ = std::move(commands);
    for (const GameCommand& c : commands_) {
        std::shared_ptr<SuiteButton> b =
            gf::make_control<SuiteButton>(gf::StableId("capsule.cmd." + c.id), c.label,
                                          c.primary ? GlossTone::gold : GlossTone::smoke);
        (*b).set_radius(15);
        (*b).set_font({gf::FontRole::content, 13, 700, false, .2});
        (*b).set_enabled(c.enabled);
        (*b).set_checked(c.checked);
        (*b).set_accessible_name(c.label);
        add_child(b);
        command_subscriptions_.push_back((*b).clicked().subscribe(
            *this,
            gf::Delegate<gf::ButtonBase&>::bind<CommandCapsule, &CommandCapsule::clicked>(*this)));
        buttons_.push_back(std::move(b));
    }
    layout_slots();
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
void CommandCapsule::set_preferences(bool music, bool sound, bool reduced) {
    (*switches_[0]).set_glyph(Glyph::music, !music);
    (*switches_[1]).set_glyph(Glyph::sound, !sound);
    (*switches_[2]).set_glyph(Glyph::motion, reduced);
    (*switches_[0]).set_accessible_name(music ? "Music on" : "Music off");
    (*switches_[1]).set_accessible_name(sound ? "Sound on" : "Sound off");
    (*switches_[2]).set_accessible_name(reduced ? "Reduced motion" : "Full motion");
}
void CommandCapsule::set_maximum_width(double width) {
    max_width_ = std::max(200.0, width);
    layout_slots();
}
void CommandCapsule::fold() {
    pinned_ = false;
    (*more_).set_checked(false);
    open_ = 0;
    idle_ = 1;
}
void CommandCapsule::layout_slots() {
    slots_.clear();
    title_width_ = std::ceil(sprites_.measure(title_spec(title_)).width) + 18;
    double x = kPad, y = kPad, right = 0;

    struct PlaceSlot {
        std::vector<Slot>& slots_;
        double max_width_;
        double& x;
        double& y;
        double& right;
        void operator()(const std::shared_ptr<SuiteButton>& b, double w, bool folded) const {
            if (!folded && x + w + kPad > max_width_ && x > kPad + kIcon + 1) {
                right = std::max(right, x - kGap);
                x = kPad + kIcon + kGap;
                y += kButton + kGap;
            }
            slots_.push_back({b, {kMargin + x, kMargin + y, w, kButton}, folded});
            x += w + kGap;
        }
    };
    PlaceSlot place{slots_, max_width_, x, y, right};
    place(back_, kIcon, true);
    x += title_width_ + kGap;
    for (std::size_t i = 0; i < commands_.size(); ++i)
        if (commands_[i].primary)
            place(buttons_[i], (*buttons_[i]).preferred_width(), true);
    place(more_, kIcon, true);
    folded_size_ = {x - kGap + kPad, kButton + 2 * kPad};
    for (std::size_t i = 0; i < commands_.size(); ++i)
        if (!commands_[i].primary)
            place(buttons_[i], (*buttons_[i]).preferred_width(), false);
    x += 8;
    for (const std::shared_ptr<SuiteButton>& s : switches_)
        place(s, kIcon, false);
    right = std::max(right, x - kGap);
    open_size_ = {std::max(folded_size_.width, right + kPad), y + kButton + kPad};
}
gf::Size CommandCapsule::pill_size() const {
    const double t = ease(open_);
    return {std::ceil(folded_size_.width + (open_size_.width - folded_size_.width) * t),
            std::ceil(folded_size_.height + (open_size_.height - folded_size_.height) * t)};
}
gf::Rect CommandCapsule::placement(gf::Rect area) const {
    const gf::Size pill = pill_size();
    const double w = pill.width, h = pill.height;
    // Expansion must not move Back or the primary commands away from a
    // pointer approaching them. Keep the same leading edge in both states.
    return {area.x + 8 - kMargin, area.y + 6 - kMargin, std::ceil(w) + 2 * kMargin,
            std::ceil(h) + 2 * kMargin};
}
bool CommandCapsule::step(double dt, bool inside, bool reduced) {
    if (inside || pinned_)
        idle_ = 0;
    else
        idle_ += dt;
    // Opening is immediate on hover; folding waits a moment so passing pointers don't flicker it.
    const double target = inside || pinned_ || (open_ > 0 && idle_ < .45) ? 1.0 : 0.0;
    double next = reduced ? target : open_ + std::clamp(target - open_, -dt * 5.5, dt * 6.5);
    if (std::abs(next - open_) < 1e-6)
        return false;
    open_ = next;
    return true;
}
void CommandCapsule::arrange(gf::Rect bounds) {
    for (const std::shared_ptr<gf::Control>& child : children())
        (*child).set_paint_plane(gf::PaintPlane::overlay);
    arrange_self(bounds);
    layout_slots();
    // The pill, not the control bounds, decides what shows: a host may crop the shadow margin.
    const gf::Size pill = pill_size();
    for (const Slot& s : slots_) {
        const bool inside = s.rect.x + s.rect.width <= kMargin + pill.width - kPad + .5 &&
                            s.rect.y + s.rect.height <= kMargin + pill.height - kPad + .5;
        (*s.button).set_visible(inside);
        set_child_layout(s.button, s.rect);
    }
}
bool CommandCapsule::hit_test_local(gf::Point local) const {
    const gf::Size pill = pill_size();
    return local.x >= kMargin && local.y >= kMargin && local.x < kMargin + pill.width &&
           local.y < kMargin + pill.height;
}
void CommandCapsule::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Size pill = pill_size();
    const gf::Rect b{kMargin, kMargin, pill.width, pill.height};
    const double radius = std::min(21.0, b.height * .5);
    p.draw_box_shadow(b, radius, {0, 4}, 12, 0, rgb(0, 0, 0, 120));
    p.save();
    p.clip_rounded_rect(b, radius);
    fill_vertical(p, b, rgb(40, 48, 72, 228), rgb(16, 20, 34, 236));
    p.draw_line({b.x + radius, b.y + 1.5}, {b.x + b.width - radius, b.y + 1.5},
                rgb(255, 255, 255, 60), 1);
    p.restore();
    p.stroke_rounded_rect(b, radius, rgb(255, 210, 122, 120), 1);
    const SpriteSpec title = title_spec(title_);
    const gf::Size ts = sprites_.measure(title);
    sprites_.draw(p, title,
                  {kMargin + kPad + kIcon + kGap + 9, kMargin + kPad + (kButton - ts.height) * .5});
}
void CommandCapsule::clicked(gf::ButtonBase& button) {
    if (&button == back_.get()) {
        if (back)
            back();
        return;
    }
    if (&button == more_.get()) {
        pinned_ = !pinned_;
        (*more_).set_checked(pinned_);
        if (!pinned_)
            idle_ = .3;
        return;
    }
    for (int i = 0; i < 3; ++i)
        if (&button == switches_[i].get()) {
            if (toggle)
                toggle(i);
            return;
        }
    for (std::size_t i = 0; i < buttons_.size(); ++i)
        if (&button == buttons_[i].get() && command) {
            const std::string id = commands_[i].id;
            command(id);
            return;
        }
}
} // namespace games
