#include "runtime_paths.hpp"
#include "koi_view.hpp"

#include "platform/audio.hpp"
#include "platform/text.hpp"
#include "paint/carpet.hpp"
#include "card_finish.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace kk {

namespace {
const Col kCream = hex(0xFFFCF3), kInk = hex(0x1F1A17), kGold = hex(0xC9A24E), kRed = hex(0xC8382A);
std::string save_path(bool dev) {
    const char* name = dev ? "koikoi-dev-v1.txt" : "koikoi-v1.txt";
    if (const char* dir = std::getenv("GAMES_STATE_DIR")) return (std::filesystem::path(dir) / name).string();
    return (games::state_directory() / name).string();
}
std::uint64_t fnv(const std::string& s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
int group_of(int c) {
    // captures are sorted: brights, animals, ribbons, plains
    return static_cast<int>(koi::info(c).type);
}
}  // namespace

KoiView::KoiView(gf::StableId id, Options opt) : Control(std::move(id)), opt_(opt) {
    cab_front_ = !opt_.hosted;
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    {
        gf::SurfaceMaterial none;
        none.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(0, 0, 0))};
        set_authored_surface_material(none);
    }
    set_accessible_name("Koi-Koi, the hanafuda game. Match cards by month, collect sets, and decide when to stop.");
    slots_.fill(-1);
    for (int c = 0; c < 48; ++c) load_png(asset_dir() + "/cards/hana_" + (c < 10 ? "0" : "") + std::to_string(c) + ".png", art_[static_cast<size_t>(c)]);
    load_png(asset_dir() + "/cards/hana_back.png", art_back_);
    load_png(asset_dir() + "/cards/card_shadow.png", art_shadow_);
    if (!load()) new_match();
    if (const char* sc = std::getenv("KK_SCRIPT"); sc && opt_.dev) {
        std::string all = sc;
        for (size_t pos = 0; pos < all.size();) {
            size_t end = all.find(',', pos);
            if (end == std::string::npos) end = all.size();
            const std::string item = all.substr(pos, end - pos);
            const size_t colon = item.find(':');
            if (colon != std::string::npos) script_.push_back({std::atof(item.c_str()), item.substr(colon + 1)});
            pos = end + 1;
        }
    }
}

void KoiView::on_attached_to_window() {
    audio_start(asset_dir());
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(16));
    subs_.push_back((*timer_).tick().subscribe(*this, gf::Delegate<>::bind<KoiView, &KoiView::tick>(*this)));
    last_ = std::chrono::steady_clock::now();
    (*timer_).start();
}

void KoiView::on_detaching_from_window(gf::Window&) noexcept {
    try { persist(); } catch (...) {}
    if (timer_) (*timer_).stop();
    timer_.reset();

    audio_stop();
}

void KoiView::activate() {
    if (attached_window()) static_cast<void>((*attached_window()).request_focus(shared_from_this()));
}

void KoiView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    W_ = bounds.width;
    H_ = bounds.height;
    bs_ = attached_window() ? (*attached_window()).scale() : 1.0;
    phys_w_ = std::max(1, static_cast<int>(std::lround(W_ * bs_)));
    phys_h_ = std::max(1, static_cast<int>(std::lround(H_ * bs_)));
    frame_.resize(phys_w_, phys_h_);
    // the scoreboard sits down the side in a big window; in a smaller one a bar along the top
    // carries the essentials and the twelve months move to a Score panel
    wide_ = W_ >= 900 && H_ >= 560;
    top_ = wide_ ? 0 : 32;
    panel_x_ = wide_ ? W_ - 236 : W_;
    // big enough to read, small enough that seven field cards sit side by side beside the pile
    ch_ = std::clamp((H_ - top_ - 96) / 4.35, 40.0, 132.0);
    cw_ = std::min(ch_ * 360.0 / 504.0, (panel_x_ - 96) / 9.6);
    ch_ = cw_ * 504.0 / 360.0;
    build_art();
    if (surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        static_cast<void>(surface_->reconfigure(d));
    }
    for (Sprite& s : sp_) s.placed = false;
    layout_buttons();
}

void KoiView::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    if (surface_) p.draw_live_surface(surface_, b);
    else p.fill_rect(b, gf::Color::rgba(30, 90, 60));
}

// ------------------------------------------------------------------ art at the drawn size
void KoiView::build_art() {
    const int bw = static_cast<int>(std::lround(cw_ * bs_)), bh = static_cast<int>(std::lround(ch_ * bs_));
    if (bw == built_w_ && felt_.w == phys_w_ && felt_.h == phys_h_) return;
    built_w_ = bw;
    const int sw = static_cast<int>(std::lround(cw_ * small_k_ * bs_)), shh = static_cast<int>(std::lround(ch_ * small_k_ * bs_));
    for (int c = 0; c < 48; ++c) {
        if (art_[static_cast<size_t>(c)].w == 0) continue;
        reduce(art_[static_cast<size_t>(c)], bw, bh, big_[static_cast<size_t>(c)]);
        reduce(art_[static_cast<size_t>(c)], sw, shh, small_[static_cast<size_t>(c)]);
    }
    if (art_back_.w) { reduce(art_back_, bw, bh, big_back_); reduce(art_back_, sw, shh, small_back_); }
    // the card table's finish (texture, rolled edge, lamp) and its card shadow, at both sizes
    auto finish = [](int w, int h, Canvas& out) {
        const std::vector<std::byte> px = games::card_finish(w, h);
        out.resize(w, h);
        std::memcpy(out.px.data(), px.data(), px.size());
    };
    finish(bw, bh, finish_big_);
    finish(sw, shh, finish_small_);
    if (art_shadow_.w) {
        reduce(art_shadow_, static_cast<int>(std::lround(bw * 440.0 / 360)), static_cast<int>(std::lround(bw * 584.0 / 360)), shadow_big_);
        reduce(art_shadow_, static_cast<int>(std::lround(sw * 440.0 / 360)), static_cast<int>(std::lround(sw * 584.0 / 360)), shadow_small_);
    }
    // the felt: the card table's own carpet (vendor/paint, preset 3, a 256-point tile), on the
    // table's base green, under its radial light, exactly as src/table.cpp lays it
    if (carpet_.w == 0) {
        // woven once, before the first frame, as the card table does (src/table.cpp, on attach)
        const paint::Image tile = paint::render_carpet_tile(paint::carpet_preset(3), 256);
        carpet_.resize(tile.width, tile.height);
        for (size_t i = 0; i < tile.pixels.size(); ++i) {
            carpet_.px[i * 4] = tile.pixels[i].b;
            carpet_.px[i * 4 + 1] = tile.pixels[i].g;
            carpet_.px[i * 4 + 2] = tile.pixels[i].r;
            carpet_.px[i * 4 + 3] = 255;
        }
    }
    felt_.resize(phys_w_, phys_h_);
    felt_.clear(rgb(22, 75, 54));
    if (carpet_.w > 0) {
        // the tile at 256 points: bilinear up to the screen's pixels
        const int tw = static_cast<int>(std::lround(256 * bs_));
        Canvas big;
        big.resize(tw, tw);
        for (int y = 0; y < tw; ++y)
            for (int x = 0; x < tw; ++x) {
                const double sx = (x + .5) * carpet_.w / tw - .5, sy = (y + .5) * carpet_.h / tw - .5;
                const int x0 = static_cast<int>(std::floor(sx)), y0 = static_cast<int>(std::floor(sy));
                const double fx = sx - x0, fy = sy - y0;
                auto at = [&](int xx, int yy, int c) { xx = (xx % carpet_.w + carpet_.w) % carpet_.w; yy = (yy % carpet_.h + carpet_.h) % carpet_.h; return carpet_.px[(static_cast<size_t>(yy) * carpet_.w + xx) * 4 + c]; };
                for (int c = 0; c < 4; ++c)
                    big.px[(static_cast<size_t>(y) * tw + x) * 4 + c] = static_cast<std::uint8_t>(std::lround(
                        at(x0, y0, c) * (1 - fx) * (1 - fy) + at(x0 + 1, y0, c) * fx * (1 - fy) + at(x0, y0 + 1, c) * (1 - fx) * fy + at(x0 + 1, y0 + 1, c) * fx * fy));
            }
        for (int y = 0; y < phys_h_; y += tw)
            for (int x = 0; x < phys_w_; x += tw) felt_.draw_canvas(big, x, y);
    }
    {
        // the light: centred a little left of and above the middle, an ellipse 0.8 x 0.85 of the table
        const double cx = phys_w_ * .47, cy = phys_h_ * .38, rx = phys_w_ * .8, ry = phys_h_ * .85;
        felt_.save();
        felt_.translate(cx, cy);
        felt_.scale(1, ry / rx);
        felt_.begin();
        felt_.rect(-cx, -cy * rx / ry, phys_w_, phys_h_ * rx / ry);
        felt_.fill(Paint::rad(0, 0, rx, {{0, rgb(122, 168, 110, 50 / 255.f)}, {.7f, rgb(0, 26, 20, 25 / 255.f)}, {1, rgb(0, 16, 13, 130 / 255.f)}}));
        felt_.restore();
    }
}

