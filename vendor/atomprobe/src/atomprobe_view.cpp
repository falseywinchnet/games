#include "atomprobe_view.hpp"

#include "platform/audio.hpp"
#include "runtime_paths.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>

namespace ap {

namespace {
const Col kInk = hex(0x061018), kPanel = hex(0x0B141C), kCyan = hex(0x56F0FF), kCyanDim = hex(0x1E5A66), kText = hex(0xCFE8EE);
const Col kGood = hex(0x6CFF9A), kBad = hex(0xFF4A5A), kGold = hex(0xFFE08A);
constexpr double kCharge = .28, kAnswer = .95, kDone = 1.05;  // the probe's timeline (the same for every beam: its length tells nothing)
constexpr double kReplayStart = 2.6, kReplayGap = .32;

std::uint64_t seed_now() { return static_cast<std::uint64_t>(wall_clock() * 1000) ^ 0xA70BEULL; }

V3 tip(int p) { return Chamber::muzzle(p) + Chamber::outward(p) * .06; }
V3 hole_at_beam(int p) {
    const V3 h = Chamber::hole(p);
    return {h.x, h.y, Chamber::kBeamZ};
}
Col kind_col(Outcome k, int pair) {
    PortView v;
    v.kind = k == Outcome::hit ? 1 : k == Outcome::reflect ? 2 : 3;
    v.pair = pair;
    return port_color(v);
}
const char* kWords[] = {"one", "two", "three", "four"};
}  // namespace

AtomProbeView::AtomProbeView(gf::StableId id, Options opt) : Control(std::move(id)), opt_(opt), box_(seed_now()) {
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    {
        gf::SurfaceMaterial none;
        none.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(0, 0, 0))};
        set_authored_surface_material(none);
    }
    set_accessible_name("Atom Probe. Four atoms hide in a fogged eight by eight chamber. Fire beams from the 32 emitters around the edge: "
                        "a beam is absorbed, reflected, or comes out somewhere else. Mark four squares on the glass, then pull the lever to open the box. "
                        "Fewest points wins. Keys: Enter opens the box, N new box, F1 help, T top scores, M music, S sound.");
    bool resumed = false;
    if (load_save(save_path(opt_.dev), save_) && save_.has_box && box_.restore(save_.box)) {
        resumed = true;
        sync_ports();
        st_.marks = box_.state().marks;
        st_.empties = box_.state().empties;
        st_.atoms = box_.atoms();
        if (box_.opened()) {
            // reopen on the finished box, everything already revealed
            st_.fog = 0; st_.lid = 1; st_.atoms_on = 1; st_.judge = 1; st_.lever = 1;
            for (const Probe& pr : box_.probes()) {
                const Trace tr = trace(box_.atoms(), pr.port);
                Bolt b;
                b.pts.push_back(tip(pr.port));
                for (const Pt& q : tr.path) b.pts.push_back(Chamber::cell(q.x, q.y));
                if (tr.kind == Outcome::exit) b.pts.push_back(tip(tr.exit));
                else if (tr.kind == Outcome::reflect) b.pts.push_back(tip(pr.port));
                b.keep = true;
                b.col = kind_col(pr.kind, pr.pair);
                b.head = 1e9;
                st_.bolts.push_back(b);
            }
            reveal_t_ = 1e9;
            replays_ = static_cast<int>(box_.probes().size());
            locks_ = kAtoms;
            result_ = true;
        } else {
            say("Welcome back. The box is as you left it.", kText);
        }
    }
    name_entry_ = save_.settings.player_name;
    if (!resumed) new_box();
    if (const char* sc = std::getenv("AP_SCRIPT"); sc && opt_.dev) {
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

void AtomProbeView::on_attached_to_window() {
    audio_start(asset_dir());
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(33));
    subs_.push_back((*timer_).tick().subscribe(*this, gf::Delegate<>::bind<AtomProbeView, &AtomProbeView::tick>(*this)));
    subs_.push_back((*attached_window()).active_changed().subscribe(
        *this, gf::Delegate<bool>::bind<AtomProbeView, &AtomProbeView::active_changed>(*this)));
    last_ = std::chrono::steady_clock::now();
    (*timer_).start();
}

void AtomProbeView::on_detaching_from_window(gf::Window&) noexcept {
    try { persist(); } catch (...) {}
    if (timer_) (*timer_).stop();
    timer_.reset();
    subs_.clear();
    cancel_press();
    audio_stop();
}

void AtomProbeView::set_cabinet(bool foreground, bool music, bool sound, bool reduced_motion) {
    cab_front_ = foreground;
    cab_music_ = music;
    cab_sound_ = sound;
    cab_reduced_ = reduced_motion;
    audio_cabinet(foreground, music, sound);
    if (!foreground) { cancel_press(); }
}
void AtomProbeView::cancel_press() {
    pressed_.clear();
    hover_.clear();
    mouse_in_ = false;
    st_.hover_port = st_.hover_cell = st_.partner = -1;
    st_.lever_hover = false;
    set_cursor(gf::CursorKind::arrow);
}
void AtomProbeView::active_changed(bool active) {
    if (!active) { cancel_press(); }
}
void AtomProbeView::on_focus_changed(bool focused) {
    if (!focused) { cancel_press(); }
}
std::string AtomProbeView::hit_button() const {
    for (const Button& button : buttons_) {
        if (mouse_x_ >= button.x && mouse_x_ < button.x + button.w &&
            mouse_y_ >= button.y && mouse_y_ < button.y + button.h) { return button.id; }
    }
    return {};
}

