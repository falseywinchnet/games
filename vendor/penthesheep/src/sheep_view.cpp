#include "runtime_paths.hpp"
#include "sheep_view.hpp"

#include "platform/audio.hpp"
#include "platform/text.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sh {

namespace {
const Col kInk = hex(0x2A2A20), kCream = hex(0xFFF8E8), kBar = hex(0x3A5A2A), kGold = hex(0xF0C040), kLeaf = hex(0x5A8A3A);

std::filesystem::path save_path(bool dev) {
    const char* name = dev ? "pen_the_sheep-dev-v1.txt" : "pen_the_sheep-v1.txt";
    if (const char* dir = std::getenv("GAMES_STATE_DIR")) return std::filesystem::path(dir) / name;
    return games::state_directory() / name;
}
std::uint64_t fnv(const std::string& s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
const char* kind_line(Smarts s) {
    switch (s) {
        case Smarts::dozy: return "a dozy sheep";
        case Smarts::clever: return "a clever sheep";
        case Smarts::cunning: return "a cunning sheep";
    }
    return "a sheep";
}
}  // namespace

SheepView::SheepView(gf::StableId id, Options opt) : Control(std::move(id)), opt_(opt) {
    cab_front_ = !opt_.hosted;
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    {
        gf::SurfaceMaterial none;
        none.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(0, 0, 0))};
        set_authored_surface_material(none);
    }
    set_accessible_name("Pen the Sheep. Put up fences on the meadow to pen the sheep in before it reaches the edge.");
    const bool resumed = load();
    load_level(level_, resumed);
    if (const char* sc = std::getenv("SH_SCRIPT"); sc && opt_.dev) {
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

SheepView::~SheepView() {
    if (pending_.valid()) pending_.wait();
}

void SheepView::on_attached_to_window() {
    audio_start(asset_dir());
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(33));
    subs_.push_back((*timer_).tick().subscribe(*this, gf::Delegate<>::bind<SheepView, &SheepView::tick>(*this)));
    last_t_ = std::chrono::steady_clock::now();
    (*timer_).start();
}

void SheepView::on_detaching_from_window(gf::Window&) noexcept {
    try { persist(); } catch (...) {}
    if (timer_) (*timer_).stop();
    timer_.reset();

    audio_stop();
}

void SheepView::activate() {
    if (attached_window()) static_cast<void>((*attached_window()).request_focus(shared_from_this()));
}

void SheepView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    pw_ = std::max(64, static_cast<int>(std::ceil(bounds.width / pixel_)));
    ph_ = std::max(64, static_cast<int>(std::ceil(bounds.height / pixel_)));
    frame_.resize(pw_, ph_);
    // the bars slim down in a small window so the meadow keeps its room
    compact_ = ph_ < 300 || pw_ < 420;
    top_h_ = compact_ ? 20 : 30;
    bottom_h_ = compact_ ? 24 : 34;
    btn_h_ = compact_ ? 15 : 18;
    pas_.resize(pw_, ph_, top_h_, bottom_h_);
    pas_.frame(m_.w);
    st_.sheep.pos = pas_.cell_pos(m_.sheep);
    bs_ = attached_window() ? (*attached_window()).scale() : 1.0;
    phys_w_ = std::max(1, static_cast<int>(std::lround(bounds.width * bs_)));
    phys_h_ = std::max(1, static_cast<int>(std::lround(bounds.height * bs_)));
    xmap_.clear();
    if (surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        static_cast<void>(surface_->reconfigure(d));
    }
    layout_buttons();
}

void SheepView::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    if (surface_) p.draw_live_surface(surface_, b);
    else p.fill_rect(b, gf::Color::rgba(124, 192, 96));
}

// ------------------------------------------------------------------ the meadows
void SheepView::load_level(int level, bool resume) {
    level_ = std::max(1, level);
    if (static_cast<int>(stars_.size()) < level_) stars_.resize(static_cast<size_t>(level_), 0);
    if (!resume) moves_.clear();
    phase_ = Phase::loading;
    phase_t_ = 0;
    pending_level_ = level_;
    const LevelParams p = params_for(level_);
    pending_ = std::async(std::launch::async, [p] { return generate(p); });
    layout_buttons();
}

void SheepView::begin_level() {
    lvl_ = pending_.get();
    m_ = lvl_.start;
    undo_.clear();
    hinted_ = false;
    hint_ = -1;
    // a resumed meadow: replay the stones placed so far
    std::vector<int> replay = moves_;
    moves_.clear();
    for (int c : replay) {
        if (m_.escaped || m_.penned()) break;
        undo_.push_back(m_);
        if (!place(m_, c)) { undo_.pop_back(); break; }
        moves_.push_back(c);
    }
    pas_.frame(m_.w);
    st_ = PastureState{};
    st_.meadow = &m_;
    st_.drop.assign(static_cast<size_t>(m_.w * m_.h), 0);
    st_.sheep.pos = pas_.cell_pos(m_.sheep);
    st_.sheep.yaw = 0;
    phase_ = Phase::play;
    phase_t_ = 0;
    if (m_.penned()) finish(true);
    else if (m_.escaped) finish(false);
    else {
        say_ = moves_.empty() ? std::string("Meadow ") + std::to_string(level_) + ": " + kind_line(m_.smarts) + ". Three fences before it moves." : "Back in the meadow.";
        say_t_ = 3.5;
        play("sh_baa_0" + std::to_string(1 + level_ % 4), .6f);
    }
    layout_buttons();
    dirty_ = true;
}