// ------------------------------------------------------------------ the game
void KoiView::new_match() {
    const std::uint32_t seed = static_cast<std::uint32_t>(wall_clock() * 1000) ^ 0x4A1Bu;
    koi::new_match(s_, seed, next_opponent_);
    next_opponent_ = (next_opponent_ + 1) % static_cast<int>(koi::opponents().size());
    slots_.fill(-1);
    holds_.clear();
    banners_.clear();
    hint_ = -1;
    for (Sprite& sp : sp_) sp.placed = false;
    wait_ = 1.0;
    play("kk_shuffle", .8f);
    banners_.push_back({std::string("A match against ") + koi::opponents()[static_cast<size_t>(s_.opponent)].name, 0, 2.4});
    dirty_ = true;
    layout_buttons();
}

void KoiView::announce_sets(int player, int before) {
    const auto ys = koi::yaku(s_.captured[static_cast<size_t>(player)]);
    const int now = koi::yaku_points(s_.captured[static_cast<size_t>(player)]);
    if (now <= before) return;
    std::string best;
    int bp = -1;
    for (const auto& y : ys) if (y.points > bp) { bp = y.points; best = y.name; }
    banners_.push_back({best + "!", 0, 2.0});
    play("kk_set", .8f);
}

void KoiView::after_move(int card, const std::vector<int>& matched, bool from_deck) {
    // the card lands on its match (or the field) for a moment, before both go to the captures
    if (!matched.empty()) {
        double x, y;
        int slot = -1;
        for (int k = 0; k < static_cast<int>(slots_.size()); ++k) if (slots_[static_cast<size_t>(k)] == matched[0]) slot = k;
        if (slot >= 0) {
            slot_pos(slot, x, y);
            holds_.push_back({card, x + cw_ * .18, y - ch_ * .1, 1, true, t_ + .45});
            for (int m : matched) {
                for (int k = 0; k < static_cast<int>(slots_.size()); ++k)
                    if (slots_[static_cast<size_t>(k)] == m) { double a, b; slot_pos(k, a, b); holds_.push_back({m, a, b, 1, true, t_ + .45}); }
            }
        }
        play("kk_take", .7f, .95f + .1f * static_cast<float>((card * 7) % 10) / 10.f);
    } else {
        play("kk_place", .7f, .95f + .1f * static_cast<float>((card * 3) % 10) / 10.f);
    }
    static_cast<void>(from_deck);
}

void KoiView::player_play(int index) {
    if (s_.phase != koi::Phase::play || s_.turn != 0 || wait_ > 0) return;
    if (index < 0 || index >= static_cast<int>(s_.hand[0].size())) return;
    const int c = s_.hand[0][static_cast<size_t>(index)];
    const auto m = koi::matches(s_, c);
    const int before = koi::yaku_points(s_.captured[0]);
    koi::play(s_, index);
    hint_ = -1;
    if (s_.phase != koi::Phase::choose_hand) after_move(c, m, false);
    else play("kk_place", .5f);
    announce_sets(0, before);
    wait_ = .55;
    dirty_ = true;
    layout_buttons();
}

void KoiView::player_choose(int field_card) {
    if ((s_.phase != koi::Phase::choose_hand && s_.phase != koi::Phase::choose_draw) || s_.turn != 0) return;
    const int c = s_.pending;
    const int before = koi::yaku_points(s_.captured[0]);
    if (!koi::choose(s_, field_card)) return;
    after_move(c, {field_card}, false);
    announce_sets(0, before);
    wait_ = .55;
    dirty_ = true;
    layout_buttons();
}

void KoiView::player_decide(bool koikoi) {
    if (s_.phase != koi::Phase::decide || s_.turn != 0) return;
    koi::decide(s_, koikoi);
    banners_.push_back({koikoi ? "Koi-koi!" : "Stop!", 0, 1.6});
    play(koikoi ? "kk_koikoi" : "kk_stop", .9f);
    if (s_.phase == koi::Phase::round_over) play(s_.round_winner == 0 ? "kk_win" : "kk_lose", .8f);
    wait_ = .8;
    dirty_ = true;
    layout_buttons();
}

void KoiView::step(double dt) {
    wait_ = std::max(0.0, wait_ - dt);
    if (wait_ > 0) return;
    using koi::Phase;
    // the draw is automatic for both players: the top card turns over and finds its match
    if (s_.phase == Phase::draw) {
        if (s_.deck.empty()) { koi::draw(s_); return; }
        const int d = s_.deck.back();
        double x, y;
        deck_pos(x, y);
        const auto m = koi::matches(s_, d);
        const int before = koi::yaku_points(s_.captured[static_cast<size_t>(s_.turn)]);
        holds_.push_back({d, x + cw_ * 1.15, y, 1, true, t_ + .55});
        play("kk_flip", .7f);
        koi::draw(s_);
        if (s_.phase != Phase::choose_draw) {
            // land it on its match after the flip
            if (!m.empty()) {
                for (int k = 0; k < static_cast<int>(slots_.size()); ++k)
                    if (slots_[static_cast<size_t>(k)] == m[0]) { double a, b; slot_pos(k, a, b); holds_.push_back({d, a + cw_ * .18, b - ch_ * .1, 1, true, t_ + 1.0}); holds_.push_back({m[0], a, b, 1, true, t_ + 1.0}); }
                play("kk_take", .6f);
            }
        }
        announce_sets(s_.turn, before);
        wait_ = 1.05;
        if (s_.phase == Phase::round_over) play(s_.round_winner == 0 ? "kk_win" : s_.round_winner == 1 ? "kk_lose" : "kk_stop", .8f);
        layout_buttons();
        dirty_ = true;
        return;
    }
    if (s_.turn == 1 || auto_) {
        // the computer's move (or yours, when the dev script plays for you)
        if (s_.phase == Phase::play && (s_.turn == 1 || auto_)) {
            const int i = koi::ai_play(s_);
            const int c = s_.hand[static_cast<size_t>(s_.turn)][static_cast<size_t>(i)];
            const auto m = koi::matches(s_, c);
            const int before = koi::yaku_points(s_.captured[static_cast<size_t>(s_.turn)]);
            const int who = s_.turn;
            koi::play(s_, i);
            if (s_.phase != Phase::choose_hand) after_move(c, m, false);
            else play("kk_place", .5f);
            announce_sets(who, before);
            wait_ = .7;
        } else if ((s_.phase == Phase::choose_hand || s_.phase == Phase::choose_draw) && (s_.turn == 1 || auto_)) {
            const int f = koi::ai_choose(s_);
            const int c = s_.pending;
            const int who = s_.turn;
            const int before = koi::yaku_points(s_.captured[static_cast<size_t>(who)]);
            koi::choose(s_, f);
            after_move(c, {f}, false);
            announce_sets(who, before);
            wait_ = .7;
        } else if (s_.phase == Phase::decide && (s_.turn == 1 || auto_)) {
            const bool k = koi::ai_koikoi(s_);
            const std::string nm = s_.turn == 1 ? koi::opponents()[static_cast<size_t>(s_.opponent)].name : "You";
            koi::decide(s_, k);
            banners_.push_back({k ? nm + ": \"Koi-koi!\"" : nm + " stops.", 0, 2.0});
            play(k ? "kk_koikoi" : "kk_stop", .9f);
            if (s_.phase == Phase::round_over) play(s_.round_winner == 0 ? "kk_win" : "kk_lose", .8f);
            wait_ = 1.4;
        } else if (s_.phase == Phase::round_over && auto_) {
            koi::next_round(s_);
            play("kk_shuffle", .7f);
            wait_ = 1.0;
        } else if (s_.phase == Phase::match_over && auto_) {
            new_match();
        }
        layout_buttons();
        dirty_ = true;
    }
}