void AtomProbeView::activate() {
    if (attached_window()) static_cast<void>((*attached_window()).request_focus(shared_from_this()));
}

void AtomProbeView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    pw_ = std::max(64, static_cast<int>(std::ceil(bounds.width / pixel_)));
    ph_ = std::max(64, static_cast<int>(std::ceil(bounds.height / pixel_)));
    frame_.resize(pw_, ph_);
    ch_.resize(pw_, ph_);
    bs_ = attached_window() != nullptr ? (*attached_window()).scale() : 1.0;
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

void AtomProbeView::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    if (surface_ && !direct_) p.draw_live_surface(surface_, b);
    else p.fill_rect(b, gf::Color::rgba(6, 10, 14));
}

// ------------------------------------------------------------------ game
void AtomProbeView::play(const std::string& name, float gain, float rate) {
    audio_sfx(name, gain, rate, save_.settings.sound && cab_sound_ && cab_front_ && visible());
}

void AtomProbeView::say(const std::string& s, Col c) {
    message_ = s;
    message_col_ = c;
    message_t_ = 0;
}

void AtomProbeView::new_box() {
    box_.new_box();
    const double lx = st_.look_x, ly = st_.look_y;
    st_ = ChamberState{};
    st_.look_x = lx;
    st_.look_y = ly;
    st_.atoms = box_.atoms();
    st_.boot = .01;
    shot_ = Shot{};
    reveal_t_ = -1;
    replays_ = locks_ = 0;
    result_ = false;
    pending_score_ = 0;
    say("A fresh box. Four atoms are hiding in the fog.", kText);
    dirty_ = true;
}

// The rim shows every answer except the one still in flight.
void AtomProbeView::sync_ports() {
    st_.ports = {};
    int pts = 0;
    for (const Probe& pr : box_.probes()) {
        if (shot_.port >= 0 && shot_.stage < 2 && pr.port == shot_.pr.port) continue;
        PortView v;
        v.kind = pr.kind == Outcome::hit ? 1 : pr.kind == Outcome::reflect ? 2 : 3;
        v.pair = pr.pair;
        st_.ports[static_cast<size_t>(pr.port)] = v;
        if (pr.kind == Outcome::exit) st_.ports[static_cast<size_t>(pr.exit)] = v;
        pts += pr.kind == Outcome::exit ? 2 : 1;
    }
    st_.points = pts;
}

void AtomProbeView::fire(int port) {
    if (port < 0 || box_.opened() || reveal_t_ >= 0 || shot_.port >= 0 || panel_ != Panel::none) return;
    bool fresh = false;
    const Probe pr = box_.fire(port, fresh);
    if (!fresh) {
        // already on the rim: show it again, free
        const int a = pr.port, b = pr.kind == Outcome::exit ? pr.exit : pr.port;
        st_.ports[static_cast<size_t>(a)].flash = 1;
        st_.ports[static_cast<size_t>(b)].flash = 1;
        play("ap_blip", .7f);
        say(pr.kind == Outcome::hit ? "Known: absorbed." : pr.kind == Outcome::reflect ? "Known: reflected." : "Known: detour " + std::to_string(pr.pair) + ".", kText);
        return;
    }
    shot_ = Shot{};
    shot_.port = port;
    shot_.pr = pr;
    // rules first: the probe is already recorded and saved; the show catches up
    sync_ports();
    // the rim's port remembers its old colour until answered; restore the in-flight one to dark
    play("ap_charge", .8f);
    dirty_ = true;
}

void AtomProbeView::shot_tick(double dt) {
    for (PortView& p : st_.ports) { p.flash = std::max(0.0, p.flash - dt * 1.6); p.charge = std::max(0.0, p.charge - dt * 3); }
    st_.glow = std::max(0.0, st_.glow - dt * 1.4);
    st_.hit = std::max(0.0, st_.hit - dt * 1.1);
    if (st_.ripple >= 0) { st_.ripple += dt; if (st_.ripple > 1) st_.ripple = -1; }
    if (shot_.port < 0) return;
    const double before = shot_.t;
    shot_.t += dt;
    const int p = shot_.port;
    auto crossed = [&](double at) { return before < at && shot_.t >= at; };
    if (shot_.t < kCharge) st_.ports[static_cast<size_t>(p)].charge = shot_.t / kCharge;
    if (crossed(kCharge)) {
        // the shot: a streak from the muzzle into the port
        Bolt b;
        b.pts = {tip(p), hole_at_beam(p)};
        b.col = kCyan;
        b.speed = 9;
        b.tail = .6;
        st_.bolts.push_back(b);
        play("ap_fire", .9f, .96f + .08f * static_cast<float>(p % 8) / 8);
        shot_.stage = 1;
    }
    if (crossed(kCharge + .06)) {
        st_.ripple = 0;
        st_.ripple_at = Chamber::hole(p);
        play("ap_hum", .7f);
    }
    if (shot_.stage == 1 && shot_.t > kCharge + .06 && shot_.t < kAnswer) st_.glow = std::min(1.0, st_.glow + dt * 4);
    if (crossed(kAnswer)) {
        shot_.stage = 2;
        const Probe& pr = shot_.pr;
        sync_ports();
        st_.ports[static_cast<size_t>(pr.port)].flash = 1;
        if (pr.kind == Outcome::exit) {
            st_.ports[static_cast<size_t>(pr.exit)].flash = 1;
            Bolt b;
            b.pts = {hole_at_beam(pr.exit), tip(pr.exit)};
            b.col = kind_col(pr.kind, pr.pair);
            b.speed = 9;
            b.tail = .6;
            st_.bolts.push_back(b);
            play("ap_exit", .85f, 1 + .02f * static_cast<float>((pr.pair - 1) % 8));
            say("Detour " + std::to_string(pr.pair) + ".", kind_col(pr.kind, pr.pair));
        } else if (pr.kind == Outcome::reflect) {
            Bolt b;
            b.pts = {hole_at_beam(pr.port), tip(pr.port)};
            b.col = hex(0xF4F7FF);
            b.speed = 9;
            b.tail = .6;
            st_.bolts.push_back(b);
            play("ap_reflect", .85f);
            say("Reflected.", hex(0xF4F7FF));
        } else {
            st_.hit = 1;
            st_.shake = std::max(st_.shake, .8);
            play("ap_hit", .95f);
            say("Absorbed.", hex(0xFF3B5C));
        }
    }
    if (shot_.t >= kDone) shot_ = Shot{};
}