void SheepView::stone(int cell) {
    if (phase_ != Phase::play || !m_.can_place(cell)) { play("sh_nope", .5f); return; }
    undo_.push_back(m_);
    from_cell_ = m_.sheep;
    place(m_, cell, &last_);
    moves_.push_back(cell);
    st_.drop[static_cast<size_t>(cell)] = 1;
    hint_ = -1;
    play("sh_place", .7f, .92f + .16f * static_cast<float>((cell * 37) % 10) / 10.f);
    phase_ = Phase::answer;
    phase_t_ = 0;
    dirty_ = true;
    layout_buttons();
}

void SheepView::undo() {
    if (undo_.empty() || (phase_ != Phase::play && phase_ != Phase::lost && phase_ != Phase::won)) return;
    if (phase_ == Phase::won) return;  // a penned sheep stays penned
    m_ = undo_.back();
    undo_.pop_back();
    if (!moves_.empty()) moves_.pop_back();
    st_.sheep = SheepPose{};
    st_.sheep.pos = pas_.cell_pos(m_.sheep);
    phase_ = Phase::play;
    hint_ = -1;
    play("sh_undo", .5f);
    dirty_ = true;
    layout_buttons();
}

void SheepView::restart() {
    m_ = lvl_.start;
    undo_.clear();
    moves_.clear();
    hint_ = -1;
    st_.sheep = SheepPose{};
    st_.sheep.pos = pas_.cell_pos(m_.sheep);
    std::fill(st_.drop.begin(), st_.drop.end(), 0);
    phase_ = Phase::play;
    play("sh_undo", .5f);
    dirty_ = true;
    layout_buttons();
}

void SheepView::hint() {
    if (phase_ != Phase::play) return;
    hint_ = bot_stone(m_);
    hinted_ = true;
    play("sh_hint", .6f);
}

int SheepView::star_count() const {
    const int used = m_.stones;
    int s = used <= lvl_.par ? 3 : used <= lvl_.par + 3 ? 2 : 1;
    if (hinted_) s = std::min(s, 2);
    return s;
}

void SheepView::finish(bool won) {
    phase_ = won ? Phase::won : Phase::lost;
    phase_t_ = 0;
    if (won) {
        const int s = star_count();
        int& best = stars_[static_cast<size_t>(level_ - 1)];
        best = std::max(best, s);
        reached_ = std::max(reached_, level_ + 1);
        say_ = s == 3 ? "Penned, and under par!" : "Penned!";
        play("sh_penned", .8f);
        play("sh_tune_penned", .7f);
        st_.sheep.sit = 0;
    } else {
        say_ = "It got away!";
        play("sh_escape", .8f);
        play("sh_tune_away", .6f);
    }
    say_t_ = 3;
    dirty_ = true;
    layout_buttons();
}

// ------------------------------------------------------------------ saving
void SheepView::persist() {
    std::ostringstream o;
    o << "level=" << level_ << "\nreached=" << reached_ << "\nstars=";
    for (int s : stars_) o << s;
    o << "\nmoves=";
    for (size_t i = 0; i < moves_.size(); ++i) o << (i ? "," : "") << moves_[i];
    o << "\nsound=" << sound_ << "\nmusic=" << music_ << "\n";
    const std::string body = o.str();
    const std::string all = "PENSHEEP1\n" + body + "check=" + std::to_string(fnv(body)) + "\n";
    const std::filesystem::path p = save_path(opt_.dev);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    {
        std::ofstream f(p.string() + ".tmp", std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << all;
    }
    std::filesystem::rename(p.string() + ".tmp", p, ec);
    dirty_ = false;
}

bool SheepView::load() {
    std::ifstream f(save_path(opt_.dev), std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string all = ss.str();
    if (all.size() > 65536 || all.rfind("PENSHEEP1\n", 0) != 0) return false;
    const size_t ck = all.rfind("check=");
    if (ck == std::string::npos || ck < 10) return false;
    const std::string body = all.substr(10, ck - 10);
    if (std::to_string(fnv(body)) + "\n" != all.substr(ck + 6)) return false;
    try {
        std::istringstream in(body);
        std::string line;
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "level") level_ = std::clamp(std::stoi(v), 1, 100000);
            else if (k == "reached") reached_ = std::clamp(std::stoi(v), 1, 100000);
            else if (k == "stars") { stars_.clear(); for (char c : v) stars_.push_back(std::clamp(c - '0', 0, 3)); }
            else if (k == "moves") { moves_.clear(); std::stringstream ms(v); std::string x; while (std::getline(ms, x, ',')) if (!x.empty()) moves_.push_back(std::stoi(x)); }
            else if (k == "sound") sound_ = v == "1";
            else if (k == "music") music_ = v == "1";
        }
    } catch (...) {
        return false;
    }
    reached_ = std::max(reached_, level_);
    return true;
}

// ------------------------------------------------------------------ the frame
void SheepView::play(const std::string& name, float gain, float rate) {
    audio_sfx(name, gain, rate, (opt_.hosted || sound_) && cab_sound_ && cab_front_ && visible());
}

void SheepView::run_script() {
    while (!script_.empty() && script_.front().first <= t_) {
        const std::string c = script_.front().second;
        script_.erase(script_.begin());
        if (c.rfind("size", 0) == 0) {  // dev: size600x370 resizes the window
            int ww = 0, hh = 0;
            if (std::sscanf(c.c_str() + 4, "%dx%d", &ww, &hh) == 2) dev_resize_window(ww, hh);
            continue;
        }
        if (c == "bot") { if (phase_ == Phase::play) stone(bot_stone(m_)); }
        else if (c == "solve") { if (phase_ == Phase::play && moves_.size() < lvl_.solution.size()) stone(lvl_.solution[moves_.size()]); }
        else if (c == "hint") hint();
        else if (c == "undo") undo();
        else if (c == "help") open(Panel::help);
        else if (c == "map") { map_page_ = -1; open(Panel::map); }
        else if (c == "close") open(Panel::none);
        else if (c == "next") action("next");
        else if (c.rfind("lvl", 0) == 0) load_level(std::atoi(c.c_str() + 3), false);
        else if (c.rfind("c", 0) == 0) stone(std::atoi(c.c_str() + 1));
        else if (c == "far") {
            // a careless stone: somewhere far from the sheep (for testing escapes)
            int best = -1, bd = -1;
            for (int i = 0; i < m_.w * m_.h; ++i) {
                const int dd = std::abs(m_.col(i) - m_.col(m_.sheep)) + std::abs(m_.row(i) - m_.row(m_.sheep));
                if (m_.can_place(i) && dd > bd) { bd = dd; best = i; }
            }
            if (best >= 0) stone(best);
        }
    }
}