// ------------------------------------------------------------------ saving
void KoiView::persist() {
    std::string body = koi::save(s_);
    body += "next_opponent=" + std::to_string(next_opponent_) + "\nsound=" + (sound_ ? "1" : "0") + "\nmusic=" + (music_ ? "1" : "0") + "\n";
    const std::string all = "KOIKOI1\n" + body + "check=" + std::to_string(fnv(body)) + "\n";
    const std::string p = save_path(opt_.dev);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(p).parent_path(), ec);
    {
        std::ofstream f(p + ".tmp", std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << all;
    }
    std::filesystem::rename(p + ".tmp", p, ec);
    dirty_ = false;
}

bool KoiView::load() {
    std::ifstream f(save_path(opt_.dev), std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string all = ss.str();
    if (all.rfind("KOIKOI1\n", 0) != 0) return false;
    const size_t ck = all.rfind("check=");
    if (ck == std::string::npos || ck < 8) return false;
    const std::string body = all.substr(8, ck - 8);
    if (std::to_string(fnv(body)) + "\n" != all.substr(ck + 6)) return false;
    koi::KoiState s;
    if (!koi::load(body, s)) return false;
    s_ = s;
    std::istringstream in(body);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("next_opponent=", 0) == 0) next_opponent_ = std::atoi(line.c_str() + 14) % static_cast<int>(koi::opponents().size());
        else if (line == "sound=0") sound_ = false;
        else if (line == "music=0") music_ = false;
    }
    wait_ = .6;
    return true;
}

void KoiView::play(const std::string& name, float gain, float rate) {
    audio_sfx(name, gain, rate, (opt_.hosted || sound_) && cab_sound_ && cab_front_ && visible());
}

// ------------------------------------------------------------------ geometry
void KoiView::deck_pos(double& x, double& y) const {
    x = 24;
    y = mid() - ch_ / 2;
}

void KoiView::slot_pos(int slot, double& x, double& y) const {
    // two rows of seven beside the draw pile (a rare overflow tucks in half a card lower)
    const int col = slot % 7, row = slot / 7;
    const double x0 = 24 + cw_ * 2.35, avail = panel_x_ - 16 - x0;
    const double step = std::min(cw_ * 1.14, (avail - cw_) / 6);
    x = x0 + col * step + (row == 2 ? step * .5 : 0);
    y = mid() - ch_ - 5 + std::min(row, 1) * (ch_ + 10) + (row == 2 ? ch_ * .5 : 0);
}

void KoiView::hand_pos(int player, int i, int n, double& x, double& y) const {
    const double avail = (panel_x_ - 30) * .56;
    const double step = n > 1 ? std::min(cw_ * 1.06, (avail - cw_) / (n - 1)) : 0;
    x = 24 + i * step;
    y = player == 0 ? H_ - ch_ - 18 : top_ + 14;
}

void KoiView::capture_pos(int player, int card, double& x, double& y) const {
    // to the right of the hand: brights, animals and ribbons in a row, the plains below them
    const auto& cap = s_.captured[static_cast<size_t>(player)];
    const double x0 = 24 + (panel_x_ - 30) * .56 + 22, x1 = panel_x_ - 14;
    const double sw = cw_ * small_k_, shh = ch_ * small_k_;
    const double y0 = player == 0 ? H_ - ch_ - 18 : top_ + 14;
    const int g = group_of(card);
    int idx = 0, n = 0;
    for (int c : cap) {
        if (group_of(c) != g) continue;
        if (c == card) idx = n;
        ++n;
    }
    if (g < 3) {
        const double gw = (x1 - x0) / 3;
        const double step = n > 1 ? std::min(sw * .55, (gw - sw - 6) / (n - 1)) : 0;
        x = x0 + g * gw + idx * step;
        y = y0;
    } else {
        const double step = n > 1 ? std::min(sw * .45, (x1 - x0 - sw) / (n - 1)) : 0;
        x = x0 + idx * step;
        y = y0 + shh + 14;
    }
}

void KoiView::sync_slots() {
    for (int& sl : slots_)
        if (sl >= 0 && std::find(s_.field.begin(), s_.field.end(), sl) == s_.field.end()) sl = -1;
    for (int c : s_.field) {
        if (std::find(slots_.begin(), slots_.end(), c) != slots_.end()) continue;
        for (int& sl : slots_) if (sl < 0) { sl = c; break; }
    }
}

void KoiView::place_targets() {
    sync_slots();
    double dx, dy;
    deck_pos(dx, dy);
    for (size_t k = 0; k < s_.deck.size(); ++k) {
        Sprite& p = sp_[static_cast<size_t>(s_.deck[k])];
        p.tx = dx + k * .25;
        p.ty = dy - k * .35;
        p.ts = 1;
        p.tface = 0;
        p.z = static_cast<int>(k);
    }
    for (int pl = 0; pl < 2; ++pl) {
        const auto& h = s_.hand[static_cast<size_t>(pl)];
        for (size_t i = 0; i < h.size(); ++i) {
            Sprite& p = sp_[static_cast<size_t>(h[i])];
            hand_pos(pl, static_cast<int>(i), static_cast<int>(h.size()), p.tx, p.ty);
            p.ts = 1;
            p.tface = pl == 0 ? 1 : 0;
            p.z = 200 + static_cast<int>(i);
        }
        for (int c : s_.captured[static_cast<size_t>(pl)]) {
            Sprite& p = sp_[static_cast<size_t>(c)];
            capture_pos(pl, c, p.tx, p.ty);
            p.ts = small_k_;
            p.tface = 1;
            int idx = 0;
            for (int x : s_.captured[static_cast<size_t>(pl)]) { if (x == c) break; ++idx; }
            p.z = 100 + idx;
        }
    }
    for (int k = 0; k < static_cast<int>(slots_.size()); ++k) {
        const int c = slots_[static_cast<size_t>(k)];
        if (c < 0) continue;
        Sprite& p = sp_[static_cast<size_t>(c)];
        slot_pos(k, p.tx, p.ty);
        p.ts = 1;
        p.tface = 1;
        p.z = 50 + k;
    }
    // a card waiting for its choice hovers beside the draw pile
    if (s_.pending >= 0) {
        Sprite& p = sp_[static_cast<size_t>(s_.pending)];
        p.tx = dx + cw_ * 1.15;
        p.ty = dy;
        p.ts = 1;
        p.tface = 1;
        p.z = 400;
    }
    // held moments (a card on its match before it goes to the captures)
    holds_.erase(std::remove_if(holds_.begin(), holds_.end(), [&](const Hold& h) { return h.until < t_; }), holds_.end());
    for (const Hold& h : holds_) {
        Sprite& p = sp_[static_cast<size_t>(h.card)];
        p.tx = h.x;
        p.ty = h.y;
        p.ts = h.s;
        p.tface = h.face ? 1 : 0;
        p.z = 380;
    }
}