void AtomProbeView::mark(int cell, bool cross) {
    if (cell < 0 || box_.opened() || reveal_t_ >= 0 || panel_ != Panel::none) return;
    if (cross) {
        if (box_.toggle_empty(cell)) play(box_.empty_mark(cell) ? "ap_cross" : "ap_unmark", .6f);
    } else if (box_.marked(cell)) {
        box_.toggle_mark(cell);
        play("ap_unmark", .7f);
    } else if (box_.toggle_mark(cell)) {
        st_.pop[static_cast<size_t>(cell)] = 1;
        play("ap_mark", .8f, .95f + .05f * static_cast<float>(box_.marks()));
        if (box_.can_open()) say("Four markers placed. Pull the lever to open the box.", kGood);
    } else {
        st_.tray_flash = .6;
        play("ap_full", .6f);
        say("All four markers are out. Click one to pick it back up.", kBad);
    }
    st_.marks = box_.state().marks;
    st_.empties = box_.state().empties;
    dirty_ = true;
}

void AtomProbeView::pull_lever() {
    if (reveal_t_ >= 0 || panel_ != Panel::none) return;  // a beam still in flight finishes during the reveal
    if (!box_.can_open()) {
        st_.tray_flash = .6;
        play("ap_full", .6f, .8f);
        const int left = kAtoms - box_.marks();
        say("Place " + std::string(left == 1 ? "one more marker" : std::string(kWords[std::clamp(left, 1, 4) - 1]) + " more markers") + " first.", kBad);
        return;
    }
    box_.open();
    ++save_.opened;
    reveal_t_ = 0;
    replays_ = locks_ = 0;
    play("ap_lever", 1);
    say("Opening the box...", kText);
    dirty_ = true;
    persist();
}

// The box opens: the lever, the vents, the lid, the atoms, every beam again along its true path, then the reckoning.
void AtomProbeView::reveal_tick(double dt) {
    if (reveal_t_ < 0 || reveal_t_ > 1e8) return;
    const double before = reveal_t_;
    reveal_t_ += dt;
    const double rt = reveal_t_;
    auto crossed = [&](double at) { return before < at && rt >= at; };
    st_.lever = std::min(1.0, rt / .3);
    if (crossed(.35)) play("ap_vent", .9f);
    if (rt >= .35) st_.fog = std::max(0.0, 1 - (rt - .35) / 1.6);
    st_.lid = std::clamp((rt - .5) / 1.8, 0.0, 1.0);
    if (crossed(1.5)) play("ap_atoms", .9f);
    st_.atoms_on = std::clamp((rt - 1.5) / .9, 0.0, 1.0);
    const int n = static_cast<int>(box_.probes().size());
    while (replays_ < n && rt >= kReplayStart + replays_ * kReplayGap) {
        const Probe& pr = box_.probes()[static_cast<size_t>(replays_)];
        const Trace tr = trace(box_.atoms(), pr.port);
        Bolt b;
        b.pts.push_back(tip(pr.port));
        for (const Pt& q : tr.path) b.pts.push_back(Chamber::cell(q.x, q.y));
        if (tr.kind == Outcome::exit) b.pts.push_back(tip(tr.exit));
        else if (tr.kind == Outcome::reflect) b.pts.push_back(tip(pr.port));
        b.keep = true;
        b.col = kind_col(pr.kind, pr.pair);
        b.speed = 15;
        b.tail = 1.6;
        st_.bolts.push_back(b);
        play("ap_replay", .6f, std::min(1.6f, .9f + .035f * static_cast<float>(replays_)));
        ++replays_;
    }
    const double judge_at = kReplayStart + n * kReplayGap + (n ? 1.0 : 0.0);
    st_.judge = std::clamp((rt - judge_at) / .8, 0.0, 1.0);
    // each marker in turn: locked onto an atom, or not
    std::vector<int> cells;
    for (int c = 0; c < kN * kN; ++c)
        if (box_.marked(c)) cells.push_back(c);
    while (locks_ < static_cast<int>(cells.size()) && rt >= judge_at + .8 + locks_ * .22) {
        const int c = cells[static_cast<size_t>(locks_)];
        if (box_.atom(c)) play("ap_lock", .85f, 1 + .12f * static_cast<float>(locks_));
        else play("ap_wrong", .8f);
        ++locks_;
    }
    const double end = judge_at + .8 + kAtoms * .22 + .3;
    if (crossed(end)) {
        result_ = true;
        const bool solved = box_.found() == kAtoms;
        st_.points = box_.points();
        if (solved) {
            ++save_.solved;
            play("ap_stinger_win", .9f);
            audio_duck_music(1);
            say("All four atoms found.", kGood);
            pending_score_ = box_.points();
        } else {
            play("ap_stinger_miss", .9f);
            audio_duck_music(1);
            say(box_.found() == 0 ? "None of the four found." : std::string(kWords[box_.found() - 1]) + " of four found.", kBad);
            message_[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(message_[0])));
        }
        layout_buttons();
        dirty_ = true;
    }
}