void SheepView::animate(double dt) {
    SheepPose& p = st_.sheep;
    const V3 to = pas_.cell_pos(m_.sheep);
    // the answer: a beat, then the hop (or munch, or the dash off the edge)
    if (phase_ == Phase::answer && m_.escaped && last_.kind == SheepMove::step) {
        // the step took it onto the edge: the walking-off plays it from here (the hop, then away)
        finish(false);
        phase_t_ = 0;
    }
    if (phase_ == Phase::answer) {
        const double t = phase_t_;
        if (last_.kind == SheepMove::step) {
            const V3 from = pas_.cell_pos(from_cell_);
            const double u = std::clamp((t - .3) / .45, 0.0, 1.0);
            p.pos = from + (to - from) * u;
            p.hop = u > 0 && u < 1 ? u : 0;
            if (u > 0 && u < 1) {
                const V3 d = to - from;
                p.yaw = std::atan2(d.x, -d.y);
            }
            if (t > .3 && t - dt <= .3) play("sh_hop", .5f, .9f + .2f * static_cast<float>(std::fmod(t_ * 7, 1.0)));
            if (t > .8) {
                if (m_.munching) { say_ = "Clover! Mmm."; say_t_ = 1.6; play("sh_munch", .7f); }
                phase_ = Phase::play;
                if (m_.penned()) finish(true);
                layout_buttons();
            }
        } else if (last_.kind == SheepMove::munch) {
            p.head_down = 1;
            if (t > .5) { phase_ = Phase::play; if (m_.penned()) finish(true); layout_buttons(); }
        } else if (last_.kind == SheepMove::escape) {
            if (t > .3) { finish(false); }
        } else {
            if (t > .35) { phase_ = Phase::play; if (m_.penned()) finish(true); layout_buttons(); }
        }
    } else if (phase_ == Phase::lost) {
        // off it walks: the last step carried it onto the edge; it keeps going the same way, out of the field
        const double u = std::clamp((phase_t_ - .3) / .45, 0.0, 1.0);
        const V3 from = pas_.cell_pos(from_cell_);
        V3 dir = norm(V3{to.x - from.x, to.y - from.y, 0});
        if (from_cell_ == m_.sheep) dir = norm(V3{to.x, to.y, 0});
        if (phase_t_ < .75) {
            p.pos = from + (to - from) * u;
            p.hop = u > 0 && u < 1 ? u : 0;
        } else {
            const double walk = (phase_t_ - .75) * 1.6;
            p.pos = to + dir * walk;
            p.hop = std::fmod(walk * 2.2, 1.0) * .35;
            p.run = .8;
        }
        p.yaw = std::atan2(dir.x, -dir.y);
        p.fade = 1 - std::clamp((phase_t_ - 3.0) / .8, 0.0, 1.0);
    } else {
        p.pos = to;
        p.hop = 0;
        p.run = 0;
        p.fade = 1;
    }
    // idle life: chewing, blinking, an ear flick, looking about; sulking when penned
    const bool penned = phase_ == Phase::won;
    p.blink = std::fmod(t_ + .7, 3.3) < .14 ? 1 : 0;
    p.ear = std::fmod(t_ * .7, 4.0) < .3 ? 1 : 0;
    p.chew = m_.munching || p.head_down > .5 ? std::fabs(std::sin(t_ * 9)) : (std::fmod(t_, 6.0) < 2.5 ? std::fabs(std::sin(t_ * 6)) * .6 : 0);
    if (phase_ != Phase::answer || last_.kind != SheepMove::munch) p.head_down += ((m_.munching ? 1.0 : 0.0) - p.head_down) * std::min(1.0, dt * 4);
    // it doesn't jump at your stones; it turns, unhurried, to face the patch it means to go to next
    if (phase_ == Phase::play && !m_.escaped && !m_.penned()) {
        Meadow probe = m_;
        probe.head = 0;
        probe.munching = false;
        const SheepMove intent = sheep_choice(probe);
        if (intent.kind == SheepMove::step && intent.to != m_.sheep) {
            const V3 d = pas_.cell_pos(intent.to) - to;
            double want = std::atan2(d.x, -d.y), diff = want - p.yaw;
            while (diff > M_PI) diff -= 2 * M_PI;
            while (diff < -M_PI) diff += 2 * M_PI;
            p.yaw += diff * std::min(1.0, dt * 2.5);
        }
    }
    p.head_turn = penned ? .35 * std::sin(t_ * .3) : .25 * std::sin(t_ * .45) * std::sin(t_ * .13);
    p.sit += ((penned ? 1.0 : 0.0) - p.sit) * std::min(1.0, dt * 3);
    p.sleep = penned && phase_t_ > 4 ? std::min(1.0, (phase_t_ - 4) / 1.5) : 0;
    p.startle = std::max(0.0, p.startle - dt * 4);
    for (double& d : st_.drop) d = std::max(0.0, d - dt * 2.2);
    if ((phase_ == Phase::won && phase_t_ >= 1.0 && phase_t_ - dt < 1.0) || (phase_ == Phase::lost && phase_t_ >= 1.6 && phase_t_ - dt < 1.6)) layout_buttons();
    st_.celebrate = penned ? std::min(1.0, phase_t_ * 2) * std::max(0.0, 1 - (phase_t_ - 3) / 2) : 0;
    if (cab_reduced_) {
        p.pos = to; p.hop = p.run = p.blink = p.ear = p.chew = p.head_turn = p.startle = 0;
        if (phase_ == Phase::lost) p.fade = 0;
        st_.celebrate = 0;
        for (double& drop : st_.drop) drop = 0;
    }
    st_.hint = hint_;
    // the sheep's shortest way out, shown faintly while you hover the hint button
    st_.exit_path.clear();
    if (hover_ == "hint" && phase_ == Phase::play) {
        const auto d = m_.distances();
        int c = m_.sheep;
        for (int guard = 0; guard < 40 && d[static_cast<size_t>(c)] > 0; ++guard) {
            int nb[6];
            const int n = m_.neighbours(c, nb);
            int nx = -1;
            for (int j = 0; j < n; ++j) if (m_.open(nb[j]) && d[static_cast<size_t>(nb[j])] == d[static_cast<size_t>(c)] - 1) { nx = nb[j]; break; }
            if (nx < 0) break;
            st_.exit_path.push_back(nx);
            c = nx;
        }
    }
}