int KoiView::card_at(double x, double y) const {
    int best = -1, bz = -1;
    for (int c = 0; c < 48; ++c) {
        const Sprite& p = sp_[static_cast<size_t>(c)];
        if (!p.placed) continue;
        if (x >= p.x && y >= p.y && x < p.x + cw_ * p.s && y < p.y + ch_ * p.s && p.z > bz) { bz = p.z; best = c; }
    }
    return best;
}

// ------------------------------------------------------------------ the frame
void KoiView::run_script() {
    while (!script_.empty() && script_.front().first <= t_) {
        const std::string c = script_.front().second;
        script_.erase(script_.begin());
        if (c.rfind("size", 0) == 0) {  // dev: size600x370 resizes the window
            int ww = 0, hh = 0;
            if (std::sscanf(c.c_str() + 4, "%dx%d", &ww, &hh) == 2) dev_resize_window(ww, hh);
            continue;
        }
        if (c == "auto") auto_ = true;
        else if (c.rfind("speed", 0) == 0) speed_ = std::atof(c.c_str() + 5);
        else if (c == "rules") { panel_ = Panel::rules; layout_buttons(); }
        else if (c == "sets") { panel_ = Panel::sets; layout_buttons(); }
        else if (c == "score") { panel_ = Panel::score; layout_buttons(); }
        else if (c == "close") { panel_ = Panel::none; layout_buttons(); }
        else if (c == "next") action("next");
        else if (c == "koi") player_decide(true);
        else if (c == "stop") player_decide(false);
        else if (c == "hint") action("hint");
        else if (c == "best") { if (s_.turn == 0 && s_.phase == koi::Phase::play) player_play(koi::ai_play(s_)); else if (s_.turn == 0 && s_.pending >= 0) player_choose(koi::ai_choose(s_)); }
        else if (c[0] == 'p') player_play(std::atoi(c.c_str() + 1));
    }
}

void KoiView::tick() {
    if (!visible() || !cab_front_) { last_ = std::chrono::steady_clock::now(); return; }
    const auto now = std::chrono::steady_clock::now();
    const double rdt = std::clamp(std::chrono::duration<double>(now - last_).count(), 0.0, .1);
    last_ = now;
    const double dt = rdt * speed_;
    t_ += dt;
    run_script();
    if (panel_ == Panel::none) step(dt);
    place_targets();
    // cards glide to where they belong; a turn shows as a flip
    const double k = cab_reduced_ ? 1.0 : 1 - std::exp(-dt * 11);
    for (Sprite& p : sp_) {
        if (!p.placed) { p.x = p.tx; p.y = p.ty; p.s = p.ts; p.face = p.tface; p.placed = true; continue; }
        p.x += (p.tx - p.x) * k;
        p.y += (p.ty - p.y) * k;
        p.s += (p.ts - p.s) * k;
        p.face += (p.tface - p.face) * (cab_reduced_ ? 1.0 : std::min(1.0, dt * 7));
    }
    for (Banner& b : banners_) b.age += dt;
    banners_.erase(std::remove_if(banners_.begin(), banners_.end(), [](const Banner& b) { return b.age > b.life; }), banners_.end());
    audio_music(visible() && cab_front_ ? "kk_music" : "", (opt_.hosted || music_) && cab_music_ && cab_front_);
    audio_tick(rdt);
    save_t_ += rdt;
    if (dirty_ && save_t_ > .5) { save_t_ = 0; persist(); }
    gf::Window* win = attached_window();
    // dev captures (--dev with KK_PRINT_WID) keep drawing even when the window is covered
    static const bool capturing = opt_.dev && std::getenv("KK_PRINT_WID");
    const bool hidden = win && (*win).occluded() && !capturing;
    const bool front = !win || (*win).active();
    const bool shown = visible();
    const auto pace = std::chrono::milliseconds(hidden || !shown || !cab_front_ ? 500 : front ? 16 : 100);
    if (timer_ && (*timer_).interval() != pace) (*timer_).set_interval(pace);

    if (hidden || !shown || frame_.px.empty()) return;
    compose();
    publish();
}

void KoiView::publish() {
    if (!surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        surface_ = gf::LiveSurface::create(d);
        if (surface_ && attached_window()) direct_ = (*attached_window()).queue_live_surface_presentation(shared_from_this(), surface_);
    }
    if (!surface_) return;
    gf::LiveSurfaceWriteLease lease = surface_->try_acquire_write();
    if (lease && static_cast<int>(lease.width()) == phys_w_ && static_cast<int>(lease.height()) == phys_h_) {
        std::span<std::byte> dst = lease.pixels();
        const size_t rb = lease.row_bytes();
        for (int y = 0; y < phys_h_; ++y) std::memcpy(dst.data() + static_cast<size_t>(y) * rb, frame_.px.data() + static_cast<size_t>(y) * phys_w_ * 4, static_cast<size_t>(phys_w_) * 4);
        static_cast<void>(lease.publish());
    }
    if (!direct_) invalidate(gf::Dirty::paint);
    text_cache_trim();
}

// ------------------------------------------------------------------ input
void KoiView::on_pointer(gf::PointerEvent& e) {
    const gf::Point local = point_from_window(e.position);
    mx_ = local.x;
    my_ = local.y;
    auto hit = [&]() -> std::string {
        for (const Button& b : buttons_)
            if (b.enabled && mx_ >= b.x && mx_ < b.x + b.w && my_ >= b.y && my_ < b.y + b.h) return b.id;
        return {};
    };
    if (e.action == gf::PointerAction::move) {
        hover_btn_ = hit();
        const int c = panel_ == Panel::none && hover_btn_.empty() ? card_at(mx_, my_) : -1;
        hover_card_ = c;
        const bool mine = c >= 0 && s_.turn == 0 && ((s_.phase == koi::Phase::play && std::find(s_.hand[0].begin(), s_.hand[0].end(), c) != s_.hand[0].end()) ||
                                                      (s_.pending >= 0 && std::find(s_.choices.begin(), s_.choices.end(), c) != s_.choices.end()));
        set_cursor(!hover_btn_.empty() || mine ? gf::CursorKind::hand : gf::CursorKind::arrow);
    }
    if (e.action == gf::PointerAction::down && e.button == gf::PointerButton::primary) {
        activate();
        const std::string h = hit();
        if (!h.empty()) { pressed_ = h; e.handled = true; return; }
        if (panel_ != Panel::none) { e.handled = true; return; }
        const int c = card_at(mx_, my_);
        if (c >= 0 && s_.turn == 0) {
            if (s_.phase == koi::Phase::play) {
                const auto& h0 = s_.hand[0];
                const auto it = std::find(h0.begin(), h0.end(), c);
                if (it != h0.end()) player_play(static_cast<int>(it - h0.begin()));
            } else if (s_.pending >= 0 && std::find(s_.choices.begin(), s_.choices.end(), c) != s_.choices.end()) {
                player_choose(c);
            }
        }
        e.handled = true;
    } else if (e.action == gf::PointerAction::up) {
        if (!pressed_.empty() && hit() == pressed_) action(pressed_);
        pressed_.clear();
        e.handled = true;
    }
}