void AtomProbeView::bolts_tick(double dt) {
    for (Bolt& b : st_.bolts) {
        b.head += b.speed * dt;
        if (b.arrived() && !b.keep && b.head > b.length() + b.tail) b.fade -= dt * 3;
    }
    st_.bolts.erase(std::remove_if(st_.bolts.begin(), st_.bolts.end(), [](const Bolt& b) { return b.fade <= 0; }), st_.bolts.end());
}

void AtomProbeView::persist() {
    save_.box = box_.state();
    save_.has_box = true;
    write_save(save_path(opt_.dev), save_);
    dirty_ = false;
}

void AtomProbeView::run_script() {
    while (!script_.empty() && script_.front().first <= t_) {
        const std::string code = script_.front().second;
        script_.erase(script_.begin());
        if (code == "n") new_box();
        else if (code == "o") pull_lever();
        else if (code == "a") { for (int c = 0; c < 64; ++c) if (box_.atom(c) && !box_.marked(c)) mark(c, false); }   // the answer
        else if (code == "w") {  // three right, one wrong
            int k = 0;
            for (int c = 0; c < 64 && k < 3; ++c) if (box_.atom(c)) { mark(c, false); ++k; }
            for (int c = 0; c < 64; ++c) if (!box_.atom(c)) { mark(c, false); break; }
        }
        else if (code == "help") open(Panel::help);
        else if (code == "scores") open(Panel::scores);
        else if (code[0] == 'f') fire(std::atoi(code.c_str() + 1));
        else if (code[0] == 'm') mark(std::atoi(code.c_str() + 1), false);
        else if (code[0] == 'x') mark(std::atoi(code.c_str() + 1), true);
        else if (code[0] == 'h') { st_.hover_port = std::atoi(code.c_str() + 1); }
        else if (code[0] == 'c') { st_.hover_cell = std::atoi(code.c_str() + 1); }
    }
}

void AtomProbeView::tick() {
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - last_).count(), 0.0, .25);
    last_ = now;
    t_ += dt;
    run_script();
    shot_tick(dt);
    reveal_tick(dt);
    bolts_tick(dt);
    for (double& p : st_.pop) p = std::max(0.0, p - dt * 4);
    st_.tray_flash = std::max(0.0, st_.tray_flash - dt);
    st_.shake = std::max(0.0, st_.shake - dt * 2.5);
    if (st_.boot > 0) { st_.boot += dt / 1.1; if (st_.boot >= 1) st_.boot = 0; }
    message_t_ += dt;
    st_.markers_left = kAtoms - box_.marks();
    st_.lever_ready = box_.can_open();
    if (result_) st_.points = box_.points();
    // a little parallax: the camera drifts toward the pointer
    const double tx = mouse_in_ && pw_ > 0 ? mouse_x_ / pw_ * 2 - 1 : 0, ty = mouse_in_ && ph_ > 0 ? mouse_y_ / ph_ * 2 - 1 : 0;
    look_x_ += (tx - look_x_) * std::min(1.0, dt * 2);
    look_y_ += (ty - look_y_) * std::min(1.0, dt * 2);
    // pending top score: the book opens a moment after the result
    if (pending_score_ > 0 && result_ && panel_ == Panel::none && message_t_ > 1.4) {
        if (qualifies(save_.scores, pending_score_)) { open(Panel::name); play("ap_stinger_topscore", .9f); audio_duck_music(1); }
        else pending_score_ = 0;
    }
    {
        const bool on = save_.settings.music && cab_music_ && cab_front_;
        audio_music(visible() && cab_front_ ? "ap_music" : "", on);
    }
    audio_tick(dt);
    save_t_ += dt;
    if (save_t_ > 10 || (dirty_ && save_t_ > 2)) { save_t_ = 0; persist(); }
    gf::Window* win = attached_window();
    const bool hidden = win && (*win).occluded();
    const bool front = !win || (*win).active();
    const bool shown = visible();
    const auto pace = std::chrono::milliseconds(hidden || !shown || !cab_front_ ? 500 : front ? 33 : 100);
    if (timer_ && (*timer_).interval() != pace) (*timer_).set_interval(pace);
    if (hidden || !shown || ch_.r.rgb.empty() || frame_.px.empty()) return;
    if (!game_text_) {
        game_text_ = std::make_unique<games::GameText>(
            std::filesystem::path(games::asset_directory()) / "fonts", "BarlowCondensed");
    }
    if (!rendering_pending_) {
        // The camera moves only between published frames. Label sizes follow the camera, so a
        // camera that drifts while text is pending would request new masks forever (no frame).
        ch_.look(cab_reduced_ ? 0 : look_x_, cab_reduced_ ? 0 : look_y_);
        capture_render_state();
    if (cab_reduced_) {
        ChamberState displayed = st_;
        displayed.shake = 0;
        displayed.pop.fill(0);
        ch_.render(displayed, 0);
    } else {
        ch_.render(st_, t_);
    }
    }
    (*game_text_).begin();
    compose();
    publish();
}