void SheepView::tick() {
    if (!visible() || !cab_front_) { last_t_ = std::chrono::steady_clock::now(); return; }
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - last_t_).count(), 0.0, .25);
    last_t_ = now;
    t_ += dt;
    phase_t_ += dt;
    say_t_ = std::max(0.0, say_t_ - dt);
    run_script();
    if (phase_ == Phase::loading && pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) begin_level();
    if (phase_ != Phase::loading) animate(dt);
    {
        const bool on = (opt_.hosted || music_) && cab_music_ && cab_front_;
        audio_music(visible() && cab_front_ ? (level_ % 2 ? "sh_music" : "sh_music_grass") : "", on);  // "Pasture Haze" on odd meadows, "Long Grass" on even
    }
    audio_tick(dt);
    save_t_ += dt;
    if (dirty_ && save_t_ > .5) { save_t_ = 0; persist(); }
    gf::Window* win = attached_window();
    // dev captures (--dev with SH_PRINT_WID) keep drawing even when the window is covered
    static const bool capturing = opt_.dev && std::getenv("SH_PRINT_WID");
    const bool hidden = win && (*win).occluded() && !capturing;
    const bool front = !win || (*win).active();
    const bool shown = visible();
    const auto pace = std::chrono::milliseconds(hidden || !shown || !cab_front_ ? 500 : front ? 33 : 100);
    if (timer_ && (*timer_).interval() != pace) (*timer_).set_interval(pace);

    if (hidden || !shown || frame_.px.empty() || pas_.r.rgb.empty()) return;
    if (phase_ != Phase::loading) {
        st_.meadow = &m_;
        pas_.render(st_, cab_reduced_ ? 0 : t_);
    }
    compose();
    publish();
}

void SheepView::publish() {
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
        const double k = pixel_ * bs_;
        if (xmap_.size() != static_cast<size_t>(phys_w_)) {
            xmap_.resize(static_cast<size_t>(phys_w_));
            for (int x = 0; x < phys_w_; ++x) xmap_[static_cast<size_t>(x)] = std::min(frame_.w - 1, static_cast<int>(x / k));
        }
        const std::uint32_t* src = reinterpret_cast<const std::uint32_t*>(frame_.px.data());
        for (int y = 0; y < phys_h_; ++y) {
            const int sy = std::min(frame_.h - 1, static_cast<int>(y / k));
            std::uint32_t* o = reinterpret_cast<std::uint32_t*>(dst.data() + static_cast<size_t>(y) * rb);
            const std::uint32_t* sr = src + static_cast<size_t>(sy) * frame_.w;
            for (int x = 0; x < phys_w_; ++x) o[x] = sr[xmap_[static_cast<size_t>(x)]];
        }
        blit_texts(reinterpret_cast<std::uint32_t*>(dst.data()), rb / 4, k);
        static_cast<void>(lease.publish());
    }
    if (!direct_) invalidate(gf::Dirty::paint);
    text_cache_trim();
}

// ------------------------------------------------------------------ input
void SheepView::on_pointer(gf::PointerEvent& e) {
    const gf::Point local = point_from_window(e.position);
    mouse_x_ = local.x / pixel_;
    mouse_y_ = local.y / pixel_;
    auto hit = [&]() -> std::string {
        for (const Button& b : buttons_)
            if (b.enabled && mouse_x_ >= b.x && mouse_x_ < b.x + b.w && mouse_y_ >= b.y && mouse_y_ < b.y + b.h) return b.id;
        return {};
    };
    const bool on_board = panel_ == Panel::none && phase_ == Phase::play && mouse_y_ > top_h_ && mouse_y_ < ph_ - bottom_h_;
    if (e.action == gf::PointerAction::move) {
        hover_ = hit();
        st_.hover = hover_.empty() && on_board ? pas_.pick(mouse_x_, mouse_y_) : -1;
        st_.hover_ok = st_.hover >= 0 && m_.can_place(st_.hover);
        set_cursor(!hover_.empty() || st_.hover_ok ? gf::CursorKind::hand : gf::CursorKind::arrow);
    }
    if (e.action == gf::PointerAction::down && e.button == gf::PointerButton::primary) {
        activate();
        const std::string h = hit();
        if (!h.empty()) { pressed_ = h; e.handled = true; return; }
        if (on_board) {
            const int c = pas_.pick(mouse_x_, mouse_y_);
            if (c >= 0) stone(c);
        }
        e.handled = true;
    } else if (e.action == gf::PointerAction::up) {
        if (!pressed_.empty() && hit() == pressed_) action(pressed_);
        pressed_.clear();
        e.handled = true;
    }
}