void KoiView::on_key(gf::KeyEvent& e) {
    if (e.handled || e.action != gf::KeyAction::down) return;
    using K = gf::PhysicalKey;
    const std::uint32_t k = e.physical_key;
    if (panel_ != Panel::none) { if (k == K::escape || k == K::enter || k == K::space) { panel_ = Panel::none; layout_buttons(); } e.handled = true; return; }
    if (k >= 0x1E && k <= 0x25 && s_.turn == 0 && s_.phase == koi::Phase::play) { player_play(static_cast<int>(k - 0x1E)); e.handled = true; return; }  // 1..8
    if (k == K::h) { action("hint"); e.handled = true; return; }
    if (k == K::k && s_.phase == koi::Phase::decide && s_.turn == 0) { player_decide(true); e.handled = true; return; }
    if (k == K::s && s_.phase == koi::Phase::decide && s_.turn == 0) { player_decide(false); e.handled = true; return; }
    if ((k == K::enter || k == K::space) && (s_.phase == koi::Phase::round_over || s_.phase == koi::Phase::match_over)) { action("next"); e.handled = true; return; }
    if (!opt_.hosted && k == K::m) { action("music"); e.handled = true; return; }
    if (k == K::f1) { panel_ = Panel::rules; layout_buttons(); e.handled = true; return; }
}

void KoiView::action(const std::string& id) {
    play("kk_click", .4f);
    if (id == "rules") panel_ = Panel::rules;
    else if (id == "sets") panel_ = Panel::sets;
    else if (id == "score") panel_ = Panel::score;
    else if (id == "close") panel_ = Panel::none;
    else if (id == "music") music_ = !music_;
    else if (id == "hint") {
        if (s_.turn == 0 && s_.phase == koi::Phase::play) hint_ = s_.hand[0][static_cast<size_t>(koi::ai_play_deep(s_, 60))];
        else if (s_.turn == 0 && s_.pending >= 0) hint_ = koi::ai_choose(s_);
    } else if (id == "koi") player_decide(true);
    else if (id == "stop") player_decide(false);
    else if (id == "next") {
        if (s_.phase == koi::Phase::round_over) { koi::next_round(s_); play("kk_shuffle", .7f); wait_ = .9; }
        else if (s_.phase == koi::Phase::match_over) new_match();
    } else if (id == "newmatch") new_match();
    dirty_ = true;
    layout_buttons();
}

void KoiView::layout_game_buttons() {
    buttons_.clear();
    if (panel_ != Panel::none) {
        // Close sits under the panel, wherever the panel ended up
        const double h = std::min(H_ - 70, panel_ == Panel::score ? 340.0 : 560.0);
        const double y = std::max(10.0, (H_ - 46 - h) / 2);
        buttons_.push_back({"close", "Close", W_ / 2 - 50, y + h + 8, 100, 30});
        return;
    }
    const bool hint_on = s_.turn == 0 && (s_.phase == koi::Phase::play || s_.pending >= 0);
    if (wide_) {
        const double px = panel_x_ + 14, pw = 236 - 28;
        buttons_.push_back({"hint", "Hint", px, H_ - 84, pw / 2 - 4, 28, hint_on});
        buttons_.push_back({"rules", "Rules", px + pw / 2 + 4, H_ - 84, pw / 2 - 4, 28});
        buttons_.push_back({"sets", "The sets", px, H_ - 50, pw / 2 - 4, 28});
        buttons_.push_back({"music", music_ ? "Music: on" : "Music: off", px + pw / 2 + 4, H_ - 50, pw / 2 - 4, 28});
    } else {
        // the top bar's buttons, sized to their words, from the right
        double x = W_ - 8;
        const std::vector<std::pair<std::string, std::string>> items = {{"music", music_ ? "Music" : "Mute"}, {"score", "Score"}, {"sets", "Sets"}, {"rules", "Rules"}, {"hint", "Hint"}};
        for (const auto& [id, label] : items) {
            const double w = text_w(label, 12, 0) + 20;
            x -= w;
            buttons_.push_back({id, label, x, 5, w, 22, id != "hint" || hint_on});
            x -= 6;
        }
    }
    const double cx = panel_x_ / 2, my = mid();
    if (s_.phase == koi::Phase::decide && s_.turn == 0) {
        const double bw = std::min(170.0, (panel_x_ - 40) / 2);
        buttons_.push_back({"stop", (bw < 150 ? "Stop: " : "Stop and score ") + std::to_string(koi::round_value(s_, 0)), cx - bw - 10, my + 52, bw, 36, true, 1});
        buttons_.push_back({"koi", "Koi-koi!", cx + 10, my + 52, bw, 36, true, 2});
    }
    if (s_.phase == koi::Phase::round_over) buttons_.push_back({"next", s_.round + 1 >= 12 ? "See the match" : std::string("On to ") + koi::month_name(s_.round + 1), cx - 90, my + 70, 180, 34, true, 1});
    if (s_.phase == koi::Phase::match_over) buttons_.push_back({"next", "A new match", cx - 90, my + 70, 180, 34, true, 1});
}

// ------------------------------------------------------------------ drawing
void KoiView::text(const std::string& s, double x, double y, Col c, double size, int font, double wrap, int align) {
    if (s.empty()) return;
    const Font f = font == 1 ? Font::speech_bold : font == 2 ? Font::title : font == 3 ? Font::ui : Font::speech;
    const Mask& m = text_mask(s, f, size * bs_, wrap > 0 ? wrap * bs_ : 0);
    double px = x * bs_;
    if (align == 1) px -= m.w / 2.0;
    else if (align == 2) px -= m.w;
    frame_.draw_mask(m, static_cast<int>(std::lround(px)), static_cast<int>(std::lround(y * bs_)), c, 1);
}
double KoiView::text_w(const std::string& s, double size, int font) const {
    const Font f = font == 1 ? Font::speech_bold : font == 2 ? Font::title : font == 3 ? Font::ui : Font::speech;
    return text_mask(s, f, size * bs_, 0).w / bs_;
}
double KoiView::text_h(const std::string& s, double size, int font, double wrap) const {
    const Font f = font == 1 ? Font::speech_bold : font == 2 ? Font::title : font == 3 ? Font::ui : Font::speech;
    return text_mask(s, f, size * bs_, wrap > 0 ? wrap * bs_ : 0).h / bs_;
}
void KoiView::rrect(double x, double y, double w, double h, double r, Col fill, Col line, double lw) {
    frame_.begin();
    frame_.rrect(x * bs_, y * bs_, w * bs_, h * bs_, r * bs_);
    frame_.fill(fill);
    if (lw > 0) {
        frame_.begin();
        frame_.rrect((x + lw / 2) * bs_, (y + lw / 2) * bs_, (w - lw) * bs_, (h - lw) * bs_, r * bs_);
        frame_.stroke(line, lw * bs_);
    }
}