void AtomProbeView::publish() {
    if (!surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        surface_ = gf::LiveSurface::create(d);
        if (surface_ && attached_window()) direct_ = (*attached_window()).queue_live_surface_presentation(shared_from_this(), surface_);
    }
    if (!surface_) return;
    gf::LiveSurfaceWriteLease lease = (*surface_).try_acquire_write();
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
        if (!(*game_text_).ready()) return;
        const bool published = lease.publish();
        if (published) {
            buttons_ = (*rendering_).buttons_;
            rendering_pending_ = false;
        }
    }
    if (!direct_) invalidate(gf::Dirty::paint);

}

// ------------------------------------------------------------------ input
void AtomProbeView::on_pointer(gf::PointerEvent& e) {
    const gf::Point local = point_from_window(e.position);
    mouse_x_ = local.x / pixel_;
    mouse_y_ = local.y / pixel_;
    mouse_in_ = e.action != gf::PointerAction::leave;
    const bool live = panel_ == Panel::none && reveal_t_ < 0 && !box_.opened();
    if (e.action == gf::PointerAction::move || e.action == gf::PointerAction::leave) {
        hover_ = hit_button();
        const bool over_ui = !hover_.empty();
        st_.hover_port = live && !over_ui && mouse_in_ ? ch_.pick_port(mouse_x_, mouse_y_) : -1;
        st_.hover_cell = live && !over_ui && mouse_in_ && st_.hover_port < 0 ? ch_.pick_cell(mouse_x_, mouse_y_) : -1;
        st_.lever_hover = live && !over_ui && mouse_in_ && ch_.pick_lever(mouse_x_, mouse_y_, st_.lever);
        st_.partner = -1;
        if (st_.hover_port >= 0) {
            const int k = box_.known(st_.hover_port);
            if (k >= 0) {
                const Probe& pr = box_.probes()[static_cast<size_t>(k)];
                if (pr.kind == Outcome::exit && !(shot_.port >= 0 && shot_.pr.port == pr.port)) st_.partner = pr.port == st_.hover_port ? pr.exit : pr.port;
            }
        }
        if (st_.hover_port >= 0 && st_.hover_port != last_hover_port_) play("ap_hover", .25f, .9f + .2f * static_cast<float>(st_.hover_port % 8) / 8);
        last_hover_port_ = st_.hover_port;
        const bool over = over_ui || st_.hover_port >= 0 || st_.hover_cell >= 0 || st_.lever_hover;
        set_cursor(over ? gf::CursorKind::hand : gf::CursorKind::arrow);
    }
    if (e.action == gf::PointerAction::down) {
        activate();
        const std::string h = hit_button();
        if (!h.empty() && e.button == gf::PointerButton::primary) { pressed_ = h; e.handled = true; return; }
        if (!live) { e.handled = true; return; }
        const bool secondary = e.button == gf::PointerButton::secondary ||
                               (static_cast<unsigned>(e.modifiers) & (static_cast<unsigned>(gf::Modifier::control) | static_cast<unsigned>(gf::Modifier::alt)));
        const int port = ch_.pick_port(mouse_x_, mouse_y_);
        if (port >= 0 && !secondary) fire(port);
        else if (const int cell = ch_.pick_cell(mouse_x_, mouse_y_); cell >= 0) mark(cell, secondary);
        else if (ch_.pick_lever(mouse_x_, mouse_y_, st_.lever)) pull_lever();
        e.handled = true;
    } else if (e.action == gf::PointerAction::up) {
        if (!pressed_.empty() && hit_button() == pressed_) action(pressed_);
        pressed_.clear();
        e.handled = true;
    }
}

void AtomProbeView::on_key(gf::KeyEvent& e) {
    if (e.handled) return;
    using K = gf::PhysicalKey;
    if (e.action != gf::KeyAction::down) return;
    const std::uint32_t k = e.physical_key;
    if (panel_ == Panel::name) {
        if (k == K::backspace && !name_entry_.empty()) { name_entry_.pop_back(); play("ui_name_backspace", .6f); }
        else if (k == K::enter) action("save_name");
        else if (k == K::escape) { pending_score_ = 0; open(Panel::none); }
        e.handled = true;
        return;
    }
    if (k == K::enter || k == K::space) {
        if (panel_ != Panel::none) open(Panel::none);
        else if (result_) action("new");
        else pull_lever();
        e.handled = true;
        return;
    }
    if (k == K::escape) { if (panel_ != Panel::none) open(Panel::none); e.handled = true; return; }
    if (k == K::f1) { open(panel_ == Panel::help ? Panel::none : Panel::help); e.handled = true; return; }
    if (k == K::t) { open(panel_ == Panel::scores ? Panel::none : Panel::scores); e.handled = true; return; }
    if (k == K::n) { action("new"); e.handled = true; return; }
    if (k == K::m) { action("music"); e.handled = true; return; }
    if (k == K::s) { action("sound"); e.handled = true; return; }
}