void SheepView::on_key(gf::KeyEvent& e) {
    if (e.handled || e.action != gf::KeyAction::down) return;
    using K = gf::PhysicalKey;
    const std::uint32_t k = e.physical_key;
    if (panel_ != Panel::none) {
        if (k == K::escape || k == K::enter || k == K::space) open(Panel::none);
        e.handled = true;
        return;
    }
    if (k == K::z || k == K::u || k == K::backspace) { undo(); e.handled = true; return; }
    if (k == K::r) { restart(); e.handled = true; return; }
    if (k == K::h) { hint(); e.handled = true; return; }
    if (!opt_.hosted && k == K::m) { action("music"); e.handled = true; return; }
    if (k == K::f1) { open(Panel::help); e.handled = true; return; }
    if (k == K::l) { map_page_ = -1; open(Panel::map); e.handled = true; return; }
    if ((k == K::enter || k == K::space || k == K::n) && phase_ == Phase::won) { action("next"); e.handled = true; return; }
    if ((k == K::enter || k == K::space) && phase_ == Phase::lost) { restart(); e.handled = true; return; }
}

void SheepView::action(const std::string& id) {
    play("sh_click", .4f);
    if (id == "close") open(Panel::none);
    else if (id == "help") open(Panel::help);
    else if (id == "map") { map_page_ = -1; open(Panel::map); }
    else if (id == "undo") undo();
    else if (id == "restart" || id == "retry") restart();
    else if (id == "hint") hint();
    else if (id == "music") { music_ = !music_; dirty_ = true; layout_buttons(); }
    else if (id == "next") { if (phase_ == Phase::won) load_level(level_ + 1, false); }
    else if (id == "skip") load_level(level_ + 1, false);
    else if (id == "page_prev") { --map_page_; layout_buttons(); }
    else if (id == "page_next") { ++map_page_; layout_buttons(); }
    else if (id.rfind("lv", 0) == 0) { open(Panel::none); load_level(std::atoi(id.c_str() + 2), false); }
    dirty_ = true;
}

void SheepView::open(Panel p) {
    panel_ = p;
    pressed_.clear();
    layout_buttons();
}

// ------------------------------------------------------------------ drawing
const Mask& SheepView::tmask(const std::string& s, int font, double size, int wrap_game) const {
    const Font f = font == 1 ? Font::speech_bold : font == 2 ? Font::title : font == 3 ? Font::ui : Font::speech;
    return text_mask(s, f, size, wrap_game > 0 ? wrap_game * pixel_ : 0);
}
int SheepView::text(const std::string& s, int x, int y, Col c, double size, int font, int wrap) {
    texts_.push_back({s, font, size, wrap, x, y, c});
    return text_w(s, size, font);
}
int SheepView::text_w(const std::string& s, double size, int font) const { return static_cast<int>(std::ceil(tmask(s, font, size, 0).w / static_cast<double>(pixel_))); }
int SheepView::text_h(const std::string& s, double size, int font, int wrap) const { return static_cast<int>(std::ceil(tmask(s, font, size, wrap).h / static_cast<double>(pixel_))); }

