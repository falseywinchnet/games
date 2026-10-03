#include "shelf.hpp"
#include "gui_forms/window.hpp"
#include "presentation.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
namespace games {
namespace {
constexpr gf::Color rgb(int r, int g, int b, int a = 255) {
    return gf::Color::rgba(static_cast<unsigned char>(r), static_cast<unsigned char>(g),
                           static_cast<unsigned char>(b), static_cast<unsigned char>(a));
}
constexpr double kLiftRoom = 16;
constexpr double kAspect = 1.38; // box height / width, like a big-box game
constexpr gf::Color gold = rgb(255, 210, 122);
std::string upper(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
// A plank seen slightly from above: lit top edge, grained front, cast shadow.
void paint_plank(gf::Painter& p, double x, double y, double w, double depth) {
    p.fill_rect({x, y + depth + 4, w, 10}, rgb(10, 5, 2, 120));
    fill_vertical(p, {x, y - 5, w, 6}, rgb(176, 124, 76), rgb(140, 92, 52));
    fill_vertical(p, {x, y, w, depth}, rgb(122, 78, 42), rgb(74, 44, 22));
    for (int i = 0; i < 3; ++i)
        p.draw_line({x, y + 3 + i * depth / 3.4}, {x + w, y + 3.5 + i * depth / 3.4},
                    rgb(60, 34, 16, 70), 1);
    p.draw_line({x, y - 5}, {x + w, y - 5}, rgb(214, 170, 120, 160), 1);
    p.draw_line({x, y + depth}, {x + w, y + depth}, rgb(30, 16, 6), 1);
}
} // namespace

ShelfBox::ShelfBox(gf::StableId id, Entry entry, TextSprites& sprites)
    : Button(std::move(id), entry_info(entry).title), entry_(entry), sprites_(sprites) {
    set_use_mnemonic(false);
    set_accessible_name(std::string(entry_info(entry).title) + ". " + entry_info(entry).blurb);
}
bool ShelfBox::step(double dt, bool reduced) {
    const double target = selected() ? 1.0 : hot() ? .55 : 0.0;
    const double next = reduced ? target : lift_ + (target - lift_) * std::min(1.0, dt * 13);
    const bool moving = std::abs(next - target) > .002;
    if (std::abs(next - lift_) > 1e-4) {
        lift_ = moving ? next : target;
        invalidate(gf::Dirty::paint);
    }
    return moving;
}
void ShelfBox::on_paint(gf::Painter& p, gf::Rect) {
    const EntryInfo& info = entry_info(entry_);
    const gf::Rect b = client_rectangle();
    const double h = b.height - kLiftRoom, depth = std::max(4.0, b.width * .07);
    const double y0 = kLiftRoom * (1 - lift_);
    const gf::Rect c{0, y0, b.width - depth, h};
    // Shadow falls onto the back of the shelf; it softens as the box comes forward.
    p.draw_box_shadow(c, 2, {3 + lift_ * 2, 4 + lift_ * 5}, 5 + lift_ * 6, 0, rgb(0, 0, 0, 150));
    if (selected())
        p.draw_box_shadow(c, 3, {0, 0}, 14, 2, with_alpha(gold, 150));
    // Right side of the box, receding.
    paint_polygon(p,
                  {{c.x + c.width, c.y},
                   {c.x + c.width + depth, c.y + depth * .7},
                   {c.x + c.width + depth, c.y + c.height - depth * .3},
                   {c.x + c.width, c.y + c.height}},
                  mix_color(info.cover_bottom, rgb(0, 0, 0), .35));
    p.draw_line({c.x + c.width, c.y}, {c.x + c.width + depth, c.y + depth * .7},
                with_alpha(info.accent, 90), 1);
    p.save();
    p.clip_rect(c);
    fill_vertical(p, c, info.cover_top, info.cover_bottom);
    // A soft glow behind the emblem, like studio lighting on box art.
    const double emblem = std::min(c.width * .62, c.height * .42);
    const gf::Point center{c.x + c.width * .5, c.y + c.height * .42};
    const gf::GradientStop glow[] = {{0, with_alpha(info.accent, 110)},
                                     {1, with_alpha(info.accent, 0)}};
    p.fill_radial_gradient({center.x - emblem, center.y - emblem, emblem * 2, emblem * 2}, center,
                           {emblem * .95, emblem * .95}, glow);
    paint_entry_emblem(p, {center.x - emblem * .5, center.y - emblem * .5, emblem, emblem}, entry_);
    // Title, condensed and uppercase, on a dark scrim so every cover reads.
    const double title_y = c.y + c.height * .7;
    p.fill_rect({c.x, title_y - 2, c.width, c.height * .3 + 2}, rgb(0, 0, 0, 70));
    double size = std::clamp(c.width * .17, 10.0, 22.0);
    std::string title = upper(info.title);
    SpriteSpec t{title, Face::condensed, true, size, rgb(255, 255, 255)};
    gf::Size ts = sprites_.measure(t);
    while (ts.width > c.width - 8 && size > 9) {
        size -= 1;
        t.size = size;
        ts = sprites_.measure(t);
    }
    sprites_.draw(p, t, {c.x + (c.width - ts.width) * .5, title_y});
    SpriteSpec k{upper(info.kind), Face::condensed, false, std::clamp(size * .62, 8.0, 13.0),
                 info.accent};
    gf::Size ks = sprites_.measure(k);
    if (ks.width <= c.width - 6)
        sprites_.draw(p, k, {c.x + (c.width - ks.width) * .5, title_y + ts.height * .92});
    p.fill_rect({c.x, c.y + c.height - 4, c.width, 4}, info.accent);
    // Gloss sheen across the shrink-wrap.
    const gf::GradientStop sheen[] = {{0, rgb(255, 255, 255, 70)},
                                      {.42, rgb(255, 255, 255, 12)},
                                      {.43, rgb(255, 255, 255, 0)},
                                      {1, rgb(255, 255, 255, 0)}};
    p.fill_linear_gradient(c, {c.x, c.y}, {c.x + c.width, c.y + c.height}, sheen);
    p.restore();
    p.draw_line({c.x + .5, c.y}, {c.x + .5, c.y + c.height}, rgb(255, 255, 255, 70), 1);
    p.stroke_rect(c, selected() ? gold : rgb(0, 0, 0, 160), selected() ? 2 : 1);
    if (focus_cue_visible() && !selected())
        p.stroke_rect({c.x - 3, c.y - 3, c.width + 6, c.height + 6}, with_alpha(gold, 200), 1);
}

void LaunchCurtain::on_paint(gf::Painter& p, gf::Rect) {
    if (progress <= 0)
        return;
    const gf::Rect b = client_rectangle();
    const double t = progress * progress * (3 - 2 * progress);
    const gf::Rect r{from.x + (b.x - from.x) * t, from.y + (b.y - from.y) * t,
                     from.width + (b.width - from.width) * t,
                     from.height + (b.height - from.height) * t};
    const EntryInfo& info = entry_info(entry);
    p.draw_box_shadow(r, 6 * (1 - t), {0, 10 * (1 - t)}, 24, 0,
                      rgb(0, 0, 0, static_cast<int>(160 * (1 - t))));
    fill_vertical(p, r, info.cover_top, info.cover_bottom);
    const double emblem = std::min(r.width, r.height) * (.5 - .2 * t);
    const gf::GradientStop glow[] = {{0, with_alpha(info.accent, 140)},
                                     {1, with_alpha(info.accent, 0)}};
    const gf::Point c{r.x + r.width * .5, r.y + r.height * .45};
    p.fill_radial_gradient(r, c, {emblem * 1.4, emblem * 1.4}, glow);
    paint_entry_emblem(p, {c.x - emblem * .5, c.y - emblem * .5, emblem, emblem}, entry);
}
ShelfView::ShelfView(gf::StableId id, TextSprites& sprites)
    : Control(std::move(id)), sprites_(sprites) {
    set_focusable(false);
}
void ShelfView::initialize_control_tree() {
    for (int i = 0; i < entry_count; ++i) {
        boxes_[i] = gf::make_control<ShelfBox>(gf::StableId("shelf.box." + std::to_string(i)),
                                               static_cast<Entry>(i), sprites_);
        add_child(boxes_[i]);
        subscriptions_.push_back(
            (*boxes_[i])
                .clicked()
                .subscribe(*this,
                           gf::Delegate<gf::ButtonBase&>::bind<ShelfView, &ShelfView::clicked_box>(
                               *this)));
    }
    const char* names[] = {"Music", "Sound", "Motion"};
    const Glyph glyphs[] = {Glyph::music, Glyph::sound, Glyph::motion};
    for (int i = 0; i < 3; ++i) {
        switches_[i] = gf::make_control<SuiteButton>(gf::StableId(std::string("shelf.") + names[i]),
                                                     "", GlossTone::smoke);
        (*switches_[i]).set_glyph(glyphs[i]);
        (*switches_[i]).set_radius(15);
        (*switches_[i]).set_accessible_name(names[i]);
        add_child(switches_[i]);
        subscriptions_.push_back(
            (*switches_[i])
                .clicked()
                .subscribe(
                    *this,
                    gf::Delegate<gf::ButtonBase&>::bind<ShelfView, &ShelfView::clicked_switch>(
                        *this)));
    }
    curtain_ = gf::make_control<LaunchCurtain>(gf::StableId("shelf.curtain"));
    (*curtain_).set_visible(false);
    add_child(curtain_);
    select(selection_);
}
void ShelfView::launch(Entry entry) {
    if (launching_)
        return;
    select(entry);
    if (reduced_ || !timer_) {
        if (open)
            open(entry);
        return;
    }
    request_animation();
    launching_ = true;
    launch_t_ = 0;
    (*curtain_).entry = entry;
    const std::shared_ptr<ShelfBox>& box = boxes_[static_cast<std::size_t>(entry)];
    const gf::Rect r = (*box).client_rectangle();
    const gf::Point origin = point_from_window((*box).point_to_window({0, 0}));
    (*curtain_).from = {origin.x, origin.y + 16, r.width * .93, r.height - 16};
    (*curtain_).progress = 0;
    (*curtain_).set_visible(true);
    last_ = std::chrono::steady_clock::now();
}
void ShelfView::on_attached_to_window() {
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(16));
    subscriptions_.push_back((*timer_).tick().subscribe(
        *this, gf::Delegate<>::bind<ShelfView, &ShelfView::tick>(*this)));
    last_ = std::chrono::steady_clock::now();
    (*timer_).start();
}
void ShelfView::on_detaching_from_window(gf::Window&) noexcept {
    if (timer_)
        (*timer_).stop();
    timer_.reset();
}
void ShelfView::request_animation() {
    if (!timer_ || !visible())
        return;
    if (!(*timer_).enabled())
        last_ = std::chrono::steady_clock::now();
    (*timer_).start();
}
void ShelfView::on_pointer_preview(gf::PointerEvent& e) {
    request_animation();
    gf::Control::on_pointer_preview(e);
}
void ShelfView::tick() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - last_).count(), 0.0, .1);
    last_ = now;
    if (!visible()) {
        if (timer_)
            (*timer_).stop();
        return;
    }
    bool moving = launching_;
    Entry shown = selection_;
    for (const std::shared_ptr<ShelfBox>& box : boxes_) {
        moving = (*box).step(dt, reduced_) || moving;
        if ((*box).hot())
            shown = (*box).entry();
    }
    if (shown != shown_) {
        shown_ = shown;
        invalidate(gf::Dirty::paint);
    }
    if (launching_) {
        launch_t_ += dt / .2;
        (*curtain_).progress = std::min(1.0, launch_t_);
        (*curtain_).invalidate(gf::Dirty::paint);
        if (launch_t_ >= 1.0) {
            launching_ = false;
            (*curtain_).set_visible(false);
            (*curtain_).progress = 0;
            if (open)
                open((*curtain_).entry);
        }
    }
    if (!moving && timer_)
        (*timer_).stop();
}
void ShelfView::set_preferences(bool music, bool sound, bool reduced) {
    reduced_ = reduced;
    request_animation();
    (*switches_[0]).set_glyph(Glyph::music, !music);
    (*switches_[1]).set_glyph(Glyph::sound, !sound);
    (*switches_[2]).set_glyph(Glyph::motion, reduced);
    (*switches_[0]).set_accessible_name(music ? "Music on" : "Music off");
    (*switches_[1]).set_accessible_name(sound ? "Sound on" : "Sound off");
    (*switches_[2]).set_accessible_name(reduced ? "Reduced motion" : "Full motion");
}
void ShelfView::set_progress(Entry entry, bool started) {
    started_[static_cast<std::size_t>(entry)] = started;
    if (entry == selection_ || entry == shown_)
        invalidate(gf::Dirty::paint);
}
void ShelfView::select(Entry entry) {
    request_animation();
    selection_ = entry;
    shown_ = entry;
    for (const std::shared_ptr<ShelfBox>& box : boxes_)
        if (box)
            (*box).set_selected((*box).entry() == entry);
    invalidate(gf::Dirty::paint);
}
void ShelfView::focus_selection() {
    if (attached_window())
        static_cast<void>(
            (*attached_window()).request_focus(boxes_[static_cast<std::size_t>(selection_)]));
}
void ShelfView::clicked_box(gf::ButtonBase& button) {
    const ShelfBox* box = dynamic_cast<ShelfBox*>(&button);
    if (!box)
        return;
    launch((*box).entry());
}
void ShelfView::clicked_switch(gf::ButtonBase& button) {
    for (int i = 0; i < 3; ++i)
        if (&button == switches_[i].get() && toggle)
            toggle(i);
}
void ShelfView::on_key_bubble(gf::KeyEvent& e) {
    if (e.handled || e.action != gf::KeyAction::down)
        return;
    using K = gf::PhysicalKey;
    int i = static_cast<int>(selection_), next = i;
    if (e.physical_key == K::left)
        next = std::max(0, i - 1);
    else if (e.physical_key == K::right)
        next = std::min(entry_count - 1, i + 1);
    else if (e.physical_key == K::up)
        next = std::max(0, i - columns_);
    else if (e.physical_key == K::down)
        next = std::min(entry_count - 1, i + columns_);
    else if (e.physical_key == K::home)
        next = 0;
    else if (e.physical_key == K::end)
        next = entry_count - 1;
    else
        return;
    select(static_cast<Entry>(next));
    focus_selection();
    e.handled = true;
}
void ShelfView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    const double w = bounds.width, h = bounds.height;
    const bool compact = h < 520 || w < 760;
    header_ = {0, 0, w, compact ? 54.0 : 76.0};
    ticket_ = {0, h - (compact ? 74.0 : 96.0), w, compact ? 74.0 : 96.0};
    const double top = header_.height + 12, bottom = ticket_.y - 4, margin = compact ? 14 : 28;
    const double area_w = w - 2 * margin, area_h = std::max(60.0, bottom - top);
    // Choose the row count that gives the largest boxes that fit both ways.
    int best_rows = 1;
    double best_w = 0;
    for (int rows = 1; rows <= 5; ++rows) {
        const int cols = (entry_count + rows - 1) / rows;
        const double by_width = area_w / (cols + (cols - 1) * .16);
        const double pitch_extra = kLiftRoom + 26;
        const double by_height = (area_h / rows - pitch_extra) / kAspect;
        const double bw = std::min({by_width, by_height, 150.0});
        if (bw > best_w + .5) {
            best_w = bw;
            best_rows = rows;
        }
    }
    box_width_ = std::max(34.0, best_w);
    const double box_h = box_width_ * kAspect, gap = box_width_ * .16;
    const double pitch = box_h + kLiftRoom + 26;
    const double used = pitch * best_rows;
    double y = top + std::max(0.0, (area_h - used) * .5);
    columns_ = (entry_count + best_rows - 1) / best_rows;
    shelf_lines_.clear();
    int index = 0;
    for (int row = 0; row < best_rows; ++row) {
        const int remaining = entry_count - index, rows_left = best_rows - row;
        const int count = (remaining + rows_left - 1) / rows_left;
        const double row_w = count * box_width_ + (count - 1) * gap;
        double x = (w - row_w) * .5;
        for (int k = 0; k < count; ++k, ++index) {
            set_child_layout(boxes_[index], {x, y, box_width_, box_h + kLiftRoom});
            x += box_width_ + gap;
        }
        shelf_lines_.push_back(y + kLiftRoom + box_h);
        y += pitch;
    }
    set_child_layout(curtain_, {0, 0, w, h});
    const double sw = compact ? 34 : 38;
    for (int i = 0; i < 3; ++i)
        set_child_layout(switches_[i], {w - (3 - i) * (sw + 6) - (compact ? 10 : 22),
                                        (header_.height - sw) * .5, sw, sw});
}
void ShelfView::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    const EntryInfo& chosen = entry_info(shown_);
    // Walnut wall with vertical panels and a warm spotlight from above.
    fill_vertical(p, b, rgb(58, 36, 22), rgb(30, 18, 10));
    for (double x = 0; x < b.width; x += 92) {
        p.draw_line({x, header_.height}, {x, b.height}, rgb(20, 11, 5, 120), 1);
        p.draw_line({x + 1, header_.height}, {x + 1, b.height}, rgb(120, 80, 48, 40), 1);
    }
    const gf::GradientStop spot[] = {{0, rgb(255, 214, 150, 60)}, {1, rgb(255, 214, 150, 0)}};
    p.fill_radial_gradient(b, {b.width * .5, header_.height}, {b.width * .62, b.height * .78},
                           spot);
    for (double line : shelf_lines_)
        paint_plank(p, 10, line, b.width - 20, 14);
    // Header sign.
    fill_vertical(p, header_, rgb(28, 17, 10), rgb(16, 9, 5));
    p.draw_line({0, header_.height - 2}, {b.width, header_.height - 2}, rgb(140, 102, 48), 1);
    p.draw_line({0, header_.height - 1}, {b.width, header_.height - 1}, rgb(255, 210, 122, 200), 1);
    const bool compact = header_.height < 70;
    SpriteSpec name{suite_name(), Face::serif, true, compact ? 24.0 : 34.0, rgb(255, 228, 168)};
    gf::Size ns = sprites_.measure(name);
    const double nx = compact ? 16 : 28, ny = (header_.height - ns.height) * .5 - (compact ? 0 : 6);
    // A faint engraved shadow under the lettering.
    sprites_.draw(p, {name.text, name.face, true, name.size, rgb(0, 0, 0, 160)}, {nx + 1, ny + 2});
    sprites_.draw(p, name, {nx, ny});
    if (!compact) {
        SpriteSpec tag{"EIGHTEEN GAMES  ·  CARDS, PUZZLES AND A VERY TALL MOUNTAIN",
                       Face::condensed, false, 12, rgb(201, 176, 138)};
        sprites_.draw(p, tag, {nx + 2, ny + ns.height - 2});
    }
    // Ticket for the chosen box: cream card stock with a perforated edge.
    const double inset = compact ? 10 : 20;
    gf::Rect t{inset, ticket_.y + 8, b.width - inset * 2, ticket_.height - 16};
    p.draw_box_shadow(t, 4, {0, 3}, 8, 0, rgb(0, 0, 0, 140));
    fill_vertical(p, t, rgb(247, 238, 214), rgb(232, 218, 186));
    p.stroke_rect(t, rgb(120, 86, 46), 1);
    p.draw_line({t.x + 4, t.y + 4}, {t.x + t.width - 4, t.y + 4}, rgb(160, 120, 70, 90), 1);
    for (double y = t.y + 8; y < t.y + t.height - 4; y += 10)
        p.fill_rounded_rect({t.x - 3, y, 6, 6}, 3, rgb(40, 24, 14));
    const double em = t.height - 18;
    gf::Rect swatch{t.x + 16, t.y + 9, em * .78, em};
    fill_vertical(p, swatch, chosen.cover_top, chosen.cover_bottom);
    p.stroke_rect(swatch, rgb(60, 40, 20), 1);
    paint_entry_emblem(
        p,
        {swatch.x + 3, swatch.y + (em - swatch.width + 6) * .5, swatch.width - 6, swatch.width - 6},
        shown_);
    const double tx = swatch.x + swatch.width + 16;
    const double text_right = b.width - inset - 16;
    SpriteSpec title{chosen.title, Face::serif, true, compact ? 19.0 : 25.0, rgb(52, 30, 14)};
    gf::Size ts = sprites_.measure(title);
    double ty = t.y + (compact ? 7 : 10);
    sprites_.draw(p, title, {tx, ty});
    SpriteSpec kind{upper(chosen.kind), Face::condensed, true, compact ? 11.0 : 13.0,
                    mix_color(chosen.cover_top, rgb(0, 0, 0), .2)};
    const double kx = tx + ts.width + 12;
    if (kx + 120 < text_right)
        sprites_.draw(p, kind, {kx, ty + ts.height - (compact ? 15 : 18)});
    gf::FontSpec body{gf::FontRole::content, compact ? 12.0 : 14.0, 400, false};
    std::string blurb = chosen.blurb;
    while (blurb.size() > 8 && p.measure_text_utf8(blurb, body).width > text_right - tx) {
        std::size_t cut = blurb.rfind(' ', blurb.size() - 2);
        blurb = blurb.substr(0, cut == std::string::npos ? blurb.size() - 4 : cut) + "…";
    }
    p.draw_text_utf8({tx, ty + ts.height + (compact ? 12 : 17)}, blurb, body, rgb(92, 70, 48));
    if (!compact)
        p.draw_text_utf8({tx, t.y + t.height - 8},
                         started_[static_cast<std::size_t>(shown_)]
                             ? "Game in progress. Click the box to pick up where you left off."
                             : "Click a box to play.",
                         {gf::FontRole::content, 11, 400, false}, rgb(140, 116, 86));
}
} // namespace games
