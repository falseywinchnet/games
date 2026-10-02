#include "table.hpp"
#include "audio.hpp"
#include "carpet.hpp"
#include "gui_forms/window.hpp"
#include "presentation.hpp"
#include "solitaire_solver.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace games {
// The PlaySuite capsule floats over the top of the felt; play starts below it.
static constexpr double kTop = 54;
namespace {
bool inside_rectangle(gf::Rect r, gf::Point p) {
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
}
gf::Rect enlarged(gf::Rect r, double n) {
    return {r.x - n, r.y - n, r.width + 2 * n, r.height + 2 * n};
}
const char* audio_name(Kind kind) {
    const char* names[] = {"solitaire", "spider", "freecell", "hearts"};
    return names[static_cast<int>(kind)];
}
// The finish of a real playing card, as a translucent layer for the printed artwork: a fine
// air-cushion dimple texture lit from the upper left, a rolled edge that catches the light,
// and the falloff of a lamp over the table. The artwork's card is a 360 x 504 image whose
// rounded rectangle starts 2 px in with an 18 px corner radius.
std::vector<std::byte> card_finish(int w, int h) {
    std::vector<std::byte> out(static_cast<std::size_t>(w) * h * 4);
    const double s = w / 360.0;
    const double inset = 2.4 * s, radius = 18 * s;
    const double half_w = w * .5 - inset, half_h = h * .5 - inset;
    // Signed distance to the card's rounded outline (negative inside).
    struct Outline {
        double half_w, half_h, radius, cx, cy;
        double operator()(double x, double y) const {
            const double qx = std::abs(x - cx) - (half_w - radius),
                         qy = std::abs(y - cy) - (half_h - radius);
            const double ox = std::max(qx, 0.0), oy = std::max(qy, 0.0);
            return std::hypot(ox, oy) + std::min(std::max(qx, qy), 0.0) - radius;
        }
    };
    const Outline outline{half_w, half_h, radius, w * .5, h * .5};
    // Dimples on a hexagonal lattice, a few device pixels apart, each a little different.
    const double pitch = std::clamp(w / 64.0, 2.6, 4.2), row_h = pitch * .866;
    struct Dimples {
        double pitch, row_h;
        double operator()(double x, double y) const {
            const int row = static_cast<int>(std::floor(y / row_h));
            double depth = 0;
            for (int r = row - 1; r <= row + 1; ++r) {
                const double shift = (r & 1) ? pitch * .5 : 0;
                const int col = static_cast<int>(std::floor((x - shift) / pitch));
                for (int c = col - 1; c <= col + 1; ++c) {
                    std::uint32_t k = static_cast<std::uint32_t>(r * 73856093) ^
                                      static_cast<std::uint32_t>(c * 19349663);
                    k = (k ^ (k >> 13)) * 0x5bd1e995U;
                    const double jitter = ((k >> 8) & 255) / 255.0;
                    const double cx = c * pitch + shift + pitch * .5 + (jitter - .5) * pitch * .2,
                                 cy = r * row_h + row_h * .5;
                    const double d2 =
                        ((x - cx) * (x - cx) + (y - cy) * (y - cy)) / (pitch * pitch * .2);
                    if (d2 < 1)
                        depth -= (1 - d2) * (1 - d2) * (.75 + .5 * jitter);
                }
            }
            return depth;
        }
    };
    const Dimples dimples{pitch, row_h};
    const double lx = -.5, ly = -.7; // toward the lamp, in the card plane
    const double rim = std::max(1.6, 3.2 * s);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double px = x + .5, py = y + .5;
            const double d = outline(px, py);
            const double cover = std::clamp(.5 - d, 0.0, 1.0);
            if (cover <= 0)
                continue;
            double light = 0, dark = 0;
            // Texture: slope of the dimpled surface toward the lamp.
            const double gx = dimples(px + .5, py) - dimples(px - .5, py),
                         gy = dimples(px, py + .5) - dimples(px, py - .5);
            const double facing = -(gx * lx + gy * ly) * 2.2;
            if (facing > 0)
                light += std::min(.1, facing * .1);
            else
                dark += std::min(.06, -facing * .06);
            // Rolled edge: the outline's outward normal, lit or shaded.
            const double depth_in = -d;
            if (depth_in < rim) {
                const double nx = outline(px + .5, py) - outline(px - .5, py),
                             ny = outline(px, py + .5) - outline(px, py - .5);
                const double toward = nx * lx + ny * ly;
                const double band = 1 - depth_in / rim;
                if (toward > 0)
                    light += .42 * toward * band * band;
                else
                    dark += .3 * -toward * band * band;
            }
            // Lamp: brighter toward the upper left, a soft gloss band, a little shade below.
            const double u = px / w, v = py / h, diagonal = u * .45 + v * .55;
            light += .08 * (1 - diagonal) * (1 - diagonal) +
                     .05 * std::exp(-std::pow((u + v - .62) / .16, 2));
            dark += .045 * diagonal * diagonal * diagonal;
            light = std::min(light, .6);
            dark = std::min(dark, .5);
            // White light composited over dark shade, premultiplied.
            const double alpha = (light + dark * (1 - light)) * cover;
            const double white = light * cover * 255;
            const std::size_t n = (static_cast<std::size_t>(y) * w + x) * 4;
            out[n] = out[n + 1] = out[n + 2] = static_cast<std::byte>(std::lround(white));
            out[n + 3] = static_cast<std::byte>(std::lround(alpha * 255));
        }
    return out;
}
std::string upper(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
gf::ImageId load_image(gf::Window& window, const std::string& name) {
    std::ifstream file(asset_directory() + "/" + name, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Missing card artwork: " + name);
    std::streamsize size = file.tellg();
    file.seekg(0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    gf::ImageLoadResult result = window.load_png(bytes);
    if (!result)
        throw std::runtime_error("Could not load " + name);
    return result.image;
}
} // namespace
Table::Table(gf::StableId id) : Control(std::move(id)) {
    set_focusable(true);
    set_accessible_name("Card table. Arrow keys choose piles, Enter selects or places, H hints, "
                        "Space deals, Z undoes, F1 opens help.");
    next_seed_ =
        static_cast<std::uint32_t>(std::chrono::system_clock::now().time_since_epoch().count());
    load_levels();
    game.deal(Kind::solitaire, deal_seed(Kind::solitaire, 1));
    if (load_cabinet(cabinet_path(), cabinet_)) {
        game = cabinet_.games[cabinet_.active];
        back_ = cabinet_.back;
        reduced_ = cabinet_.reduced;
        sound_ = cabinet_.sound;
        music_ = cabinet_.music;
    }
}
void Table::initialize_control_tree() {
    const char* names[] = {"Solitaire", "Spider",   "FreeCell",    "Hearts",     "New game",
                           "Undo",      "Hint",     "How to play", "Options",    "Close",
                           "Card back", "Motion",   "Sound",       "Music",      "Deal options",
                           "Rules",     "Controls", "About",       "Pass cards", "Top scores",
                           "Save name", "New game"};
    for (int i = 0; i < 22; ++i) {
        buttons_[i] =
            gf::make_control<GameButton>(gf::StableId("action" + std::to_string(i)), names[i]);
        (*std::static_pointer_cast<GameButton>(buttons_[i])).set_skin(ButtonSkin::ivory);
        (*buttons_[i]).set_font({gf::FontRole::content, 14, 700, false});
        (*buttons_[i]).set_accessible_name(names[i]);
        add_child(buttons_[i]);
        subscriptions_.push_back(
            (*buttons_[i])
                .clicked()
                .subscribe(*this,
                           gf::Delegate<gf::ButtonBase&>::bind<Table, &Table::action>(*this)));
        // Game commands live in the PlaySuite capsule; panel buttons appear with their panels.
        (*buttons_[i]).set_visible(false);
    }
    score_name_ = gf::make_control<gf::TextBox>(gf::StableId("score.name"), cabinet_.player_name);
    (*score_name_).set_accessible_name("Your name for the top score table");
    (*score_name_).set_maximum_length(24);
    (*score_name_).set_visible(false);
    add_child(score_name_);
}
void Table::on_attached_to_window() {
    gf::Window& window = *attached_window();
    for (int i = 0; i < 52; ++i) {
        std::string number = i < 10 ? "0" + std::to_string(i) : std::to_string(i);
        faces_[i] = load_image(window, "card_" + number + ".png");
    }
    for (int i = 0; i < 4; ++i)
        backs_[i] = load_image(window, "back_" + std::to_string(i) + ".png");
    shadow_ = load_image(window, "card_shadow.png");
    paint::Image tile = paint::render_carpet_tile(paint::carpet_preset(3), 256);
    std::vector<std::byte> pixels(tile.pixels.size() * 4);
    for (std::size_t i = 0; i < tile.pixels.size(); ++i) {
        pixels[4 * i] = static_cast<std::byte>(tile.pixels[i].b);
        pixels[4 * i + 1] = static_cast<std::byte>(tile.pixels[i].g);
        pixels[4 * i + 2] = static_cast<std::byte>(tile.pixels[i].r);
        pixels[4 * i + 3] = std::byte{255};
    }
    felt_ = window.load_bgra32_premultiplied(256, 256, 1024, pixels).image;
    timer_ = std::make_unique<gf::Timer>(window, std::chrono::milliseconds(8));
    subscriptions_.push_back(
        (*timer_).tick().subscribe(*this, gf::Delegate<>::bind<Table, &Table::tick>(*this)));
    if (effectively_visible())
        music_play(audio_name(game.state.kind), music_);
    persist();
    show_result();
    request_tick();
}
void Table::on_detaching_from_window(gf::Window& window) noexcept {
    try {
        persist();
    } catch (...) {
    }
    if (timer_)
        (*timer_).stop();
    timer_.reset();
    for (gf::ImageId id : faces_)
        if (id.value)
            static_cast<void>(window.remove_image(id));
    for (gf::ImageId id : backs_)
        if (id.value)
            static_cast<void>(window.remove_image(id));
    if (felt_.value)
        static_cast<void>(window.remove_image(felt_));
    if (shadow_.value)
        static_cast<void>(window.remove_image(shadow_));
    if (finish_.value)
        static_cast<void>(window.remove_image(finish_));
    finish_ = {};
    finish_width_ = finish_height_ = 0;
    music_play("", false);
}
void Table::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    double w = bounds.width;
    popup_ = {std::max(10.0, (w - 680) * .5), kTop + 8, std::min(680.0, w - 20),
              std::min(520.0, bounds.height - kTop - 46)};
    set_child_layout(buttons_[9], {popup_.x + popup_.width - 93, popup_.y + 17, 72, 30});
    for (int i = 10; i < 15; ++i)
        set_child_layout(buttons_[i], {popup_.x + 30, popup_.y + 86 + (i - 10) * 66.0, 200, 38});
    for (int i = 15; i < 18; ++i)
        set_child_layout(buttons_[i], {popup_.x + 30 + (i - 15) * 145.0, popup_.y + 73, 133, 32});
    set_child_layout(buttons_[18], {w * .5 - 76, bounds.height * .53, 152, 38});
    set_child_layout(buttons_[19], {w - 134, bounds.height - 64, 110, 28});
    set_child_layout(score_name_, {popup_.x + 30, popup_.y + 125, 245, 35});
    set_child_layout(buttons_[20], {popup_.x + 289, popup_.y + 125, 119, 35});
    set_child_layout(buttons_[21],
                     {popup_.x + popup_.width - 140, popup_.y + popup_.height - 48, 110, 30});
    layout_cards(false);
    refresh_finish();
}
void Table::refresh_finish() {
    gf::Window* window = attached_window();
    if (!window)
        return;
    const double scale = std::max(1.0, (*window).scale());
    const int w = std::max(8, static_cast<int>(std::lround(card_w_ * scale)));
    const int h = std::max(8, static_cast<int>(std::lround(card_w_ * 1.4 * scale)));
    if (finish_.value && w == finish_width_ && h == finish_height_)
        return;
    const std::vector<std::byte> pixels = card_finish(w, h);
    gf::ImageLoadResult result =
        finish_.value ? (*window).replace_bgra32_premultiplied(finish_, w, h, w * 4, pixels, *this)
                      : (*window).load_bgra32_premultiplied(w, h, w * 4, pixels);
    if (result) {
        finish_ = result.image;
        finish_width_ = w;
        finish_height_ = h;
    }
}
void Table::layout_cards(bool animate) {
    gf::Rect bounds = client_rectangle();
    int columns = game.state.kind == Kind::spider ? 10 : game.state.kind == Kind::freecell ? 8 : 7;
    // Cards scale with the window: columns share the width, and two rows plus a fan
    // must fit below the capsule.
    const double margin = std::clamp(bounds.width * .025, 10.0, 30.0);
    const double step = (bounds.width - 2 * margin) / columns;
    const bool spider = game.state.kind == Kind::spider;
    card_w_ = std::min(113.0, step * .86);
    card_w_ =
        std::max(30.0, std::min(card_w_, (bounds.height - kTop - 44) / (spider ? 2.5 : 3.0) / 1.4));
    card_h_ = card_w_ * 1.4;
    spread_ = std::clamp(card_h_ * .25, 12.0, 35.0);
    const double left = margin + (step - card_w_) * .5;
    const double row = kTop + std::max(20.0, card_h_ * .2);
    const double tableau =
        spider ? kTop + std::max(56.0, card_h_ * .5) : row + card_h_ + std::max(16.0, card_h_ * .2);
    for (gf::Rect& r : slots_)
        r = {};
    for (int i = 0; i < columns; ++i)
        slots_[i] = {left + i * step, tableau, card_w_, card_h_};
    // Foundations and free cells line up with the tableau columns beneath them.
    for (int i = 0; i < 4; ++i) {
        slots_[10 + i] = {left + (columns - 4 + i) * step, row, card_w_, card_h_};
        slots_[16 + i] = {left + i * step, row, card_w_, card_h_};
    }
    slots_[14] = {left, row, card_w_, card_h_};
    slots_[15] = {left + step, row, card_w_, card_h_};
    if (spider)
        slots_[14] = {bounds.width - margin - card_w_ * .5, kTop + 6, card_w_ * .42, card_h_ * .42};
    if (game.state.kind == Kind::freecell) {
        slots_[14] = {};
        slots_[15] = {};
    } else
        for (int i = 16; i < 20; ++i)
            slots_[i] = {};
    if (game.state.kind == Kind::spider)
        for (int i = 10; i < 14; ++i)
            slots_[i] = {};
    for (Sprite& s : sprites_) {
        s.start = s.rect;
        s.prior_up = s.card.up;
        s.flipping = false;
        s.moving = false;
        s.delay = 0;
        s.duration = .38;
        s.flip_progress = 1;
        s.visible = false;
    }
    for (int pile = 0; pile < 20; ++pile) {
        const Pile& cards = game.state.piles[pile];
        for (int i = 0; i < static_cast<int>(cards.size()); ++i) {
            Card c = cards[i];
            Sprite& s = sprites_[c.id];
            s.card = c;
            s.pile = pile;
            s.index = i;
            s.target = slots_[pile];
            if (game.state.kind == Kind::hearts) {
                if (pile == 0) {
                    double fan = std::min(card_w_ * .66,
                                          (bounds.width - 100 - card_w_) /
                                              std::max(1, static_cast<int>(cards.size()) - 1));
                    double start = (bounds.width - card_w_ - fan * (cards.size() - 1)) * .5;
                    s.target = {start + i * fan, bounds.height - card_h_ - 63, card_w_, card_h_};
                    s.visible = true;
                }
                if (pile == 10) {
                    int player = (game.state.leader + i) % 4;
                    double dx[] = {0, -card_w_ * .85, 0, card_w_ * .85};
                    double dy[] = {55, 0, -55, 0};
                    s.target = {bounds.width * .5 - card_w_ * .5 + dx[player],
                                bounds.height * .43 - card_h_ * .5 + dy[player], card_w_, card_h_};
                    s.visible = true;
                }
            } else {
                if (pile < columns) {
                    double offset = 0;
                    double available =
                        std::max(40.0, bounds.height - 40 - slots_[pile].y - card_h_);
                    double exposed = 0;
                    for (int j = 0; j < static_cast<int>(cards.size()) - 1; ++j)
                        exposed += cards[j].up ? spread_ : 14;
                    double factor = exposed > available ? available / exposed : 1;
                    for (int j = 0; j < i; ++j)
                        offset += (cards[j].up ? spread_ : 14) * factor;
                    s.target.y += offset;
                    s.visible = true;
                } else if (pile == 14) {
                    s.card.up = false;
                    s.visible = i == static_cast<int>(cards.size()) - 1;
                } else if (pile == 15) {
                    int shown = std::min(game.state.draw_count, static_cast<int>(cards.size()));
                    int first = static_cast<int>(cards.size()) - shown;
                    s.visible = i >= first;
                    s.target.x += std::max(0, i - first) * 18;
                } else if (pile >= 16 && game.state.kind == Kind::freecell)
                    s.visible = true;
                else if (pile >= 10 && pile < 14 && game.state.kind != Kind::spider)
                    s.visible = i == static_cast<int>(cards.size()) - 1;
            }
            if (s.start.width == 0)
                s.start = {bounds.width * .5, 100, card_w_, card_h_};
            s.flipping = animate && !reduced_ && s.prior_up != s.card.up;
            if (dealing_ && animate && !reduced_ && s.visible) {
                s.start = {bounds.width * .5 - card_w_ * .5, 86, card_w_, card_h_};
                s.prior_up = false;
                s.flipping = s.card.up;
                s.delay = (pile < columns ? i * columns + pile : i) * .014;
                s.duration = .44;
                s.rect = s.start;
            }
            s.moving = animate && !reduced_ &&
                       (std::abs(s.start.x - s.target.x) + std::abs(s.start.y - s.target.y) > .1 ||
                        s.flipping);
            s.flip_progress = s.flipping ? 0 : 1;
            if (!animate || reduced_)
                s.rect = s.target;
        }
    }
    (*buttons_[18])
        .set_visible(panel_ == 0 && game.state.kind == Kind::hearts && !game.state.over &&
                     (game.state.passing || game.state.trick_number == 13));
    (*buttons_[18]).set_text(game.state.passing ? "Pass cards" : "Next hand");
    (*buttons_[18]).set_enabled(!game.state.over);
    dealing_ = false;
    animating_ = animate && !reduced_;
    animation_start_ = gf::FrameClock::now();
    if (animating_)
        request_tick();
    invalidate(gf::Dirty::paint);
}
void Table::request_tick() {
    if (timer_ && !(*timer_).enabled())
        (*timer_).start();
}
void Table::reload_preferences() {
    Cabinet preferences;
    if (load_cabinet(cabinet_path(), preferences)) {
        reduced_ = preferences.reduced;
        sound_ = preferences.sound;
        music_ = preferences.music;
    }
}
void Table::activate() {
    reload_preferences();
    music_play(audio_name(game.state.kind), music_);
    request_tick();
    if (attached_window())
        static_cast<void>((*attached_window()).request_focus(shared_from_this()));
}
void Table::tick() {
    if (!effectively_visible()) {
        if (timer_)
            (*timer_).stop();
        return;
    }
    gf::FrameTime now = gf::FrameClock::now();
    if (animating_) {
        double elapsed = std::chrono::duration<double>(now - animation_start_).count();
        bool pending = false;
        for (Sprite& s : sprites_)
            if (s.visible && s.moving) {
                double t = std::clamp((elapsed - s.delay) / s.duration, 0.0, 1.0);
                double ease = t * t * t * (t * (t * 6 - 15) + 10);
                gf::Rect old = s.rect;
                s.rect = {s.start.x + (s.target.x - s.start.x) * ease,
                          s.start.y + (s.target.y - s.start.y) * ease,
                          s.start.width + (s.target.width - s.start.width) * ease,
                          s.start.height + (s.target.height - s.start.height) * ease};
                s.flip_progress = std::clamp((elapsed - s.delay - .10) / .34, 0.0, 1.0);
                bool complete = t >= 1 && (!s.flipping || s.flip_progress >= 1);
                pending = pending || !complete;
                s.moving = !complete;
                invalidate(enlarged(old, 16));
                invalidate(enlarged(s.rect, 16));
            }
        animating_ = pending;
    }
    bool ai = game.state.kind == Kind::hearts && !game.state.passing && !game.state.over &&
              game.state.trick_number < 13 &&
              (game.state.turn != 0 || game.state.piles[10].size() == 4);
    if (ai && panel_ == 0 && now >= ai_due_) {
        if (game.state.piles[10].size() == 4)
            game.advance_trick();
        else {
            int index = game.computer_choice();
            if (index >= 0)
                game.play(index);
        }
        ai_due_ = now + std::chrono::milliseconds(game.state.piles[10].size() == 4 ? 1100 : 650);
        persist();
        show_result();
        layout_cards(true);
        sound_play("place", sound_);
    }
    if (cascading_)
        step_cascade();
    if (!animating_ && !cascading_ && (!ai || panel_ != 0))
        (*timer_).stop();
}
// The classic finish: cards leap from the foundations one at a time, bounce along the
// bottom of the table and leave a fading trail. Any click or key skips to the result.
void Table::start_cascade() {
    launch_queue_.clear();
    bouncers_.clear();
    launched_.fill(false);
    for (int rank = 13; rank >= 1; --rank)
        for (int f = 10; f < 14; ++f)
            for (const Card& c : game.state.piles[f])
                if (c.rank == rank)
                    launch_queue_.push_back(c);
    if (launch_queue_.empty()) // Spider sends completed runs home; replay two decks of runs.
        for (int run = 0; run < 8; ++run)
            for (int rank = 13; rank >= 1; --rank)
                launch_queue_.push_back({rank, run % 4, run * 13 + rank - 1, true});
    cascading_ = true;
    launch_timer_ = 1;
    cascade_last_ = std::chrono::steady_clock::now();
    request_tick();
}
void Table::step_cascade() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double dt =
        std::clamp(std::chrono::duration<double>(now - cascade_last_).count(), 0.0, .05);
    cascade_last_ = now;
    const gf::Rect b = client_rectangle();
    launch_timer_ += dt;
    if (launch_timer_ >= .26 && !launch_queue_.empty()) {
        launch_timer_ = 0;
        const Card c = launch_queue_.front();
        launch_queue_.erase(launch_queue_.begin());
        gf::Point start{b.width * .5 - card_w_ * .5, kTop + 10};
        if (c.id >= 0 && c.id < 104 && sprites_[c.id].visible && sprites_[c.id].pile >= 10 &&
            sprites_[c.id].pile < 14)
            start = {sprites_[c.id].rect.x, sprites_[c.id].rect.y};
        if (c.id >= 0 && c.id < 104)
            launched_[c.id] = true;
        const double speed = 160 + std::fmod(c.id * 73.0 + c.rank * 31.0, 220.0);
        Bouncer bouncer{c, start.x, start.y, (c.id + c.rank) % 2 ? speed : -speed,
                        -(40 + std::fmod(c.id * 47.0, 260.0))};
        bouncer.trail.fill(start);
        bouncers_.push_back(bouncer);
    }
    const double floor = b.height - card_h_ - 34;
    for (Bouncer& bouncer : bouncers_) {
        bouncer.vy += 1900 * dt;
        bouncer.x += bouncer.vx * dt;
        bouncer.y += bouncer.vy * dt;
        if (bouncer.y > floor) {
            bouncer.y = floor;
            bouncer.vy = -bouncer.vy * .74;
            if (std::abs(bouncer.vy) < 150)
                bouncer.vy = -520; // keep hopping, like the original
        }
        bouncer.sample += dt;
        if (bouncer.sample >= .035) {
            bouncer.sample = 0;
            for (std::size_t k = bouncer.trail.size() - 1; k > 0; --k)
                bouncer.trail[k] = bouncer.trail[k - 1];
            bouncer.trail[0] = {bouncer.x, bouncer.y};
        }
    }
    struct OffscreenBouncer {
        gf::Rect b;
        double card_w_;
        bool operator()(const Bouncer& bouncer) const {
            return (bouncer.x > b.width + 10 && bouncer.trail.back().x > b.width + 10) ||
                   (bouncer.x < -card_w_ - 10 && bouncer.trail.back().x < -card_w_ - 10);
        }
    };
    std::erase_if(bouncers_, OffscreenBouncer{b, card_w_});
    invalidate(gf::Dirty::paint);
    if (launch_queue_.empty() && bouncers_.empty())
        finish_cascade();
}
void Table::finish_cascade() {
    if (!cascading_)
        return;
    cascading_ = false;
    launch_queue_.clear();
    bouncers_.clear();
    launched_.fill(false);
    invalidate(gf::Dirty::paint);
    show_result();
}
void Table::draw_text(gf::Painter& p, double x, double y, const std::string& text, double size,
                      gf::Color color) {
    p.draw_text_utf8(
        {x, y}, text,
        {gf::FontRole::content, size, static_cast<std::uint16_t>(size >= 15 ? 700 : 600), false,
         size <= 12 && text.find_first_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos ? .6
                                                                                             : 0},
        color);
}
void Table::draw_card(gf::Painter& p, const Sprite& s, bool selected) {
    gf::Rect r = s.rect;
    if (dragging_ && s.pile == selection_ && s.index >= selected_index_) {
        r.x += pointer_.x - press_.x;
        r.y += pointer_.y - press_.y;
    }
    bool face_up = s.card.up;
    if (animating_ && s.flipping && s.flip_progress < 1) {
        double turn = s.flip_progress;
        double width = r.width * std::max(.018, std::abs(std::cos(turn * 3.141592653589793)));
        r.x += (r.width - width) * .5;
        r.width = width;
        face_up = turn < .5 ? s.prior_up : s.card.up;
    }
    double shadow_scale = r.width / 360.0;
    // A card in the hand rides higher: its shadow falls further and softer.
    const bool lifted = dragging_ && s.pile == selection_ && s.index >= selected_index_;
    const double drop = lifted ? r.width * .07 : 0, grow = lifted ? 1.06 : 1;
    p.draw_image(shadow_,
                 {r.x - 40 * shadow_scale * grow + drop * .6, r.y - 40 * shadow_scale * grow + drop,
                  440 * shadow_scale * grow, 584 * shadow_scale * grow});
    if (selected) {
        p.fill_rounded_rect(enlarged(r, 4), 9, gf::Color::rgba(248, 220, 119));
        p.draw_box_shadow(r, 7, {0, 0}, 9, 3, gf::Color::rgba(255, 220, 103, 100));
    }
    p.draw_image(face_up ? faces_[s.card.suit * 13 + s.card.rank - 1] : backs_[back_], r);
    if (finish_.value)
        p.draw_image(finish_, r);
    if (game.state.kind == Kind::hearts && s.pile == 0 && !game.state.passing &&
        !game.legal_heart(0, s.index)) {
        p.fill_rounded_rect(r, 6, gf::Color::rgba(24, 67, 50, 70));
    }
}
void Table::on_paint(gf::Painter& p, gf::Rect) {
    gf::Rect b = client_rectangle();
    p.fill_rect(b, gf::Color::rgba(22, 75, 54));
    if (felt_.value)
        p.fill_image_pattern(felt_, {256, 256}, b, {256, 256});
    std::array<gf::GradientStop, 3> light{{{0, gf::Color::rgba(122, 168, 110, 50)},
                                           {.7, gf::Color::rgba(0, 26, 20, 25)},
                                           {1, gf::Color::rgba(0, 16, 13, 130)}}};
    p.fill_radial_gradient(b, {b.width * .47, b.height * .38}, {b.width * .8, b.height * .85},
                           light);
    gf::Color cream = gf::Color::rgba(223, 229, 204);
    gf::Color muted = gf::Color::rgba(156, 189, 160);
    if (game.state.kind != Kind::hearts) {
        int columns = game.state.kind == Kind::spider     ? 10
                      : game.state.kind == Kind::freecell ? 8
                                                          : 7;
        for (int i = 0; i < 20; ++i) {
            bool show = i < columns || (i >= 10 && i < 14 && game.state.kind != Kind::spider) ||
                        (i >= 16 && game.state.kind == Kind::freecell) ||
                        (i == 14 && game.state.kind != Kind::freecell);
            if (show) {
                p.fill_rounded_rect(slots_[i], 7, gf::Color::rgba(0, 25, 18, 32));
                p.stroke_rounded_rect(slots_[i], 7, gf::Color::rgba(185, 209, 170, 60), 1.5);
                if (i >= 10 && i < 14)
                    draw_text(p, slots_[i].x + card_w_ * .40, slots_[i].y + card_h_ * .38, "A", 29,
                              muted);
            }
        }
        if (game.state.kind == Kind::freecell) {
            draw_text(p, slots_[16].x, slots_[16].y - 7, "FREE CELLS  ·  " + upper(level_name()),
                      11, cream);
            draw_text(p, slots_[10].x, slots_[10].y - 7, "FOUNDATIONS", 11, cream);
        }
        if (game.state.kind == Kind::solitaire) {
            draw_text(p, slots_[14].x, slots_[14].y - 7,
                      "DRAW " + std::to_string(game.state.draw_count) + "  ·  " +
                          upper(level_name()),
                      11, cream);
            draw_text(p, slots_[10].x, slots_[10].y - 7, "FOUNDATIONS", 11, cream);
            if (game.state.piles[14].empty())
                draw_text(p, slots_[14].x + card_w_ * .2, slots_[14].y + card_h_ * .32, "Redeal",
                          15, cream);
        }
        if (game.state.kind == Kind::spider) {
            draw_text(p, slots_[0].x, kTop + 6,
                      std::to_string(game.state.completed) + " / 8 runs home", 18, cream);
            draw_text(p, slots_[0].x, kTop + 26,
                      std::to_string(game.state.spider_suits) + " suit  ·  " +
                          std::to_string(game.state.piles[14].size() / 10) + " deals left  ·  " +
                          level_name(),
                      13, muted);
        }
    } else {
        double xs[] = {b.width * .5 - 90, 34, b.width * .5 - 90, b.width - 190};
        double ys[] = {b.height - 49, b.height * .38, kTop + 22, b.height * .38};
        for (int i = 0; i < 4; ++i) {
            draw_text(p, xs[i], ys[i], hearts_name(game.state, i), 17, cream);
            if (i > 0) {
                draw_text(p, xs[i], ys[i] + 26,
                          std::to_string(game.state.piles[i].size()) + " cards", 12, muted);
                if (game.state.turn == i && !game.state.passing)
                    p.fill_rounded_rect({xs[i] - 12, ys[i] + 3, 5, 17}, 2,
                                        gf::Color::rgba(240, 211, 132));
            }
        }
        for (int player = 1; player < 4; ++player) {
            int count = static_cast<int>(game.state.piles[player].size());
            double miniature = card_w_ * .36, offset = 7;
            double x = player == 2   ? b.width * .5 - (miniature + offset * (count - 1)) * .5
                       : player == 1 ? 36
                                     : b.width - 36 - miniature - offset * std::max(0, count - 1);
            double y = player == 2 ? kTop + 34 : b.height * .38 + 58;
            for (int i = 0; i < count; ++i)
                p.draw_image(backs_[back_], {x + i * offset, y, miniature, miniature * 1.4});
        }
        if (game.state.passing)
            draw_text(p, b.width * .5 - 180, b.height * .38,
                      "Select 3 cards, then click Pass cards", 18, cream);
        if (game.state.trick_number == 13)
            draw_text(p, b.width * .5 - 145, b.height * .4,
                      game.state.over ? "Match complete · lowest score wins"
                                      : "Hand complete · click Next hand",
                      18, cream);
    }
    for (int pile = 0; pile < 20; ++pile)
        for (const Card& c : game.state.piles[pile]) {
            const Sprite& s = sprites_[c.id];
            if (!s.visible ||
                (cascading_ && c.id < 104 && launched_[static_cast<std::size_t>(c.id)]) ||
                (animating_ && s.moving) ||
                (dragging_ && pile == selection_ && s.index >= selected_index_))
                continue;
            bool selected = pile == selection_ && s.index >= selected_index_;
            if (game.state.kind == Kind::hearts)
                selected = pile == 0 && (std::find(pass_cards_.begin(), pass_cards_.end(),
                                                   s.index) != pass_cards_.end() ||
                                         (selection_ == 0 && s.index == selected_index_));
            draw_card(p, s, selected);
        }
    if (animating_)
        for (const Pile& pile : game.state.piles)
            for (const Card& card : pile) {
                const Sprite& sprite = sprites_[card.id];
                if (sprite.visible && sprite.moving &&
                    !(dragging_ && sprite.pile == selection_ && sprite.index >= selected_index_))
                    draw_card(p, sprite,
                              sprite.pile == selection_ && sprite.index >= selected_index_);
            }
    if (dragging_)
        for (const Card& c : game.state.piles[selection_]) {
            const Sprite& s = sprites_[c.id];
            if (s.index >= selected_index_)
                draw_card(p, s, true);
        }
    if (hover_ >= 0 && selection_ < 0 && slots_[hover_].width > 0)
        p.stroke_rounded_rect(enlarged(slots_[hover_], 4), 8, gf::Color::rgba(239, 220, 151), 2);
    if (hover_ >= 0 && selection_ >= 0 && game.legal({selection_, selected_index_, hover_}))
        p.stroke_rounded_rect(enlarged(slots_[hover_], 5), 9, gf::Color::rgba(246, 228, 155), 3);
    if (cascading_)
        // As each top card leaps away, the next one down shows on its foundation.
        for (int f = 10; f < 14; ++f)
            for (std::vector<Card>::const_reverse_iterator it = game.state.piles[f].rbegin();
                 it != game.state.piles[f].rend(); ++it)
                if ((*it).id < 104 && !launched_[static_cast<std::size_t>((*it).id)]) {
                    if (slots_[f].width > 0)
                        p.draw_image(
                            faces_[static_cast<std::size_t>((*it).suit * 13 + (*it).rank - 1)],
                            slots_[f]);
                    break;
                }
    for (const Bouncer& bouncer : bouncers_) {
        const gf::ImageId face =
            faces_[static_cast<std::size_t>(bouncer.card.suit * 13 + bouncer.card.rank - 1)];
        for (std::size_t k = bouncer.trail.size(); k-- > 1;)
            p.draw_image(face, {bouncer.trail[k].x, bouncer.trail[k].y, card_w_, card_h_},
                         .5 * (1 - static_cast<double>(k) / bouncer.trail.size()));
        p.draw_image(face, {bouncer.x, bouncer.y, card_w_, card_h_});
    }
    if (game.state.over && game.state.kind != Kind::hearts && !cascading_) {
        p.fill_rounded_rect({b.width * .5 - 225, b.height * .5 - 45, 450, 90}, 12,
                            gf::Color::rgba(246, 238, 206));
        draw_text(p, b.width * .5 - 174, b.height * .5 - 22, "Beautifully played!", 30,
                  gf::Color::rgba(35, 83, 61));
    }
    p.fill_rect({0, b.height - 29, b.width, 29}, gf::Color::rgba(16, 43, 33, 230));
    draw_text(p, 20, b.height - 23, game.message, 12, cream);

    if (panel_)
        draw_panel(p);
}
void Table::draw_panel(gf::Painter& p) {
    gf::Rect b = client_rectangle();
    p.fill_rect(b, gf::Color::rgba(0, 19, 15, 155));
    p.draw_box_shadow(popup_, 12, {0, 10}, 28, 0, gf::Color::rgba(0, 0, 0, 130));
    p.fill_rounded_rect(popup_, 12, gf::Color::rgba(255, 251, 236));
    // A felt-green header band with a gold rule, matching the table.
    p.save();
    p.clip_rounded_rect(popup_, 12);
    fill_vertical(p, {popup_.x, popup_.y, popup_.width, 56}, gf::Color::rgba(38, 112, 78),
                  gf::Color::rgba(18, 66, 44));
    p.restore();
    p.draw_line({popup_.x, popup_.y + 56}, {popup_.x + popup_.width, popup_.y + 56},
                gf::Color::rgba(232, 196, 112), 2);
    p.stroke_rounded_rect(popup_, 12, gf::Color::rgba(120, 96, 50), 1);
    gf::Color ink = gf::Color::rgba(39, 61, 48);
    const gf::Color title_ink = gf::Color::rgba(255, 232, 170);
    if (panel_ == 3) {
        draw_text(p, popup_.x + 30, popup_.y + 38, "Top scores", 24, title_ink);
        draw_text(p, popup_.x + 30, popup_.y + 80, score_profile_name(game.state), 17, ink);
        bool pending = pending_score();
        std::string result = game.state.over
                                 ? "Your result: " + std::to_string(final_score(game.state)) + " " +
                                       score_unit(game.state)
                                 : score_unit(game.state);
        draw_text(p, popup_.x + 30, popup_.y + 104, result, 14, ink);
        if (game.state.over && !pending)
            draw_text(p, popup_.x + 30, popup_.y + 142,
                      cabinet_.result_recorded[static_cast<int>(game.state.kind)]
                          ? "Your name is on the board. Beautifully played."
                          : "Game complete. The ten best results are below.",
                      14, ink);
        const std::vector<TopScore>& list = cabinet_.top_scores[score_profile(game.state)];
        double row = popup_.y + 181;
        if (list.empty())
            draw_text(p, popup_.x + 30, row, "Your next finish can be the first name here.", 16,
                      ink);
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            gf::Color color = i < 3 ? gf::Color::rgba(140, 93, 20) : ink;
            draw_text(p, popup_.x + 30, row, std::to_string(i + 1), 16, color);
            draw_text(p, popup_.x + 78, row, list[i].name, 16, color);
            draw_text(p, popup_.x + popup_.width - 120, row, std::to_string(list[i].value), 16,
                      color);
            row += 27;
        }
        return;
    }
    draw_text(p, popup_.x + 30, popup_.y + 38,
              panel_ == 1 ? std::string(game_name(game.state.kind)) + " Help"
                          : "Make yourself at home",
              24, title_ink);
    if (panel_ == 2) {
        const char* backs[] = {"Sapphire clubs", "Ruby diamonds", "Emerald hearts",
                               "Amethyst spades"};
        const std::string values[] = {
            backs[back_], reduced_ ? "Reduced motion" : "Smooth card motion",
            sound_ ? "Soft effects on" : "Effects off", music_ ? "Music on" : "Music off",
            game.state.kind == Kind::spider
                ? std::to_string(game.state.spider_suits) + " suit (starts new deal)"
            : game.state.kind == Kind::solitaire
                ? "Draw " + std::to_string(game.state.draw_count) + " (starts new deal)"
                : "Standard rules"};
        for (int i = 0; i < 5; ++i)
            draw_text(p, popup_.x + 251, popup_.y + 111 + i * 66, values[i], 15, ink);
        return;
    }
    std::string text;
    if (topic_ == 1)
        text = "Click a card, then its destination, or drag a legal run. A gold outline marks your "
               "selection; a gold destination outline confirms a legal move. Double-click a card "
               "to send it to a foundation.\n\nKeyboard: arrow keys choose a pile and card. Enter "
               "selects or places. Space draws. Z undoes. H gives a hint. F1 opens this help; "
               "Escape closes it or clears a selection. Tab visits the buttons.\n\nOptions changes "
               "the card back, motion, sound, music, and deal rules. New game deals fresh cards. "
               "All sound has visible feedback.";
    else if (topic_ == 2)
        text = "Games is a native C++ card cabinet built with GUI.Forms. Card faces and backs are "
               "original, reproducible artwork. The felt comes from Plan Paint's fiber and light "
               "renderer.\n\nThe rules are independent of drawing and sound. Deals use a recorded "
               "seed and a specified shuffle. Animation follows committed moves and stops "
               "requesting frames when idle.\n\nMusic and effects are being made in the dedicated "
               "Opus session. Audio files are loaded locally. There are no accounts, "
               "advertisements, or network play.";
    else if (game.state.kind == Kind::solitaire)
        text =
            "Build each foundation from ace through king in one suit. In the seven tableau "
            "columns, build downward in alternating red and black. Move a correctly ordered "
            "face-up run together. Only a king may fill an empty column.\n\nClick the stock to "
            "turn one card, or three in Draw 3 mode. When the stock is empty, click it to recycle "
            "the waste. There is no redeal limit. Revealing a hidden card happens "
            "automatically.\n\nUndo is unlimited within the last 512 actions. Hints suggest legal "
            "moves; they do not promise that a deal can be won. Clear all 52 cards to win.";
    else if (game.state.kind == Kind::spider)
        text = "Build downward from king to ace. You can place a card on the next higher rank "
               "regardless of suit, but only a descending run of ONE suit can move together. Any "
               "card or valid run may fill an empty column.\n\nA complete king-to-ace run of one "
               "suit moves home automatically. Clear eight runs to win. Click the stock to deal "
               "one card onto each column. Every column must contain a card before "
               "dealing.\n\nOptions selects one, two, or four suits and starts a new deal. One "
               "suit is a relaxed introduction; four suits needs much more planning.";
    else if (game.state.kind == Kind::freecell)
        text = "Build the four foundations upward from ace to king in one suit. Build tableau "
               "columns downward in alternating colors. Any single card may occupy an empty free "
               "cell. Any card may fill an empty tableau column.\n\nYou may move an ordered run "
               "when enough temporary space exists: (empty free cells + 1) times 2 for each usable "
               "empty column. An empty destination does not count as spare storage.\n\nAll 52 "
               "cards are visible from the beginning. Keep free cells open when possible. This "
               "application uses its own seeded deals, not Microsoft deal numbers.";
    else
        text =
            "Avoid penalty cards: every heart is 1 point and the queen of spades is 13. Lowest "
            "score wins when someone reaches 100. Three computer opponents, named for US "
            "presidents, watch what is played and guess at the hands they cannot see. Each hand "
            "one of them is sharp and remembers every card, and one is forgetful.\n\nPass "
            "three cards left, then right, then across; every fourth hand has no pass. The two of "
            "clubs leads the first trick. Follow suit if possible. The highest card of the led "
            "suit wins; aces are high.\n\nHearts may lead only after a heart has been discarded, "
            "unless your hand contains only hearts. No points may be discarded on the first trick "
            "if a non-point alternative exists. Taking all 26 points shoots the moon: each "
            "opponent gets 26 instead.\n\nSelect three cards then click Pass cards. During play, "
            "click a legal card in your hand. After 13 tricks, click Next hand.";
    std::istringstream words(text);
    std::string paragraph;
    double y = popup_.y + 129;
    double max_width = popup_.width - 60;
    while (std::getline(words, paragraph)) {
        if (paragraph.empty()) {
            y += 14;
            continue;
        }
        std::istringstream stream(paragraph);
        std::string word, line;
        while (stream >> word) {
            std::string candidate = line.empty() ? word : line + " " + word;
            if (p.measure_text_utf8(candidate, {gf::FontRole::content, 15, 400, false}).width >
                    max_width &&
                !line.empty()) {
                draw_text(p, popup_.x + 30, y, line, 15, ink);
                y += 23;
                line = word;
            } else
                line = candidate;
        }
        if (!line.empty()) {
            draw_text(p, popup_.x + 30, y, line, 15, ink);
            y += 23;
        }
    }
}
void Table::open_panel(int panel) {
    panel_ = panel;
    pointer_down_ = false;
    dragging_ = false;
    set_pointer_capture(false);
    for (int i = 0; i < 9; ++i)
        (*buttons_[i]).set_enabled(panel == 0);
    for (int i = 9; i < 22; ++i)
        (*buttons_[i])
            .set_visible(i == 9   ? panel != 0
                         : i < 15 ? panel == 2
                         : i < 18 ? panel == 1
                         : i == 18
                             ? panel == 0 && game.state.kind == Kind::hearts && !game.state.over &&
                                   (game.state.passing || game.state.trick_number == 13)
                         : i == 19 ? false
                         : i == 20 ? panel == 3 && pending_score()
                                   : panel == 3 && game.state.over);
    (*score_name_).set_visible(panel == 3 && pending_score());
    if (!panel)
        request_tick();
    if (attached_window())
        static_cast<void>(
            (*attached_window()).request_focus(panel ? buttons_[9] : shared_from_this()));
    invalidate(gf::Dirty::paint);
}
void Table::load_levels() {
    // A fresh install starts each table at a different place.
    for (int k = 0; k < 3; ++k)
        picks_[k] = (next_seed_ >> (k * 5)) % 9973;
    std::ifstream file(cabinet_path().parent_path() / "card-levels.txt");
    int version = 0;
    std::array<int, 3> levels{};
    std::array<std::uint32_t, 3> picks{};
    if (!(file >> version) || version != 1)
        return;
    for (int k = 0; k < 3; ++k)
        if (!(file >> levels[k] >> picks[k]) || levels[k] < 0 || levels[k] > 2)
            return;
    levels_ = levels;
    picks_ = picks;
}
void Table::save_levels() const {
    std::error_code error;
    std::filesystem::create_directories(cabinet_path().parent_path(), error);
    std::ofstream file(cabinet_path().parent_path() / "card-levels.txt");
    file << 1;
    for (int k = 0; k < 3; ++k)
        file << ' ' << levels_[k] << ' ' << picks_[k];
    file << '\n';
}
std::uint32_t Table::deal_seed(Kind kind, int option) {
    if (kind == Kind::hearts)
        return ++next_seed_;
    const int k = static_cast<int>(kind);
    const std::uint32_t seed = graded_seed(kind, normalized_option(kind, option),
                                           static_cast<Difficulty>(levels_[k]), picks_[k]++);
    save_levels();
    return seed;
}
std::string Table::level_name() const {
    const int k = static_cast<int>(game.state.kind);
    return k < 3 ? difficulty_name(static_cast<Difficulty>(levels_[k])) : "";
}
void Table::new_game(Kind kind) {
    int option = kind == Kind::spider ? game.state.spider_suits : game.state.draw_count;
    game.deal(kind, deal_seed(kind, option), option);
    dealing_ = true;
    cabinet_.result_recorded[static_cast<int>(kind)] = false;
    selection_ = -1;
    selected_index_ = -1;
    pass_cards_.clear();
    dragging_ = false;
    pointer_down_ = false;
    set_pointer_capture(false);
    layout_cards(true);
    music_play(audio_name(kind), music_);
    sound_play("deal", sound_);
    persist();
}
bool Table::pending_score() const {
    return !cabinet_.result_recorded[static_cast<int>(game.state.kind)] &&
           qualifies(cabinet_.top_scores, game.state);
}
void Table::show_result() {
    if (cascading_)
        return; // the score card opens when the cards have finished bouncing
    if (game.state.over && !cabinet_.result_recorded[static_cast<int>(game.state.kind)]) {
        (*score_name_).set_text(cabinet_.player_name);
        open_panel(3);
    }
}
void Table::persist() {
    int current = static_cast<int>(game.state.kind);
    cabinet_.active = current;
    cabinet_.games[current].state = game.state;
    cabinet_.games[current].message = game.message;
    cabinet_.started[current] = true;
    cabinet_.back = back_;
    cabinet_.reduced = reduced_;
    cabinet_.sound = sound_;
    cabinet_.music = music_;
    if (!save_cabinet(cabinet_path(), cabinet_))
        game.message = "Your move is safe in memory, but the local save could not be written.";
}
void Table::switch_game(Kind kind) {
    persist();
    cabinet_.games[static_cast<int>(game.state.kind)].history = game.history;
    int next = static_cast<int>(kind);
    if (cabinet_.started[next]) {
        game = cabinet_.games[next];
        selection_ = -1;
        selected_index_ = -1;
        dragging_ = false;
        pointer_down_ = false;
        set_pointer_capture(false);
        pass_cards_.clear();
        layout_cards(false);
        music_play(audio_name(kind), music_);
        request_tick();
    } else
        new_game(kind);
    persist();
    show_result();
}
void Table::show_kind(Kind kind) {
    if (game.state.kind != kind) {
        open_panel(0);
        switch_game(kind);
    }
}
std::vector<GameCommand> Table::commands() const {
    std::vector<GameCommand> list{{"new", "New game", true, false, true},
                                  {"undo", "Undo", !game.history.empty() && panel_ == 0},
                                  {"hint", "Hint", panel_ == 0 && !game.state.over}};
    if (game.state.kind == Kind::solitaire)
        list.push_back({"deal", game.state.draw_count == 1 ? "Draw one" : "Draw three"});
    if (game.state.kind == Kind::spider)
        list.push_back({"deal", game.state.spider_suits == 1   ? "One suit"
                                : game.state.spider_suits == 2 ? "Two suits"
                                                               : "Four suits"});
    if (game.state.kind != Kind::hearts)
        list.push_back({"level", "Next: " + level_name()});
    list.push_back({"options", "Options", true, panel_ == 2});
    list.push_back({"help", "Help", true, panel_ == 1});
    list.push_back({"scores", "Top scores", true, panel_ == 3});
    return list;
}
void Table::run_command(std::string_view id) {
    if (id == "new") {
        open_panel(0);
        action(*buttons_[4]);
    } else if (id == "undo")
        action(*buttons_[5]);
    else if (id == "hint")
        action(*buttons_[6]);
    else if (id == "deal") {
        open_panel(0);
        action(*buttons_[14]);
    } else if (id == "level" && game.state.kind != Kind::hearts) {
        const int k = static_cast<int>(game.state.kind);
        levels_[k] = (levels_[k] + 1) % 3;
        save_levels();
        // An untouched deal is replaced at once; otherwise the level waits for the next deal.
        if (game.history.empty() && !game.state.over) {
            open_panel(0);
            new_game(game.state.kind);
        } else
            game.message = "Your next deal will be " + level_name() + ".";
        invalidate(gf::Dirty::paint);
    } else if (id == "options")
        panel_ == 2 ? open_panel(0) : action(*buttons_[8]);
    else if (id == "help")
        panel_ == 1 ? open_panel(0) : action(*buttons_[7]);
    else if (id == "scores")
        panel_ == 3 ? open_panel(0) : action(*buttons_[19]);
}
void Table::action(gf::ButtonBase& button) {
    int a = std::stoi(std::string(button.stable_id().value().substr(6)));
    if (a < 4) {
        open_panel(0);
        switch_game(static_cast<Kind>(a));
    }
    if (a == 4)
        new_game(game.state.kind);
    if (a == 5 && game.undo()) {
        pass_cards_.clear();
        changed("undo");
    }
    if (a == 6) {
        sound_play("hint", sound_);
        if (game.state.kind == Kind::hearts) {
            if (game.state.passing)
                game.message = "Pass high spades and hearts, and try to shorten a suit.";
            else if (game.state.turn == 0) {
                int index = game.computer_choice();
                if (index >= 0)
                    game.message = "Consider " + card_name(game.state.piles[0][index]) +
                                   "; this is a legal suggestion.";
            }
        } else {
            // Follow a winning line when the solver finds one quickly; otherwise suggest any
            // legal move.
            std::vector<SolverStep> line;
            const SolveReport plan = solve_state(game.state, 60000, &line);
            Move m = plan.solved && !line.empty() && !line.front().draw ? line.front().move
                                                                        : game.hint();
            if (plan.solved && !line.empty() && line.front().draw) {
                selection_ = selected_index_ = -1;
                hover_ = keyboard_slot_ = 14;
                game.message = game.state.kind == Kind::spider
                                   ? "Deal a new row: the winning line goes through the stock."
                                   : "Turn the stock: the winning line goes through it.";
            } else if (m.from >= 0) {
                selection_ = m.from;
                selected_index_ = m.index;
                hover_ = m.to;
                keyboard_slot_ = m.to;
                game.message =
                    plan.solved ? "This move keeps a winning line open."
                    : plan.exhausted
                        ? "No winning line is left from here; undo may help. This move is legal."
                        : "The highlighted card has a legal destination. Choose where to place it.";
            } else
                game.message = plan.exhausted
                                   ? "No winning line is left from here. Try undo or a new deal."
                                   : "No tableau move found. Try the stock, undo, or a new deal.";
        }
        invalidate(gf::Dirty::paint);
    }
    if (a == 7) {
        topic_ = 0;
        open_panel(1);
    }
    if (a == 8)
        open_panel(2);
    if (a == 9)
        open_panel(0);
    if (a == 10)
        back_ = (back_ + 1) % 4;
    if (a == 11) {
        reduced_ = !reduced_;
        layout_cards(false);
    }
    if (a == 12)
        sound_ = !sound_;
    if (a == 13) {
        music_ = !music_;
        music_play(audio_name(game.state.kind), music_);
    }
    if (a == 14) {
        if (game.state.kind == Kind::solitaire) {
            game.state.draw_count = game.state.draw_count == 1 ? 3 : 1;
            new_game(game.state.kind);
        }
        if (game.state.kind == Kind::spider) {
            game.state.spider_suits = game.state.spider_suits == 1   ? 2
                                      : game.state.spider_suits == 2 ? 4
                                                                     : 1;
            new_game(game.state.kind);
        }
    }
    if (a >= 15 && a < 18)
        topic_ = a - 15;
    if (a == 18)
        heart_continue();
    if (a == 19)
        open_panel(3);
    if (a == 20 && pending_score()) {
        std::string name((*score_name_).text());
        if (add_top_score(cabinet_.top_scores, game.state, name)) {
            cabinet_.player_name = name;
            cabinet_.result_recorded[static_cast<int>(game.state.kind)] = true;
            open_panel(3);
        } else {
            game.message = "Enter a name before saving your top score.";
            if (attached_window())
                static_cast<void>((*attached_window()).request_focus(score_name_));
        }
    }
    if (a == 21) {
        new_game(game.state.kind);
        open_panel(0);
    }
    if (panel_ == 0 && attached_window())
        static_cast<void>((*attached_window()).request_focus(shared_from_this()));
    persist();
    invalidate(gf::Dirty::paint);
}
void Table::changed(const char* effect) {
    persist();
    if (game.state.over && game.state.kind != Kind::hearts && !reduced_ && !cascading_ &&
        !cabinet_.result_recorded[static_cast<int>(game.state.kind)])
        start_cascade();
    show_result();
    selection_ = -1;
    selected_index_ = -1;
    dragging_ = false;
    hover_ = -1;
    set_pointer_capture(false);
    layout_cards(true);
    sound_play(game.state.over ? "win" : effect, sound_);
    if (game.state.kind == Kind::hearts) {
        ai_due_ = gf::FrameClock::now() + std::chrono::milliseconds(650);
        request_tick();
    }
}
int Table::hit_card(gf::Point point) const {
    int found = -1;
    for (int pile = 0; pile < 20; ++pile)
        for (const Card& c : game.state.piles[pile]) {
            const Sprite& s = sprites_[c.id];
            if (s.visible && inside_rectangle(s.rect, point))
                found = c.id;
        }
    return found;
}
int Table::hit_slot(gf::Point point) const {
    int card = hit_card(point);
    if (card >= 0)
        return sprites_[card].pile;
    for (int i = 0; i < 20; ++i)
        if (slots_[i].width > 0 && inside_rectangle(slots_[i], point))
            return i;
    return -1;
}
void Table::select_or_move(int pile, int index) {
    if (pile == 15 && index != static_cast<int>(game.state.piles[15].size()) - 1) {
        game.message = "Only the top waste card is available.";
        invalidate(gf::Dirty::paint);
        return;
    }
    if (pile == 14) {
        if (game.draw())
            changed("flip");
        else
            invalidate(gf::Dirty::paint);
        return;
    }
    if (selection_ >= 0 && game.move({selection_, selected_index_, pile})) {
        changed("place");
        return;
    }
    if (pile >= 0 && index >= 0 && index < static_cast<int>(game.state.piles[pile].size()) &&
        game.state.piles[pile][index].up) {
        selection_ = pile;
        selected_index_ = index;
        sound_play("select", sound_);
        game.message =
            "Selected " + card_name(game.state.piles[pile][index]) + ". Choose a destination.";
    } else {
        selection_ = -1;
        selected_index_ = -1;
    }
    invalidate(gf::Dirty::paint);
}
void Table::on_pointer(gf::PointerEvent& e) {
    const gf::Point local_position = point_from_window(e.position);
    if (cascading_ && e.action == gf::PointerAction::down) {
        finish_cascade();
        e.handled = true;
        return;
    }
    if (panel_)
        return;
    if (e.action == gf::PointerAction::down && e.button == gf::PointerButton::primary) {
        if (attached_window())
            static_cast<void>((*attached_window()).request_focus(shared_from_this()));
        int id = hit_card(local_position);
        int pile = hit_slot(local_position);
        int index = id < 0 ? -1 : sprites_[id].index;
        if (game.state.kind == Kind::hearts) {
            if (pile == 0 && index >= 0)
                heart_card(index);
            e.handled = true;
            return;
        }
        if (e.click_count >= 2 && pile >= 0 && index >= 0) {
            for (int to = 10; to < 14; ++to)
                if (game.move({pile, index, to})) {
                    changed("place");
                    e.handled = true;
                    return;
                }
        }
        pending_from_ = pending_index_ = pending_to_ = -1;
        if (pile == 14) {
            select_or_move(pile, index);
            e.handled = true;
            return;
        }
        // A click may finish the previous selection, but a drag always starts
        // with the card under this press. Defer click-to-place until release.
        if (selection_ >= 0 && pile != selection_ &&
            game.legal({selection_, selected_index_, pile})) {
            pending_from_ = selection_;
            pending_index_ = selected_index_;
            pending_to_ = pile;
        }
        selection_ = selected_index_ = -1;
        select_or_move(pile, index);
        if (selection_ >= 0 || pending_from_ >= 0) {
            pointer_down_ = true;
            dragging_ = false;
            press_ = pointer_ = local_position;
            set_pointer_capture(true);
        }
        e.handled = true;
    } else if (e.action == gf::PointerAction::move && pointer_down_) {
        gf::Point previous = pointer_;
        pointer_ = local_position;
        if (selection_ >= 0 && std::hypot(pointer_.x - press_.x, pointer_.y - press_.y) > 4)
            dragging_ = true;
        if (hover_ >= 0)
            invalidate(enlarged(slots_[hover_], 12));
        hover_ = hit_slot(pointer_);
        if (hover_ >= 0)
            invalidate(enlarged(slots_[hover_], 12));
        for (const Sprite& sprite : sprites_) {
            if (sprite.visible && sprite.pile == selection_ && sprite.index >= selected_index_) {
                gf::Rect before = sprite.rect;
                before.x += previous.x - press_.x;
                before.y += previous.y - press_.y;
                gf::Rect after = sprite.rect;
                after.x += pointer_.x - press_.x;
                after.y += pointer_.y - press_.y;
                invalidate(enlarged(before, 12));
                invalidate(enlarged(after, 12));
            }
        }
        e.handled = true;
    } else if (e.action == gf::PointerAction::up && pointer_down_) {
        pointer_down_ = false;
        set_pointer_capture(false);
        if (dragging_) {
            int to = hit_slot(local_position);
            for (Sprite& sprite : sprites_)
                if (sprite.pile == selection_ && sprite.index >= selected_index_) {
                    sprite.rect.x += pointer_.x - press_.x;
                    sprite.rect.y += pointer_.y - press_.y;
                }
            bool moved = game.move({selection_, selected_index_, to});
            dragging_ = false;
            if (moved)
                changed("place");
            else {
                game.message = "That run cannot go there. Your cards are still selected.";
                sound_play("invalid", sound_);
                layout_cards(true);
            }
        } else if (pending_from_ >= 0 && game.move({pending_from_, pending_index_, pending_to_})) {
            changed("place");
        }
        pending_from_ = pending_index_ = pending_to_ = -1;
        e.handled = true;
    }
}
void Table::heart_card(int index) {
    if (game.state.passing) {
        std::vector<int>::iterator found = std::find(pass_cards_.begin(), pass_cards_.end(), index);
        if (found != pass_cards_.end())
            pass_cards_.erase(found);
        else if (pass_cards_.size() < 3)
            pass_cards_.push_back(index);
        game.message = std::to_string(pass_cards_.size()) + " of 3 cards selected to pass.";
        invalidate(gf::Dirty::paint);
    } else if (game.play(index))
        changed("place");
    else
        invalidate(gf::Dirty::paint);
}
void Table::heart_continue() {
    if (game.state.passing) {
        if (game.pass(pass_cards_)) {
            pass_cards_.clear();
            changed("deal");
        } else {
            game.message = "Choose exactly three cards to pass.";
            invalidate(gf::Dirty::paint);
        }
    } else if (game.state.trick_number == 13) {
        game.next_round();
        changed("deal");
    }
}
void Table::on_key_bubble(gf::KeyEvent& e) {
    on_key(e);
}
void Table::on_key_preview(gf::KeyEvent& e) {
    if (e.action == gf::KeyAction::down &&
        (e.physical_key == gf::PhysicalKey::f1 || e.physical_key == gf::PhysicalKey::escape))
        on_key(e);
}
void Table::on_key(gf::KeyEvent& e) {
    if (cascading_ && e.action == gf::KeyAction::down) {
        finish_cascade();
        e.handled = true;
        return;
    }
    if (e.handled || e.action != gf::KeyAction::down)
        return;
    if (e.physical_key == gf::PhysicalKey::escape) {
        open_panel(0);
        selection_ = -1;
        dragging_ = false;
        set_pointer_capture(false);
        e.handled = true;
        return;
    }
    if (e.physical_key == gf::PhysicalKey::f1) {
        open_panel(panel_ ? 0 : 1);
        e.handled = true;
        return;
    }
    if (panel_)
        return;
    if (game.state.kind == Kind::hearts) {
        if (e.physical_key == gf::PhysicalKey::left || e.physical_key == gf::PhysicalKey::right) {
            int count = static_cast<int>(game.state.piles[0].size());
            if (count > 0) {
                keyboard_card_ =
                    (keyboard_card_ + (e.physical_key == gf::PhysicalKey::left ? -1 : 1) + count) %
                    count;
                game.message = "Selected " + card_name(game.state.piles[0][keyboard_card_]) +
                               ". Enter plays or marks it.";
                selection_ = 0;
                selected_index_ = keyboard_card_;
                invalidate(gf::Dirty::paint);
            }
            e.handled = true;
            return;
        }
        if (e.physical_key == gf::PhysicalKey::enter) {
            heart_card(keyboard_card_);
            e.handled = true;
            return;
        }
        if (e.physical_key == gf::PhysicalKey::space) {
            heart_continue();
            e.handled = true;
            return;
        }
    }
    if (e.physical_key == gf::PhysicalKey::z) {
        if (game.undo())
            changed("undo");
    } else if (e.physical_key == gf::PhysicalKey::h)
        action(*buttons_[6]);
    else if (e.physical_key == gf::PhysicalKey::space) {
        if (game.draw())
            changed("flip");
    } else if (e.physical_key == gf::PhysicalKey::left ||
               e.physical_key == gf::PhysicalKey::right) {
        int step = e.physical_key == gf::PhysicalKey::left ? -1 : 1;
        do {
            keyboard_slot_ = (keyboard_slot_ + step + 20) % 20;
        } while (
            slots_[keyboard_slot_].width == 0 ||
            (game.state.kind == Kind::spider && keyboard_slot_ >= 10 && keyboard_slot_ != 14) ||
            (game.state.kind == Kind::solitaire && (keyboard_slot_ == 7 || keyboard_slot_ == 8 ||
                                                    keyboard_slot_ == 9 || keyboard_slot_ >= 16)) ||
            (game.state.kind == Kind::freecell && (keyboard_slot_ == 8 || keyboard_slot_ == 9 ||
                                                   keyboard_slot_ == 14 || keyboard_slot_ == 15)));
        hover_ = keyboard_slot_;
        game.message =
            "Pile " + std::to_string(keyboard_slot_ + 1) + ". Press Enter to select or place.";
        invalidate(gf::Dirty::paint);
    } else if (e.physical_key == gf::PhysicalKey::enter) {
        select_or_move(keyboard_slot_,
                       static_cast<int>(game.state.piles[keyboard_slot_].size()) - 1);
    } else if (e.physical_key == gf::PhysicalKey::up && selection_ >= 0) {
        selected_index_ = std::max(0, selected_index_ - 1);
        invalidate(gf::Dirty::paint);
    } else if (e.physical_key == gf::PhysicalKey::down && selection_ >= 0) {
        selected_index_ = std::min(static_cast<int>(game.state.piles[selection_].size()) - 1,
                                   selected_index_ + 1);
        invalidate(gf::Dirty::paint);
    } else
        return;
    e.handled = true;
}
} // namespace games