void SheepView::layout_game_buttons() {
    buttons_.clear();
    if (pw_ <= 0) return;
    const int bh = btn_h_;
    if (panel_ == Panel::help) {
        const Box hb = help_box();
        buttons_.push_back({"close", "Close", pw_ / 2 - 40, hb.y + hb.h + 6, 80, bh});
        return;
    }
    if (panel_ == Panel::map) {
        // a grid of meadows: every one you've reached, and the next, a page at a time
        const int bw = 36, cell_h = 26, gap = 4, top = compact_ ? 34 : 52, bottom = compact_ ? 44 : 64;
        const int cols = std::clamp((pw_ - 16) / (bw + gap), 1, 10);
        const int rows = std::max(1, (ph_ - top - bottom) / (cell_h + gap));
        const int per = cols * rows;
        const int n = std::min(100, std::max(reached_, level_));
        const int pages = std::max(1, (n + per - 1) / per);
        if (map_page_ < 0) map_page_ = std::max(0, level_ - 1) / per;
        map_page_ = std::clamp(map_page_, 0, pages - 1);
        const int gx = (pw_ - cols * (bw + gap) + gap) / 2;
        for (int k = 0; k < per; ++k) {
            const int i = map_page_ * per + k;
            if (i >= n) break;
            buttons_.push_back({"lv" + std::to_string(i + 1), std::to_string(i + 1), gx + (k % cols) * (bw + gap), top + (k / cols) * (cell_h + gap), bw, cell_h});
        }
        const int cy = ph_ - bh - 8;
        buttons_.push_back({"close", "Close", pw_ / 2 - 36, cy, 72, bh});
        if (pages > 1) {
            buttons_.push_back({"page_prev", "<", pw_ / 2 - 36 - 8 - 30, cy, 30, bh, map_page_ > 0});
            buttons_.push_back({"page_next", ">", pw_ / 2 + 36 + 8, cy, 30, bh, map_page_ < pages - 1});
        }
        return;
    }
    const int y = ph_ - bottom_h_ + (bottom_h_ - bh) / 2;
    const bool live = phase_ == Phase::play;
    // buttons fit their labels; in a narrow window the padding tightens, then the labels shorten
    struct Spec { const char* id; std::string label, brief; bool en; };
    const std::vector<Spec> left = {{"undo", "Undo", "Undo", (live || phase_ == Phase::lost) && !undo_.empty()},
                                    {"restart", "Restart", "Again", phase_ != Phase::loading},
                                    {"hint", "Hint", "Hint", live}};
    const std::vector<Spec> right = {{"map", "Meadows", "Map", true}, {"help", "Help", "?", true},
                                     {"music", music_ ? "Music" : "No music", music_ ? "Music" : "Mute", true}};
    int pad = 0, gap = 0;
    bool brief = false;
    auto width_of = [&](const Spec& sp) { return std::max(brief ? 22 : 40, text_w(brief ? sp.brief : sp.label, 11, 1) + 2 * pad); };
    auto total = [&]() { int t = 16 + 12; for (const Spec& sp : left) t += width_of(sp) + gap; for (const Spec& sp : right) t += width_of(sp) + gap; return t; };
    for (const auto& [p, g, br] : {std::tuple{10, 6, false}, std::tuple{6, 4, false}, std::tuple{6, 4, true}, std::tuple{4, 3, true}}) {
        pad = p; gap = g; brief = br;
        if (total() <= pw_) break;
    }
    int x = 8;
    for (const Spec& sp : left) { const int w = width_of(sp); buttons_.push_back({sp.id, brief ? sp.brief : sp.label, x, y, w, bh, sp.en}); x += w + gap; }
    int rw = -gap;
    for (const Spec& sp : right) rw += width_of(sp) + gap;
    x = pw_ - 8 - rw;
    for (const Spec& sp : right) { const int w = width_of(sp); buttons_.push_back({sp.id, brief ? sp.brief : sp.label, x, y, w, bh, sp.en}); x += w + gap; }
    // the result's buttons arrive with its card
    const Box rc = result_box();
    const int ry = rc.y + rc.h + 6;
    if (phase_ == Phase::won && phase_t_ >= 1.0) buttons_.push_back({"next", "Next meadow", pw_ / 2 - 50, ry, 100, bh + 2});
    if (phase_ == Phase::lost && phase_t_ >= 1.6) {
        const int w = std::min(110, (pw_ - 30) / 2);
        buttons_.push_back({"retry", "Try again", pw_ / 2 - 3 - w, ry, w, bh + 2});
        buttons_.push_back({"skip", compact_ ? "Skip it" : "Skip this meadow", pw_ / 2 + 3, ry, w, bh + 2});
    }
}

// the result card, centred on the meadow
SheepView::Box SheepView::result_box() const {
    // as wide as its words need, and only as tall
    const bool won = phase_ == Phase::won;
    const std::string t = won ? "Penned with " + std::to_string(m_.stones) + " fences" : "The sheep got away!";
    const std::string sub = won ? "(a hint was taken: two stars at most)" : "Undo to step back, or try the meadow again.";
    const int w = std::clamp(std::max(text_w(t, 16, 2), text_w(sub, 10, 0)) + 30, 160, std::min(260, pw_ - 24));
    const int h = won ? (compact_ ? 70 : 84) : (compact_ ? 46 : 56);
    const int mid = (top_h_ + ph_ - bottom_h_) / 2;
    return {(pw_ - w) / 2, std::max(top_h_ + 4, mid - (h + btn_h_ + 8) / 2), w, h};
}

// the help card: its words wrap to the width, and step down a size if the window is short
SheepView::Box SheepView::help_box() const {
    const int ww = std::min(pw_ - 20, 380);
    const int room = ph_ - 20 - btn_h_ - 6;
    help_size_ = 11;
    int need = 0;
    for (double size : {11.0, 10.0, 9.0}) {
        help_size_ = size;
        need = 32 + 6;
        for (const std::string& l : help_lines()) need += text_h(l, size, 0, ww - 28) + 4;
        if (need <= room) break;
    }
    const int wh = std::min(need, room);
    return {(pw_ - ww) / 2, std::max(8, (ph_ - wh - btn_h_ - 6) / 2), ww, wh};
}

std::vector<std::string> SheepView::help_lines() const {
    return {"Click a patch of grass to put up a fence there; it joins any fence beside it. Then the sheep trots one patch toward the edge. If it reaches the edge, it walks off and it's gone.",
            "Fence it in so no way leads out and it sits down and sulks: you win. You get three fences before it starts moving.",
            "Dozy sheep dawdle; clever ones keep their options open; cunning ones think a fence ahead. Any sheep will stop for clover.",
            "Stars: penned at or under par (the fences our own shepherd needed) earns three.",
            "Keys: Z undo, R restart, H hint, L meadows, Use the capsule for music and sound."};
}

void SheepView::draw_button(const Button& b) {
    const bool down = pressed_ == b.id, over = hover_ == b.id && b.enabled;
    const Col face = !b.enabled ? hex(0xA8B098) : over ? hex(0xFFFDF0) : kCream;
    frame_.fill_rect(b.x + 1, b.y + 2, b.w, b.h, hex(0x000000, .25f));
    frame_.begin(); frame_.rrect(b.x, b.y + (down ? 1 : 0), b.w, b.h, 5); frame_.fill(face);
    frame_.begin(); frame_.rrect(b.x + .5, b.y + .5 + (down ? 1 : 0), b.w - 1, b.h - 1, 5); frame_.stroke(kLeaf, 1);
    // a meadow button in the map shows its stars
    if (b.id.rfind("lv", 0) == 0) {
        const int lv = std::atoi(b.id.c_str() + 2);
        const int s = lv - 1 < static_cast<int>(stars_.size()) ? stars_[static_cast<size_t>(lv - 1)] : 0;
        text(b.label, b.x + (b.w - text_w(b.label, 11, 1)) / 2, b.y + 2, lv == level_ ? hex(0xC06020) : kInk, 11, 1);
        for (int k = 0; k < 3; ++k) draw_star(b.x + b.w / 2.0 + (k - 1) * 9, b.y + b.h - 6, 3.5, k < s);
        return;
    }
    const double size = 11;
    const int tw = text_w(b.label, size, 1), th = text_h(b.label, size, 1);
    text(b.label, b.x + std::max(4, (b.w - tw) / 2), b.y + (b.h - th) / 2 + (down ? 1 : 0), b.enabled ? kInk : hex(0x6A7060), size, 1);
}