void AtomProbeView::on_text_input(gf::TextInputEvent& e) {
    if (panel_ != Panel::name) return;
    for (char c : e.text_utf8) {
        if (name_entry_.size() >= 12) break;
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '.') {
            name_entry_ += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            play("ui_name_key_0" + std::to_string(1 + std::rand() % 3), .6f);
        }
    }
    e.handled = true;
}

void AtomProbeView::action(const std::string& id) {
    if (id == "close") open(Panel::none);
    else if (id == "new") {
        if (reveal_t_ >= 0 && !result_) return;  // let the reveal finish
        open(Panel::none);
        new_box();
        play("ap_boot", .8f);
        layout_buttons();
    }
    else if (id == "help") open(Panel::help);
    else if (id == "scores") open(Panel::scores);
    else if (id == "open") pull_lever();
    else if (id == "sound") { save_.settings.sound = !save_.settings.sound; dirty_ = true; layout_buttons(); }
    else if (id == "music") { save_.settings.music = !save_.settings.music; dirty_ = true; layout_buttons(); }
    else if (id == "save_name") {
        TopScore t;
        t.name = name_entry_.empty() ? "PROBER" : name_entry_;
        t.points = pending_score_;
        t.when = wall_clock();
        add_score(save_.scores, t);
        save_.settings.player_name = t.name;
        pending_score_ = 0;
        play("ui_name_confirm", .8f);
        persist();
        open(Panel::scores);
    }
}

void AtomProbeView::open(Panel p) {
    panel_ = p;
    pressed_.clear();
    layout_buttons();
}

// ------------------------------------------------------------------ drawing
games::TextImage AtomProbeView::tmask(const std::string& s, bool bold, double size, int wrap_game) {
    games::TextImage image = (*game_text_).get(s, bold, size,
        wrap_game > 0 ? wrap_game * pixel_ : 0, bs_);
    return image;
}
int AtomProbeView::text(const std::string& s, int x, int y, Col c, double size, bool bold, int wrap) {
    texts_.push_back({s, bold, size, wrap, x, y, c});
    return text_w(s, size, bold);
}
int AtomProbeView::text_w(const std::string& s, double size, bool bold) {
    return static_cast<int>(std::ceil(tmask(s, bold, size, 0).w / static_cast<double>(pixel_)));
}
int AtomProbeView::text_h(const std::string& s, double size, bool bold, int wrap) {
    return static_cast<int>(std::ceil(tmask(s, bold, size, wrap).h / static_cast<double>(pixel_)));
}

void AtomProbeView::layout_render_buttons() {
    (*rendering_).buttons_.clear();
    if (pw_ <= 0) return;
    if ((*rendering_).panel_ == Panel::none) {
        const int bh = 13, y = 5;
        int x = 6;
        auto add = [&](const std::string& id, const std::string& label) {
            const int w = text_w(label, 11, true) + 12;
            (*rendering_).buttons_.push_back({id, label, x, y, w, bh});
            x += w + 4;
        };
        if (!opt_.hosted) {
            add("new", "New box");
            add("help", "Help");
            add("scores", "Top scores");
            add("music", (*rendering_).save_.settings.music ? "Music on" : "Music off");
            add("sound", (*rendering_).save_.settings.sound ? "Sound on" : "Sound off");
        }
        if ((*rendering_).result_) {
            // over the console once the box is open
            double cx, cy;
            ch_.to_screen(Chamber::lever_pivot() + V3{0, -.4, .8}, cx, cy);
            const std::string label = "New box";
            const int w = text_w(label, 13, true) + 22;
            (*rendering_).buttons_.push_back({"new", label, static_cast<int>(cx) - w / 2, static_cast<int>(cy) + 6, w, 18, true});
        }
        return;
    }
    const int ww = std::min(pw_ - 30, 380), wh = std::min(ph_ - 30, 280);
    const int wx = (pw_ - ww) / 2, wy = (ph_ - wh) / 2;
    const std::string label = (*rendering_).panel_ == Panel::name ? "Sign the log" : "Close";
    const int w = text_w(label, 11, true) + 16;
    (*rendering_).buttons_.push_back({(*rendering_).panel_ == Panel::name ? "save_name" : "close", label, wx + ww - w - 10, wy + wh - 20, w, 14});
}

void AtomProbeView::draw_button(const Button& b) {
    const bool down = (*rendering_).pressed_ == b.id, over = (*rendering_).hover_ == b.id;
    frame_.fill_rect(b.x + 1, b.y + 1, b.w, b.h, hex(0x000000, .5f));
    frame_.begin(); frame_.rect(b.x, b.y + (down ? 1 : 0), b.w, b.h); frame_.fill(b.big ? (over ? hex(0x1E6A4A) : hex(0x124430)) : over ? hex(0x16303C) : kPanel);
    frame_.begin(); frame_.rect(b.x + .5, b.y + .5 + (down ? 1 : 0), b.w - 1, b.h - 1); frame_.stroke(b.big ? kGood : over ? kCyan : kCyanDim, 1);
    const double size = b.big ? 13 : 11;
    const int tw = text_w(b.label, size, true), th = text_h(b.label, size, true);
    text(b.label, b.x + (b.w - tw) / 2, b.y + (b.h - th) / 2 + (down ? 1 : 0), b.big ? hex(0xD8FFE6) : over ? hex(0xE8FDFF) : kText, size, true);
}