void KoiView::draw_card(int id, const Sprite& p, bool lift, bool glow, bool dim) {
    const bool small = p.s < .8;
    const Canvas& face = small ? small_[static_cast<size_t>(id)] : big_[static_cast<size_t>(id)];
    const Canvas& back = small ? small_back_ : big_back_;
    const double y = p.y - (lift ? ch_ * .12 : 0);
    const double w = cw_ * p.s, h = ch_ * p.s;
    // the card table's soft shadow, falling further when the card is lifted
    {
        const Canvas& sh = small ? shadow_small_ : shadow_big_;
        const double k = (small ? cw_ * small_k_ : cw_) / 360.0, drop = lift ? w * .07 : 0;
        // in the draw pile only the bottom card casts a shadow (the stack's edges show its height)
        const bool buried = !s_.deck.empty() && std::find(s_.deck.begin(), s_.deck.end(), id) != s_.deck.end() && id != s_.deck.front();
        if (sh.w && !buried) frame_.draw_canvas(sh, static_cast<int>(std::lround((p.x - 40 * k + drop * .6) * bs_)), static_cast<int>(std::lround((y - 40 * k + drop) * bs_)));
    }
    // thickness: hanafuda are stout cards; a sliver of their dark core shows along the lower edge
    {
        const double t = std::max(1.1, w * .022);
        rrect(p.x + t * .45, y + t, w - t * .2, h, 7 * p.s + 1, hex(0x3A1210));
        rrect(p.x + t * .2, y + t * .5, w - t * .1, h, 7 * p.s + 1, hex(0x6A2420));
    }
    if (glow) rrect(p.x - 4, y - 4, w + 8, h + 8, 10, hex(0xFFE27A, .55f + .25f * static_cast<float>(std::sin(t_ * 6))));
    // the flip: the card narrows to its edge and opens on the other side
    const double f = p.face;
    const double squeeze = std::fabs(f - .5) * 2;  // 1 flat, 0 edge-on
    const bool show_face = f >= .5;
    const Canvas& stock = show_face ? face : back;
    if (stock.w == 0) return;
    // a card changing size in flight is resized to exactly where it is (only a card or two at a time)
    Canvas sized;
    const int want_w = static_cast<int>(std::lround(w * bs_)), want_h = static_cast<int>(std::lround(h * bs_));
    const bool exact = std::abs(want_w - stock.w) <= 1;
    if (!exact) reduce(show_face ? art_[static_cast<size_t>(id)] : art_back_, want_w, want_h, sized);
    const Canvas& img = exact ? stock : sized;
    if (squeeze > .98) {
        frame_.draw_canvas(img, static_cast<int>(std::lround(p.x * bs_)), static_cast<int>(std::lround(y * bs_)), dim ? .55f : 1.f);
        const Canvas& fin = small ? finish_small_ : finish_big_;
        if (fin.w && exact) frame_.draw_canvas(fin, static_cast<int>(std::lround(p.x * bs_)), static_cast<int>(std::lround(y * bs_)));
    } else {
        Canvas thin;
        reduce(img, std::max(1, static_cast<int>(img.w * squeeze)), img.h, thin);
        frame_.draw_canvas(thin, static_cast<int>(std::lround((p.x + w * (1 - squeeze) / 2) * bs_)), static_cast<int>(std::lround(y * bs_)), 1.f);
    }
}

void KoiView::draw_captures_labels() {
    for (int pl = 0; pl < 2; ++pl) {
        const double x0 = 24 + (panel_x_ - 30) * .56 + 22, x1 = panel_x_ - 14;
        const double y0 = pl == 0 ? H_ - ch_ - 18 : top_ + 14;
        const char* names[4] = {"Brights", "Animals", "Ribbons", "Plains"};
        int n[4] = {};
        for (int c : s_.captured[static_cast<size_t>(pl)]) ++n[group_of(c)];
        const double gw = (x1 - x0) / 3;
        for (int g = 0; g < 4; ++g) {
            const double lx = g < 3 ? x0 + g * gw : x0;
            // brights, animals and ribbons are labelled on the line beneath them; the plains count sits at the row's end
            const double ly = y0 + ch_ * small_k_ + 1;
            const double px = g < 3 ? lx + 2 : x1 - 2;
            const double py = g < 3 ? ly : y0 + ch_ * small_k_ * 2 + 14 - 13;
            text(std::string(names[g]) + " " + std::to_string(n[g]), px, py, hex(0xE8E0C8, .85f), 9.5, 0, 0, g < 3 ? 0 : 2);
        }
    }
}

void KoiView::draw_months(double x, double y, double w) {
    // the twelve months, and how each went
    for (int m = 0; m < 12; ++m) {
        const bool now = m == s_.round && s_.phase != koi::Phase::match_over;
        if (now) rrect(x - 6, y - 2, w + 12, 19, 4, hex(0xFFE9B0, .18f));
        text(std::string(koi::month_name(m)), x + 2, y, now ? hex(0xFFE9B0) : hex(0xC8D4C8), 10.5, now ? 1 : 0);
        text(koi::plant_name(m), x + 82, y, hex(0x8AA090), 9.5, 0);
        if (m < static_cast<int>(s_.rounds_won_points.size())) {
            const int v = s_.rounds_won_points[static_cast<size_t>(m)];
            const std::string r = v > 0 ? "+" + std::to_string(v) : v < 0 ? std::to_string(-v) + " to them" : "draw";
            text(r, x + w - 2, y, v > 0 ? hex(0x9AE0A0) : v < 0 ? hex(0xF0A090) : hex(0xA0A0A0), 10.5, 1, 0, 2);
        }
        y += 19;
    }
}

double KoiView::draw_sets_lines(double x, double y, double w) {
    // sets so far this round
    for (int pl = 0; pl < 2; ++pl) {
        const auto ys = koi::yaku(s_.captured[static_cast<size_t>(pl)]);
        std::string line = pl == 0 ? "Your sets: " : "Their sets: ";
        if (ys.empty()) line += "none yet";
        for (size_t i = 0; i < ys.size(); ++i) line += (i ? ", " : "") + ys[i].name + " " + std::to_string(ys[i].points);
        if (s_.koi[static_cast<size_t>(pl)] > 0) line += "  (koi-koi called)";
        text(line, x, y, pl == 0 ? hex(0xE0F0E0) : hex(0xF0E0D0), 10, 0, w);
        y += text_h(line, 10, 0, w) + 6;
    }
    return y;
}

void KoiView::draw_scoreboard() {
    const double x = panel_x_, w = 236;
    frame_.fill_rect(x * bs_, 0, w * bs_, H_ * bs_, hex(0x0E2A1E, .82f));
    frame_.fill_rect(x * bs_, 0, 2 * bs_, H_ * bs_, kGold);
    double y = 14;
    text("Koi-Koi", x + 16, y, hex(0xFFE9B0), 22, 2);
    y += 34;
    const koi::Opponent& o = koi::opponents()[static_cast<size_t>(s_.opponent)];
    text(o.name, x + 16, y, kCream, 13, 1);
    y += 18;
    text(o.blurb, x + 16, y, hex(0xB8C8B8), 10.5, 0, w - 32);
    y += text_h(o.blurb, 10.5, 0, w - 32) + 12;
    // totals
    rrect(x + 14, y, w - 28, 52, 6, hex(0x000000, .25f), kGold, 1);
    text("You", x + 26, y + 8, kCream, 11, 1);
    text(std::to_string(s_.totals[0]), x + 26, y + 22, hex(0xFFE9B0), 20, 2);
    text("Them", x + w - 26, y + 8, kCream, 11, 1, 0, 2);
    text(std::to_string(s_.totals[1]), x + w - 26, y + 22, hex(0xFFE9B0), 20, 2, 0, 2);
    y += 64;
    draw_months(x + 16, y, w - 34);
    draw_sets_lines(x + 16, y + 12 * 19 + 6, w - 32);
}