void SheepView::draw_star(double cx, double cy, double r, bool lit) {
    frame_.begin();
    for (int k = 0; k < 10; ++k) {
        const double a = -M_PI / 2 + k * M_PI / 5, rr = k % 2 ? r * .45 : r;
        if (k == 0) frame_.move(cx + std::cos(a) * rr, cy + std::sin(a) * rr);
        else frame_.line(cx + std::cos(a) * rr, cy + std::sin(a) * rr);
    }
    frame_.close();
    frame_.fill(lit ? kGold : hex(0xC8C8B8, .7f));
}

void SheepView::draw_bars() {
    // top: the meadow, the sheep, the stones against par
    frame_.fill_rect(0, 0, pw_, top_h_, hex(0x2E4A22, .82f));
    const double ts = compact_ ? 11 : 12;
    const int ty = (top_h_ - text_h("M", ts, 1)) / 2;
    if (phase_ == Phase::loading) { text("Herding the next sheep...", 10, ty, kCream, ts, 1); return; }
    // the right side first (the score must always show), then the title in what's left
    std::string b = "Fences: " + std::to_string(m_.stones) + "   Par: " + std::to_string(lvl_.par);
    if (text_w(b, 11, 0) + 50 > pw_ / 2) b = std::to_string(m_.stones) + " / par " + std::to_string(lvl_.par);
    const int bw = text_w(b, 11, 0);
    text(b, pw_ - 12 - bw - 40, (top_h_ - text_h(b, 11, 0)) / 2, hex(0xE0F0C8), 11, 0);
    const int s = phase_ == Phase::won ? star_count() : (m_.stones <= lvl_.par ? 3 : m_.stones <= lvl_.par + 3 ? 2 : 1);
    for (int k = 0; k < 3; ++k) draw_star(pw_ - 34 + k * 11, top_h_ / 2.0, 4.5, k < (hinted_ ? std::min(s, 2) : s));
    std::string a = "Meadow " + std::to_string(level_) + "  -  " + kind_line(m_.smarts);
    if (10 + text_w(a, ts, 1) > pw_ - 12 - bw - 50) a = "Meadow " + std::to_string(level_);
    text(a, 10, ty, kCream, ts, 1);
    if (m_.head > 0 && phase_ == Phase::play && panel_ == Panel::none) {
        const std::string h = std::to_string(m_.head) + (m_.head == 1 ? " free fence" : " free fences") + " before it moves";
        const int w = text_w(h, 11, 1) + 16, hh = text_h(h, 11, 1) + 6;
        frame_.begin(); frame_.rrect((pw_ - w) / 2, top_h_ + 4, w, hh, hh / 2.0); frame_.fill(hex(0xFFF8E8, .9f));
        text(h, (pw_ - w) / 2 + 8, top_h_ + 7, kInk, 11, 1);
    }
    // bottom: the buttons
    frame_.fill_rect(0, ph_ - bottom_h_, pw_, bottom_h_, hex(0x2E4A22, .82f));
    // a thought from the meadow
    if (say_t_ > 0 && !say_.empty() && panel_ == Panel::none) {
        const float a = static_cast<float>(std::min(1.0, say_t_ * 2));
        const int w = std::min(pw_ - 16, text_w(say_, 12, 1) + 20);
        const int th = text_h(say_, 12, 1, w - 20), bh = th + 6;
        const int y = ph_ - bottom_h_ - bh - 6;
        frame_.begin(); frame_.rrect((pw_ - w) / 2, y, w, bh, std::min(10, bh / 2)); frame_.fill(hex(0xFFFFFF, .92f * a));
        text(say_, (pw_ - w) / 2 + 10, y + 3, alpha(kInk, a), 12, 1, w - 20);
    }
}

void SheepView::draw_result() {
    if (phase_ != Phase::won && phase_ != Phase::lost) return;
    if (phase_t_ < (phase_ == Phase::won ? 1.0 : 1.6)) return;
    const Box rc = result_box();
    const int w = rc.w, h = rc.h, x = rc.x, y = rc.y;
    const double k = compact_ ? .8 : 1;  // the card's insides, a little closer together when small
    frame_.fill_rect(x + 3, y + 3, w, h, hex(0x000000, .25f));
    frame_.begin(); frame_.rrect(x, y, w, h, 8); frame_.fill(hex(0xFFF8E8, .96f));
    frame_.begin(); frame_.rrect(x + .5, y + .5, w - 1, h - 1, 8); frame_.stroke(kLeaf, 1.5);
    if (phase_ == Phase::won) {
        const std::string t = "Penned with " + std::to_string(m_.stones) + " fences";
        text(t, x + (w - text_w(t, 16, 2)) / 2, y + static_cast<int>(8 * k), kInk, 16, 2);
        const int s = star_count();
        for (int i = 0; i < 3; ++i) draw_star(x + w / 2.0 + (i - 1) * 26, y + 42 * k, 10, i < s);
        const std::string p = hinted_ ? "(a hint was taken: two stars at most)" : s == 3 ? "At or under par!" : "Par is " + std::to_string(lvl_.par) + ".";
        text(p, x + std::max(6, (w - text_w(p, 9.5, 0)) / 2), y + static_cast<int>(56 * k), hex(0x6A6A50), 9.5, 0, w - 12);
    } else {
        const std::string t = "The sheep got away!";
        text(t, x + (w - text_w(t, 16, 2)) / 2, y + static_cast<int>(10 * k), kInk, 16, 2);
        const std::string p = "Undo to step back, or try the meadow again.";
        text(p, x + std::max(6, (w - text_w(p, 10, 0)) / 2), y + static_cast<int>(32 * k), hex(0x6A6A50), 10, 0, w - 12);
    }
}