// Words on the scene: the answers on the emitter plates, the console's labels, the status line.
void AtomProbeView::draw_overlay() {
    for (int p = 0; p < kPorts; ++p) {
        const PortView& v = (*rendering_).st_.ports[static_cast<size_t>(p)];
        if (!v.kind) continue;
        double x, y, ex, ey;
        ch_.to_screen(Chamber::plate(p), x, y);
        ch_.to_screen(Chamber::plate(p) + ch_.r.right() * .24, ex, ey);
        const std::string g = v.kind == 1 ? "H" : v.kind == 2 ? "R" : std::to_string(v.pair);
        // sized to the cap as the camera sees it
        const double size = std::round(std::clamp(std::hypot(ex - x, ey - y) * (g.size() > 1 ? 1.9 : 2.5), 8.0, 18.0) * 2) / 2;
        const int w = text_w(g, size, true), h = text_h(g, size, true);
        text(g, static_cast<int>(std::lround(x - w / 2.0)), static_cast<int>(std::lround(y - h / 2.0)), kInk, size, true);
    }
    double x, y;
    if (!(*rendering_).result_) {
        ch_.to_screen(Chamber::readout() + V3{0, .78, 0}, x, y);
        text("POINTS", static_cast<int>(x) - text_w("POINTS", 10, true) / 2, static_cast<int>(y) - 6, kCyanDim, 10, true);
        ch_.to_screen(Chamber::tray(0) + V3{.75, .5, 0}, x, y);
        text("MARKERS", static_cast<int>(x) - text_w("MARKERS", 10, true) / 2, static_cast<int>(y) - 6, kCyanDim, 10, true);
        ch_.to_screen(Chamber::lever_pivot() + V3{0, -1.6, 0}, x, y);
        const Col oc = (*rendering_).st_.lever_ready ? kInk : hex(0xC08088);
        text("OPEN", static_cast<int>(x) - text_w("OPEN", 10, true) / 2, static_cast<int>(y) - 6, oc, 10, true);
    }
    // the status line, top right of the buttons
    if (!(*rendering_).message_.empty() && (*rendering_).message_t_ < 6) {
        const float a = static_cast<float>(std::clamp(6 - (*rendering_).message_t_, 0.0, 1.0));
        const double size = 14;
        const int w = text_w((*rendering_).message_, size, true);
        const int mx = std::max(pw_ / 2 - w / 2 - 40, 270);
        text((*rendering_).message_, mx, 5, alpha((*rendering_).message_col_, a), size, true);
    }
}

void AtomProbeView::draw_result() {
    if (!(*rendering_).result_) return;
    // a card over the console: what was found and what it cost
    // fitted to the console's top, clear of the emitters
    double lx, ly, rx, ry;
    ch_.to_screen({Chamber::kConsoleX0 + .15, Chamber::kConsoleY1, .8}, lx, ly);
    ch_.to_screen({Chamber::kConsoleX1 - .05, Chamber::kConsoleY1, .8}, rx, ry);
    const int w = std::max(110, static_cast<int>(rx - lx)), h = 74;
    const int x = std::clamp(static_cast<int>(lx), 4, pw_ - w - 4), y = std::max(24, static_cast<int>(std::min(ly, ry)) - 4);
    const bool solved = (*rendering_).box_.found() == kAtoms;
    frame_.fill_rect(x + 2, y + 2, w, h, hex(0x000000, .5f));
    frame_.fill_rect(x, y, w, h, hex(0x081016, .94f));
    frame_.begin(); frame_.rect(x + .5, y + .5, w - 1, h - 1); frame_.stroke(solved ? kGood : kBad, 1);
    const std::string head = solved ? "ALL FOUR FOUND" : std::to_string((*rendering_).box_.found()) + " OF 4 FOUND";
    text(head, x + (w - text_w(head, 13, true)) / 2, y + 5, solved ? kGood : kBad, 13, true);
    const std::string pts = std::to_string((*rendering_).box_.points()) + " points";
    text(pts, x + (w - text_w(pts, 18, true)) / 2, y + 22, kGold, 18, true);
    const std::string sub = solved ? std::to_string((*rendering_).box_.probes().size()) + ((*rendering_).box_.probes().size() == 1 ? " beam fired" : " beams fired")
                                   : "+" + std::to_string(kMissPenalty * (*rendering_).box_.missed()) + " for missed atoms = " + std::to_string((*rendering_).box_.total());
    text(sub, x + (w - text_w(sub, 10, false)) / 2, y + 46, kText, 10, false);
}