void KoiView::draw_topbar() {
    // a slim bar: who you're playing, the score, the month; the buttons sit at its right
    frame_.fill_rect(0, 0, W_ * bs_, top_ * bs_, hex(0x0E2A1E, .9f));
    frame_.fill_rect(0, (top_ - 1) * bs_, W_ * bs_, 1 * bs_, kGold);
    double right = W_;
    for (const Button& b : buttons_) if (b.y < top_) right = std::min(right, b.x);
    const koi::Opponent& o = koi::opponents()[static_cast<size_t>(s_.opponent)];
    const std::string score = "You " + std::to_string(s_.totals[0]) + " - " + std::to_string(s_.totals[1]) + " them";
    const std::string month = s_.phase == koi::Phase::match_over ? "" : std::string(koi::month_name(s_.round));
    double x = 12;
    const double y = (top_ - 15) / 2;
    auto put = [&](const std::string& t, double size, int font, Col c) {
        if (t.empty() || x + text_w(t, size, font) > right - 10) return;
        text(t, x, y, c, size, font);
        x += text_w(t, size, font) + 14;
    };
    put(score, 12.5, 1, hex(0xFFE9B0));
    put(month, 12, 0, kCream);
    put(o.name, 12, 0, hex(0xB8C8B8));
}

void KoiView::draw_dialog() {
    using koi::Phase;
    const double cx = panel_x_ / 2, my = mid();
    const double cw = std::min(420.0, panel_x_ - 24), tw = cw - 40;  // the card and its words fit the table
    auto card = [&](double w, double h) { w = std::min(w, cw); rrect(cx - w / 2, my - h / 2, w, h, 10, hex(0xFFFCF3, .97f), kGold, 2); return my - h / 2; };
    if (s_.phase == Phase::decide && s_.turn == 0) {
        const double y = card(400, 170);
        text("You made a set!", cx, y + 14, kInk, 18, 2, 0, 1);
        std::string sets;
        for (const auto& yk : koi::yaku(s_.captured[0])) sets += (sets.empty() ? "" : ",  ") + yk.name + " " + std::to_string(yk.points);
        text(sets, cx, y + 44, hex(0x5A4A30), 11.5, 0, tw, 1);
        const int v = koi::round_value(s_, 0);
        text("Stop now for " + std::to_string(v) + (v == 1 ? " point" : " points") + ", or call koi-koi and play on for more.", cx, y + 70, kInk, 11, 0, tw, 1);
        if (s_.koi[1] > 0) text("(They called koi-koi: your points are doubled.)", cx, y + 92, hex(0x2A7A3A), 10, 0, tw, 1);
    } else if (s_.phase == Phase::round_over) {
        const double y = card(420, 210);
        std::string head = s_.round_winner < 0 ? std::string(koi::month_name(s_.round)) + ": a draw" :
                           s_.round_winner == 0 ? std::string(koi::month_name(s_.round)) + ": you score " + std::to_string(s_.round_points) :
                           std::string(koi::month_name(s_.round)) + ": " + koi::opponents()[static_cast<size_t>(s_.opponent)].name + " scores " + std::to_string(s_.round_points);
        text(head, cx, y + 16, kInk, 17, 2, tw, 1);
        double ly = y + 50;
        for (const auto& yk : s_.round_yaku) { text(yk.name + "  " + std::to_string(yk.points), cx, ly, hex(0x5A4A30), 12, 0, 0, 1); ly += 18; }
        if (!s_.note.empty()) text(s_.note, cx, ly + 4, hex(0x7A6A50), 10.5, 0, tw, 1);
        text("You " + std::to_string(s_.totals[0]) + "  -  " + std::to_string(s_.totals[1]) + " them", cx, y + 150, kInk, 13, 1, 0, 1);
    } else if (s_.phase == Phase::match_over) {
        const double y = card(420, 200);
        const bool won = s_.totals[0] > s_.totals[1], tied = s_.totals[0] == s_.totals[1];
        text(tied ? "A tied match" : won ? "You win the match!" : std::string(koi::opponents()[static_cast<size_t>(s_.opponent)].name) + " wins the match", cx, y + 22, kInk, 19, 2, tw, 1);
        text(std::to_string(s_.totals[0]) + " to " + std::to_string(s_.totals[1]), cx, y + 62, hex(0x5A4A30), 16, 1, 0, 1);
        text("Twelve months played.", cx, y + 100, hex(0x7A6A50), 11, 0, 0, 1);
    }
}

std::vector<std::string> KoiView::panel_lines() const {
    if (panel_ == Panel::rules)
        return {"The deck has twelve months of four cards, each month a flower. On your turn, play a card from your hand. If a card on the field shares its month, you take both (pick which if there are two; take all four if there are three). Otherwise it joins the field.",
                "Then the top card of the draw pile turns over and is matched the same way.",
                "Collect sets (see The sets). The moment you make a new one, choose: stop and score the round, or call koi-koi and play on for a bigger score. But if your opponent then makes a set and stops, they score, and their points are doubled because you called koi-koi.",
                "A round worth seven points or more is doubled. Four of a month in your starting hand, or four pairs, wins the round outright for six.",
                "A match is twelve rounds, January to December. The winner of a round deals the next and moves first.",
                "Keys: 1-8 play a card, H hint, K koi-koi, S stop, Enter continue, Use the capsule for music and sound."};
    if (panel_ == Panel::sets)
        return {"Five Brights: all five  10.   Four Brights (no Rain Man)  8.   Rainy Four Brights  7.   Three Brights (no Rain Man)  5.",
                "Viewing the Blossoms: Curtain and Sake Cup  5.   Viewing the Moon: Full Moon and Sake Cup  5.",
                "Boar, Deer and Butterflies  5, and 1 for each more animal.   Animals: any five  1, and 1 for each more.",
                "Red Poem Ribbons (pine, plum, cherry)  5.   Blue Ribbons (peony, chrysanthemum, maple)  5.   Both  10.   Each more ribbon adds 1.",
                "Ribbons: any five  1, and 1 for each more.   Plains: any ten  1, and 1 for each more. The Sake Cup counts as a plain too.",
                "Brights: Crane (January), Curtain (March), Full Moon (August), Rain Man (November), Phoenix (December). The label at the foot of each card names its month and kind."};
    return {};
}

void KoiView::draw_panel() {
    if (panel_ == Panel::none) return;
    frame_.fill_rect(0, 0, W_ * bs_, H_ * bs_, hex(0x000000, .55f));
    const double w = std::min(W_ - 24, panel_ == Panel::score ? 520.0 : 640.0);
    const double h = std::min(H_ - 70, panel_ == Panel::score ? 340.0 : 560.0);
    const double x = (W_ - w) / 2, y = std::max(10.0, (H_ - 46 - h) / 2);
    if (panel_ == Panel::score) {
        // the scoreboard, as a panel: the match on the left, the twelve months on the right
        rrect(x, y, w, h, 12, hex(0x0E2A1E, .97f), kGold, 2);
        const double colw = (w - 60) / 2;
        const koi::Opponent& o = koi::opponents()[static_cast<size_t>(s_.opponent)];
        double ly = y + 16;
        text(o.name, x + 20, ly, kCream, 13, 1);
        ly += 18;
        text(o.blurb, x + 20, ly, hex(0xB8C8B8), 10.5, 0, colw);
        ly += text_h(o.blurb, 10.5, 0, colw) + 10;
        rrect(x + 18, ly, colw, 52, 6, hex(0x000000, .25f), kGold, 1);
        text("You", x + 30, ly + 8, kCream, 11, 1);
        text(std::to_string(s_.totals[0]), x + 30, ly + 22, hex(0xFFE9B0), 20, 2);
        text("Them", x + 18 + colw - 12, ly + 8, kCream, 11, 1, 0, 2);
        text(std::to_string(s_.totals[1]), x + 18 + colw - 12, ly + 22, hex(0xFFE9B0), 20, 2, 0, 2);
        draw_sets_lines(x + 20, ly + 64, colw);
        draw_months(x + 40 + colw, y + 18, colw);
        return;
    }
    rrect(x, y, w, h, 12, hex(0xFFFCF3), kGold, 2);
    // the words wrap to the width and step down a size until they fit
    const std::vector<std::string> lines = panel_lines();
    for (double size : {12.0, 11.0, 10.0, 9.0}) {
        panel_size_ = size;
        double need = 52;
        for (const std::string& l : lines) need += text_h(l, size, 0, w - 52) + 7;
        if (need <= h - 10) break;
    }
    text(panel_ == Panel::rules ? "How to play Koi-Koi" : "The sets", x + w / 2, y + 16, kInk, panel_size_ >= 11 ? 20 : 16, 2, 0, 1);
    double ly = y + (panel_size_ >= 11 ? 52 : 42);
    for (size_t i = 0; i < lines.size(); ++i) {
        const bool last = i + 1 == lines.size();
        const double size = last ? panel_size_ - .5 : panel_size_;
        const Col c = last ? (panel_ == Panel::rules ? hex(0x7A6A50) : hex(0x5A4A30)) : kInk;
        text(lines[i], x + 26, ly, c, size, last && panel_ == Panel::rules ? 1 : 0, w - 52);
        ly += text_h(lines[i], size, 0, w - 52) + 7;
    }
}