void SheepView::draw_panel() {
    if (panel_ == Panel::none) return;
    frame_.fill_rect(0, 0, pw_, ph_, hex(0x1A2A12, .55f));
    if (panel_ == Panel::help) {
        const Box hb = help_box();
        const int ww = hb.w, wh = hb.h, wx = hb.x, wy = hb.y;
        frame_.begin(); frame_.rrect(wx, wy, ww, wh, 8); frame_.fill(hex(0xFFF8E8));
        frame_.begin(); frame_.rrect(wx + .5, wy + .5, ww - 1, wh - 1, 8); frame_.stroke(kLeaf, 1.5);
        text("Pen the Sheep", wx + (ww - text_w("Pen the Sheep", 15, 2)) / 2, wy + 8, kInk, 15, 2);
        int ly = wy + 32;
        const std::vector<std::string> lines = help_lines();
        for (size_t i = 0; i < lines.size(); ++i) {
            const bool keys = i + 1 == lines.size();
            const double size = keys ? help_size_ - 1 : help_size_;
            text(lines[i], wx + 14, ly, keys ? hex(0x6A6A50) : kInk, size, keys ? 1 : 0, ww - 28);
            ly += text_h(lines[i], size, keys ? 1 : 0, ww - 28) + 4;
        }
    } else {
        const double hs = compact_ ? 14 : 18;
        text("Meadows", (pw_ - text_w("Meadows", hs, 2)) / 2, compact_ ? 8 : 22, kCream, hs, 2);
        int total = 0;
        for (int s : stars_) total += s;
        const std::string t = std::to_string(total) + " stars";
        text(t, (pw_ - text_w(t, 11, 1)) / 2, ph_ - btn_h_ - 10 - text_h(t, 11, 1) - 4, kCream, 11, 1);
    }
}

void SheepView::compose() {
    texts_.clear();
    if (phase_ == Phase::loading) frame_.clear(hex(0x7CC060));
    else pas_.r.present(frame_, 1, 0, 0, true);
    draw_bars();
    draw_result();
    if (panel_ != Panel::none) {
        texts_.clear();
        draw_panel();
    }
    for (const Button& b : buttons_) draw_button(b);
}

void SheepView::blit_texts(std::uint32_t* dst, size_t stride_px, double k) {
    const int sc = std::max(1, static_cast<int>(std::lround(bs_)));
    for (const HiText& h : texts_) {
        const Mask& m = tmask(h.s, h.font, h.size, h.wrap);
        const int ox = static_cast<int>(h.x * k), oy = static_cast<int>(h.y * k);
        const float pr = h.c.r * h.c.a, pg = h.c.g * h.c.a, pb = h.c.b * h.c.a;
        for (int my = 0; my < m.h * sc; ++my) {
            const int dy = oy + my;
            if (dy < 0 || dy >= phys_h_) continue;
            const std::uint8_t* srow = m.a.data() + static_cast<size_t>(my / sc) * m.w;
            std::uint32_t* drow = dst + static_cast<size_t>(dy) * stride_px;
            for (int mx = 0; mx < m.w * sc; ++mx) {
                const int dx = ox + mx;
                if (dx < 0 || dx >= phys_w_) continue;
                const float cov = srow[mx / sc] * (1.f / 255.f);
                if (cov <= 0) continue;
                const std::uint32_t d = drow[dx];
                const float a = h.c.a * cov, kk = 1 - a;
                const auto chn = [&](int sh, float src) { return static_cast<std::uint32_t>(std::min(255.f, src * cov * 255 + static_cast<float>((d >> sh) & 255) * kk + .5f)) << sh; };
                drow[dx] = chn(0, pb) | chn(8, pg) | chn(16, pr) | (0xFFu << 24);
            }
        }
    }
}

}  // namespace sh

namespace sh {
void SheepView::set_cabinet(bool foreground, bool music, bool sound, bool reduced) {
    if (timer_) {
        if (foreground) {
            last_t_ = std::chrono::steady_clock::now();
            (*timer_).start();
        } else {
            (*timer_).stop();
        }
    }

    cab_front_ = foreground; cab_music_ = music; cab_sound_ = sound; cab_reduced_ = reduced;
    audio_cabinet(foreground, music, sound);
    if (!foreground) { pressed_.clear(); }
}
}

namespace sh {
void SheepView::layout_buttons() {
    layout_game_buttons();
    if (!opt_.hosted) return;
    for (std::size_t i = buttons_.size(); i > 0; --i) {
        const std::string& id = buttons_[i-1].id;
        if (id == "help" || id == "map" || id == "music") buttons_.erase(buttons_.begin() + static_cast<std::ptrdiff_t>(i-1));
    }
}
std::vector<games::GameCommand> SheepView::commands() const { return {{"map", "Meadows", true, panel_ == Panel::map}, {"help", "Help", true, panel_ == Panel::help}}; }
void SheepView::run_command(std::string_view id) {
    for (const games::GameCommand& cmd : commands()) {
        if (cmd.id == id && cmd.enabled) { action(cmd.checked ? "close" : std::string(id)); return; }
    }
}
}