void AtomProbeView::draw_panel() {
    if ((*rendering_).panel_ == Panel::none) return;
    frame_.fill_rect(0, 0, pw_, ph_, hex(0x000000, .5f));
    const int ww = std::min(pw_ - 30, 380), wh = std::min(ph_ - 30, 280);
    const int wx = (pw_ - ww) / 2, wy = (ph_ - wh) / 2;
    frame_.fill_rect(wx + 3, wy + 3, ww, wh, hex(0x000000, .6f));
    frame_.fill_rect(wx, wy, ww, wh, hex(0x0A141C));
    frame_.begin(); frame_.rect(wx + .5, wy + .5, ww - 1, wh - 1); frame_.stroke(kCyan, 1);
    frame_.begin(); frame_.rect(wx + 3.5, wy + 3.5, ww - 7, wh - 7); frame_.stroke(kCyanDim, 1);
    int ly = wy + 32;
    auto line = [&](const std::string& t, Col c = kText, bool bold = false, double size = 11.5) {
        text(t, wx + 14, ly, c, size, bold, ww - 28);
        ly += text_h(t, size, bold, ww - 28) + 3;
    };
    auto title = [&](const std::string& t) { text(t, wx + (ww - text_w(t, 16, true)) / 2, wy + 9, kCyan, 16, true); };
    switch ((*rendering_).panel_) {
        case Panel::help:
            title("Atom Probe");
            line("Four atoms hide in the fogged chamber. Click an emitter on the rim to fire a beam into the box, and watch where it comes out.");
            line("Straight into an atom: the beam is absorbed. H", hex(0xFF8A9C));
            line("Passing an atom diagonally ahead: it turns 90 degrees away. Two at once send it straight back. R", kText);
            line("An atom beside the square it would enter turns it back at the door. R", kText);
            line("Otherwise it comes out somewhere else: both ends show the same number.", hex(0x8AD8FF));
            line("Click a square on the glass to place a marker; right-click (or Control-click) to cross a square out. With four markers placed, pull the lever.");
            line("Points: 1 for each H or R, 2 for each detour. Find all four atoms in the fewest points. Every box can be worked out exactly.");
            line("Keys: Enter opens the box   N new box   T top scores   M music   S sound   F1 help", kCyanDim, true, 10);
            break;
        case Panel::scores:
            title("The Lab Log");
            if ((*rendering_).save_.scores.empty()) line("No box solved yet.", kCyanDim);
            for (size_t i = 0; i < (*rendering_).save_.scores.size(); ++i) {
                const TopScore& t = (*rendering_).save_.scores[i];
                char buf[64];
                std::snprintf(buf, sizeof buf, "%2zu.  %-14s  %d %s", i + 1, t.name.c_str(), t.points, t.points == 1 ? "point" : "points");
                line(buf, i == 0 ? kGold : kText, i == 0, 12);
            }
            break;
        case Panel::name:
            title("The Lab Log");
            line("All four atoms found for " + std::to_string((*rendering_).pending_score_) + ((*rendering_).pending_score_ == 1 ? " point." : " points."), kGood, true, 13);
            line("Sign the log:");
            line((*rendering_).name_entry_ + (std::sin((*rendering_).t_ * 6) > 0 ? "_" : " "), kGold, true, 15);
            break;
        case Panel::none: break;
    }
    for (const Button& b : (*rendering_).buttons_) draw_button(b);
}

void AtomProbeView::compose() {
    texts_.clear();
    layout_render_buttons();
    ch_.r.present(frame_, 1, 0, 0, true);
    // text sits on its own layer above the frame: nothing from the scene may show through a dialog
    if ((*rendering_).panel_ == Panel::none) {
        draw_overlay();
        draw_result();
    }
    if ((*rendering_).panel_ == Panel::none) for (const Button& b : (*rendering_).buttons_) draw_button(b);
    draw_panel();
}

void AtomProbeView::blit_texts(std::uint32_t* dst, size_t stride_px, double k) {
    std::span<std::uint32_t> pixels(dst, stride_px * static_cast<std::size_t>(phys_h_));
    for (const HiText& h : texts_) {
        const games::TextImage image = tmask(h.s, h.bold, h.size, h.wrap);
        games::blit_game_text(pixels, phys_w_, phys_h_, stride_px, image,
            static_cast<int>(h.x * k), static_cast<int>(h.y * k),
            h.c.r, h.c.g, h.c.b, h.c.a);
    }
}

void AtomProbeView::layout_buttons() {
    // New panel, size or settings invalidate a pending attempt, never the
    // currently displayed pixels. Hit regions follow the next published frame.
    rendering_pending_ = false;
    buttons_.clear();
    if (game_text_) (*game_text_).cancel();
}

void AtomProbeView::capture_render_state() {
    if (!rendering_) rendering_ = std::make_unique<RenderState>();
    rendering_pending_ = true;
    RenderState& state = *rendering_;
    state.box_ = box_;
    state.save_ = save_;
    state.st_ = st_;
    state.message_ = message_;
    state.message_col_ = message_col_;
    state.message_t_ = message_t_;
    state.name_entry_ = name_entry_;
    state.pressed_ = pressed_;
    state.hover_ = hover_;
    state.panel_ = panel_;
    state.result_ = result_;
    state.pending_score_ = pending_score_;
    state.t_ = t_;
}

void AtomProbeView::host_command(const std::string& id) {
    if (id == "new") { action("new"); return; }
    const Panel wanted = id == "help" ? Panel::help : id == "scores" ? Panel::scores : Panel::none;
    if (wanted == Panel::none || panel_ == Panel::name) return;
    open(panel_ == wanted ? Panel::none : wanted);
}

std::string AtomProbeView::host_panel() const {
    return panel_ == Panel::help ? "help" : panel_ == Panel::scores ? "scores" : "";
}

}  // namespace ap