void KoiView::draw_buttons() {
    for (const Button& b : buttons_) {
        const bool over = hover_btn_ == b.id && b.enabled, down = pressed_ == b.id;
        Col face = !b.enabled ? hex(0x6A7A6A) : over ? hex(0xFFF6DA) : hex(0xF4ECD8);
        Col ink = b.enabled ? kInk : hex(0x3A4A3A);
        if (b.style == 2 && b.enabled) { face = over ? hex(0xE85040) : kRed; ink = hex(0xFFF4E0); }
        rrect(b.x + 1, b.y + 2, b.w, b.h, 6, hex(0x000000, .3f));
        rrect(b.x, b.y + (down ? 1 : 0), b.w, b.h, 6, face, b.style == 2 ? hex(0x7A1A10) : kGold, 1.2);
        text(b.label, b.x + b.w / 2, b.y + b.h / 2 - 8 + (down ? 1 : 0), ink, b.style ? 14 : 12, b.style ? 1 : 0, 0, 1);
    }
}

void KoiView::compose() {
    frame_.px = felt_.px;
    // the draw pile's count, and a quiet line about whose turn it is
    double dx, dy;
    deck_pos(dx, dy);
    if (s_.deck.empty()) rrect(dx, dy, cw_, ch_, 8, hex(0x000000, .15f), hex(0xFFFFFF, .2f), 1);
    // empty field places, faintly
    for (int k = 0; k < static_cast<int>(slots_.size()); ++k) {
        if (slots_[static_cast<size_t>(k)] >= 0) continue;
        double x, y;
        slot_pos(k, x, y);
        if (k < 14) rrect(x, y, cw_, ch_, 8, hex(0x000000, .07f), hex(0xFFFFFF, .07f), 1);
    }
    // the cards, back to front
    std::vector<int> order(48);
    for (int c = 0; c < 48; ++c) order[static_cast<size_t>(c)] = c;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return sp_[static_cast<size_t>(a)].z < sp_[static_cast<size_t>(b)].z; });
    // which field cards the hovered (or pending) card would take
    std::vector<int> lit;
    const bool my_play = s_.turn == 0 && s_.phase == koi::Phase::play;
    if (my_play && hover_card_ >= 0 && std::find(s_.hand[0].begin(), s_.hand[0].end(), hover_card_) != s_.hand[0].end()) lit = koi::matches(s_, hover_card_);
    if (s_.pending >= 0 && s_.turn == 0) lit = s_.choices;
    for (int c : order) {
        const Sprite& p = sp_[static_cast<size_t>(c)];
        const bool in_hand = std::find(s_.hand[0].begin(), s_.hand[0].end(), c) != s_.hand[0].end();
        const bool lift = in_hand && my_play && (c == hover_card_ || c == hint_);
        const bool glow = std::find(lit.begin(), lit.end(), c) != lit.end() || (c == hint_ && hint_ >= 0);
        const bool dim = my_play && in_hand && hover_card_ >= 0 && false;
        draw_card(c, p, lift, glow, dim);
    }
    text(std::to_string(s_.deck.size()) + " left", dx + cw_ / 2, dy + ch_ + 6, hex(0xE8E0C8, .8f), 10.5, 0, 0, 1);
    draw_captures_labels();
    // whose turn
    {
        std::string turn;
        if (s_.phase == koi::Phase::play) turn = s_.turn == 0 ? "Your turn: play a card" : std::string(koi::opponents()[static_cast<size_t>(s_.opponent)].name) + " is thinking...";
        else if ((s_.phase == koi::Phase::choose_hand || s_.phase == koi::Phase::choose_draw) && s_.turn == 0) turn = "Two cards match: take which?";
        if (!turn.empty()) text(turn, 24, H_ - ch_ - 46, hex(0xFFE9B0, .9f), 12.5, 1);
    }
    if (wide_) draw_scoreboard();
    else draw_topbar();
    // banners
    double by = mid() - 30;
    for (const Banner& b : banners_) {
        const float a = static_cast<float>(std::clamp(std::min(b.age * 5, (b.life - b.age) * 3), 0.0, 1.0));
        const double bsz = text_w(b.text, 24, 2) + 40 > panel_x_ - 20 ? 17 : 24;
        const double tw = text_w(b.text, bsz, 2) + 40;
        rrect(panel_x_ / 2 - tw / 2, by - 6, tw, bsz + 20, (bsz + 20) / 2, hex(0x7A1E1E, .85f * a), alpha(kGold, a), 1.5);
        text(b.text, panel_x_ / 2, by, alpha(hex(0xFFF4E0), a), bsz, 2, 0, 1);
        by += 52;
    }
    if (wait_ <= 0 || s_.phase == koi::Phase::decide || s_.phase == koi::Phase::round_over || s_.phase == koi::Phase::match_over) draw_dialog();
    draw_panel();
    draw_buttons();
}

}  // namespace kk

namespace kk {
void KoiView::set_cabinet(bool foreground, bool music, bool sound, bool reduced) {
    cab_front_ = foreground; cab_music_ = music; cab_sound_ = sound; cab_reduced_ = reduced;
    audio_cabinet(foreground, music, sound);
    if (!foreground) { pressed_.clear(); }
}
}

namespace kk {
void KoiView::layout_buttons() {
    layout_game_buttons();
    if (!opt_.hosted) return;
    for (std::size_t i = buttons_.size(); i > 0; --i) {
        const std::string& id = buttons_[i-1].id;
        if (id == "rules" || id == "sets" || id == "score" || id == "music" || id == "hint") buttons_.erase(buttons_.begin() + static_cast<std::ptrdiff_t>(i-1));
    }
}
std::vector<games::GameCommand> KoiView::commands() const { return {{"hint", "Hint", s_.turn == 0 && (s_.phase == koi::Phase::play || s_.pending >= 0), false, true}, {"rules", "Help", true, panel_ == Panel::rules}, {"sets", "Sets", true, panel_ == Panel::sets}, {"score", "Score", true, panel_ == Panel::score}}; }
void KoiView::run_command(std::string_view id) {
    for (const games::GameCommand& cmd : commands()) {
        if (cmd.id == id && cmd.enabled) { action(cmd.checked ? "close" : std::string(id)); return; }
    }
}
}
