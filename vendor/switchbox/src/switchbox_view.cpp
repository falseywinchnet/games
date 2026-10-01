#include "switchbox_view.hpp"

#include "platform/audio.hpp"
#include "platform/text.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sbx {

namespace {
const Col kInk = hex(0x4A2C40), kPaper = hex(0xFFFDF8), kPink = hex(0xF07EA0), kPinkSoft = hex(0xFFD6E2), kMint = hex(0xBFE8CF);
const char* kMusic = "music_switchbox_loop";

std::uint64_t seed_now() {
    return static_cast<std::uint64_t>(wall_clock() * 1000) ^ 0x5B0C5EEDULL;
}
}  // namespace

SwitchboxView::SwitchboxView(gf::StableId id, Options opt)
    : Control(std::move(id)), opt_(opt), puzzle_(seed_now()), actor_(seed_now() ^ 0xA5A5) {
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    {
        gf::SurfaceMaterial none;
        none.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(0, 0, 0))};
        set_authored_surface_material(none);
    }
    set_accessible_name("Switchbox. A girl lives in a box of six switches and flips them back down. Find the order that lights "
                        "all six lamps. Click a switch or press 1 to 6. F1 help, T top scores, M music, N sound.");
    lines_.load(asset_dir() + "/lines/switchbox");
    if (load_save(save_path(opt_.dev), save_) && save_.has_puzzle) static_cast<void>(puzzle_.restore(save_.puzzle));
    for (int i = 0; i < kSwitches; ++i) {
        SwitchVis& v = st_.sw[static_cast<size_t>(i)];
        v.sink = puzzle_.present(i) ? 0 : 1;
        v.lamp = puzzle_.lamp(i) ? 1 : 0;
        v.on = puzzle_.lamp(i) ? 1 : 0;  // lit switches stay up
    }
    name_entry_ = save_.settings.player_name;
    if (const char* sc = std::getenv("SBX_SCRIPT"); sc && opt_.dev) {
        std::string all = sc;
        size_t pos = 0;
        while (pos < all.size()) {
            size_t end = all.find(',', pos);
            if (end == std::string::npos) end = all.size();
            const std::string item = all.substr(pos, end - pos);
            const size_t colon = item.find(':');
            if (colon != std::string::npos) script_.push_back({std::atof(item.c_str()), std::atoi(item.c_str() + colon + 1)});
            pos = end + 1;
        }
    }
}

void SwitchboxView::on_attached_to_window() {
    audio_start(asset_dir());
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(33));
    subs_.push_back((*timer_).tick().subscribe(*this, gf::Delegate<>::bind<SwitchboxView, &SwitchboxView::tick>(*this)));
    subs_.push_back((*attached_window()).active_changed().subscribe(
        *this, gf::Delegate<bool>::bind<SwitchboxView, &SwitchboxView::cursor_active_changed>(*this)));
    subs_.push_back((*attached_window()).pointer_capture_changed().subscribe(
        *this, gf::Delegate<const gf::PointerCaptureChange&>::bind<SwitchboxView, &SwitchboxView::cursor_capture_changed>(*this)));
    last_ = std::chrono::steady_clock::now();
    (*timer_).start();
}

void SwitchboxView::on_detaching_from_window(gf::Window&) noexcept {
    try { persist(); } catch (...) {}
    subs_.clear();
    cancel_cursor_interaction();
    if (timer_) (*timer_).stop();
    timer_.reset();
    audio_stop();
}

void SwitchboxView::activate() {
    if (attached_window()) static_cast<void>((*attached_window()).request_focus(shared_from_this()));
}

void SwitchboxView::set_cabinet(bool foreground, bool music, bool sound, bool reduced_motion) {
    cab_front_ = foreground;
    cab_music_ = music;
    cab_sound_ = sound;
    cab_reduced_ = reduced_motion;
    audio_cabinet(foreground, music, sound);
    if (!foreground) {
        cancel_cursor_interaction();
    }
    audio_music(foreground ? kMusic : "", foreground && music && save_.settings.music);
}

void SwitchboxView::cancel_cursor_interaction() {
    hold_sw_ = -1;
    hold_blocked_ = true;
    mouse_down_ = false;
    st_.cursor = false;
    cursor_hidden_ = false;
    actor_.cancel_pointer_interaction();
    const gf::CursorStatus released = cursor_lease_.release();
    if (!released.accepted() && released.error == gf::CursorError::native_failure) {
        std::fprintf(stderr, "Switchbox cursor restoration failed.\n");
    }
    set_pointer_capture(false);
}

void SwitchboxView::on_focus_changed(bool focused) {
    if (!focused) { cancel_cursor_interaction(); }
}

void SwitchboxView::cursor_active_changed(bool active) {
    if (!active) { cancel_cursor_interaction(); }
}

void SwitchboxView::cursor_capture_changed(const gf::PointerCaptureChange& change) {
    const bool owns_capture = change.captured && change.control_id == runtime_id();
    if (!owns_capture && (mouse_down_ || cursor_hidden_)) { cancel_cursor_interaction(); }
}

void SwitchboxView::cursor_refused(gf::CursorStatus status) {
    cancel_cursor_interaction();
    std::fprintf(stderr, "Switchbox pointer interaction refused: %d\n", static_cast<int>(status.error));
    say_text("The pointer stays with you here.");
}

void SwitchboxView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    pw_ = std::max(64, static_cast<int>(std::ceil(bounds.width / pixel_)));
    ph_ = std::max(64, static_cast<int>(std::ceil(bounds.height / pixel_)));
    frame_.resize(pw_, ph_);
    stage_.resize(pw_, ph_);
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

void SwitchboxView::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    if (surface_ && !direct_) p.draw_live_surface(surface_, b);
    else p.fill_rect(b, gf::Color::rgba(251, 227, 230));
}

// ------------------------------------------------------------------ game
void SwitchboxView::play(const std::string& name, float gain, float rate) {
    audio_sfx(name, gain, rate, save_.settings.sound && cab_sound_ && cab_front_ && visible());
}

void SwitchboxView::flip(int sw) {
    if (sw < 0 || sw >= kSwitches || panel_ != Panel::none || mole_.active() || mole_pending_ >= 0 || mole_after_ > 0) return;
    SwitchVis& v = st_.sw[static_cast<size_t>(sw)];
    if (!puzzle_.present(sw) || v.sink > .05) return;
    if (v.on > .01 || puzzle_.solved() || actor_.celebrating()) {  // still up, or she is busy celebrating
        v.wiggle = 1;
        play("switchbox_switch_on_01", .35f, .7f);
        return;
    }
    const FlipResult r = puzzle_.flip(sw);
    if (!r.accepted) { v.wiggle = 1; return; }
    v.on = 1;
    play("switchbox_switch_on_0" + std::to_string(1 + std::rand() % 3), .9f, .95f + .1f * static_cast<float>(std::rand() % 10) / 10);
    actor_.flipped(sw, r.correct, r.solved);
    // the same switch over and over: on the fourth in a row she takes it away
    same_count_ = sw == same_sw_ ? same_count_ + 1 : 1;
    same_sw_ = sw;
    if (!r.correct && same_count_ >= 4 && actor_.stolen() < 0 && r.reaction == Reaction::none) {
        actor_.steal(sw);
        same_count_ = 0;
    }
    if (!r.correct) for (double& d : lamp_delay_) d = 0;
    if (r.reaction != Reaction::none) react(r.reaction);
    if (r.solved) {
        last_steps_ = puzzle_.steps();
        pending_score_ = last_steps_;
        ++save_.cracked;
    }
    dirty_ = true;
}

void SwitchboxView::react(Reaction r) {
    actor_.react(r, puzzle_.first());
    switch (r) {
        case Reaction::hint_first: say("hint"); break;
        case Reaction::forgot: say("forgot"); break;
        case Reaction::scold: say("scold"); break;
        case Reaction::rip_out: say("rip_out"); break;
        case Reaction::restore: say("restore"); break;
        case Reaction::stuck_scold: say("stuck"); break;
        case Reaction::stuck_grumble: if (std::rand() % 2) say("grumble"); break;
        case Reaction::none: break;
    }
}

void SwitchboxView::say_text(const std::string& txt, bool special) {
    if (txt.empty()) return;
    bubble_ = Bubble{};
    bubble_.text = txt;
    bubble_.dur = special ? 7 : std::clamp(1.6 + txt.size() * .065, 2.6, 6.0);
    bubble_.muffled = actor_.hidden() && !special;
    bubble_.special = special;
}

void SwitchboxView::say(const std::string& category) { say_text(lines_.pick(category)); }

void SwitchboxView::persist() {
    save_.puzzle = puzzle_.state();
    save_.has_puzzle = !puzzle_.solved();
    write_save(save_path(opt_.dev), save_);
    dirty_ = false;
}

void SwitchboxView::tick() {
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - last_).count(), 0.0, .25);
    last_ = now;
    t_ += dt;
    while (!script_.empty() && script_.front().first <= t_) {
        const int sw = script_.front().second;
        script_.erase(script_.begin());
        if (sw == -1) actor_.head_clicked();
        else if (sw == 9) {  // some wrong switch that is down
            for (int k = 0; k < kSwitches; ++k)
                if (k != puzzle_.combination()[static_cast<size_t>(puzzle_.progress())] && st_.sw[static_cast<size_t>(k)].on <= 0 && st_.sw[static_cast<size_t>(k)].sink <= 0) { flip(k); break; }
        }
        else if (sw == 10) actor_.hole_clicked(actor_.stolen());
        else if (sw == 8) { for (int i = 0; i < kSwitches; ++i) if (mole_.hit(i)) { st_.sw[static_cast<size_t>(i)].wiggle = 1; break; } }
        else if (sw == 7) flip(puzzle_.combination()[static_cast<size_t>(std::min(puzzle_.progress(), kSwitches - 1))]);
        else flip(sw);
    }
    // her eyes follow the cursor over the box
    {
        double wx = 0, wy = 0;
        const bool ok = mouse_in_ && stage_.r.unproject_ground(mouse_x_, mouse_y_, Stage::kBoxZ, wx, wy);
        actor_.set_cursor({wx, wy, Stage::kBoxZ}, ok && std::fabs(wx) < 4 && wy > -3 && wy < 2);
    }
    actor_.set_hold(hold_blocked_ ? -1 : hold_sw_);
    actor_.update(dt, st_);
    mischief(dt);
    mole_tick(dt);
    for (const Cue& c : actor_.cues) {
        if (c.kind == Cue::sound) {
            if (c.name.rfind("stinger", 0) == 0) audio_duck_music(.9f);
            play(c.name, c.gain, c.rate);
        } else if (c.kind == Cue::special) {
            say_text(lines_.pick(c.name), true);
        } else {
            say(c.name);
        }
    }
    actor_.cues.clear();
    if (actor_.take_reset_request()) {
        puzzle_.new_combination();
        for (int i = 0; i < kSwitches; ++i) lamp_delay_[static_cast<size_t>(i)] = .12 * i;
        if (pending_score_ > 0 && qualifies(save_.scores, pending_score_)) {
            open(Panel::name);
            play("stinger_topscore_switchbox", .9f);
            audio_duck_music(.9f);
        }
        pending_score_ = 0;
        dirty_ = true;
    }
    for (int i = 0; i < kSwitches && !mole_.active(); ++i) {
        SwitchVis& v = st_.sw[static_cast<size_t>(i)];
        const double want = puzzle_.lamp(i) ? 1 : 0;
        if (want > v.lamp) v.lamp = std::min(want, v.lamp + dt * 9);
        else if (lamp_delay_[static_cast<size_t>(i)] > 0) lamp_delay_[static_cast<size_t>(i)] -= dt;
        else v.lamp = std::max(want, v.lamp - dt * 4);
        v.wiggle = std::max(0.0, v.wiggle - dt * 4);
    }
    // speech typing and babble: muffled while she is inside the box
    if (!bubble_.text.empty()) {
        const int before = bubble_.shown;
        bubble_.age += dt;
        bubble_.shown = std::min(static_cast<int>(bubble_.text.size()), static_cast<int>(bubble_.age * 30));
        if (bubble_.shown / 3 != before / 3 && bubble_.shown < static_cast<int>(bubble_.text.size()) && bubble_.text[static_cast<size_t>(bubble_.shown)] != ' ') {
            const float rate = (bubble_.muffled ? .85f : 1.05f) + .25f * static_cast<float>(std::rand() % 100) / 100;
            play("switchbox_babble_0" + std::to_string(1 + std::rand() % 6), bubble_.muffled ? .35f : .55f, rate);
        }
        if (bubble_.age > bubble_.dur) bubble_ = Bubble{};
    }
    // one call per tick: switching tracks back and forth would restart the loop
    audio_music(visible() && cab_front_ ? kMusic : "", save_.settings.music && cab_music_ && cab_front_);
    audio_tick(dt);
    save_t_ += dt;
    if (save_t_ > 10 || (dirty_ && save_t_ > 2)) { save_t_ = 0; persist(); }
    // pacing: smooth while the window is in front, slow in the background, nothing while hidden
    gf::Window* win = attached_window();
    const bool hidden = win && (*win).occluded();
    const bool front = !win || (*win).active();
    const bool shown = visible();
    const auto pace = std::chrono::milliseconds(hidden || !shown || !cab_front_ ? 500 : front ? 33 : 100);
    if (timer_ && (*timer_).interval() != pace) (*timer_).set_interval(pace);
    if (!shown || !cab_front_) {
        cancel_cursor_interaction();
    }
    if (hidden || !shown || stage_.r.rgb.empty() || frame_.px.empty()) return;
    StageState displayed = st_;
    const bool quiet = cab_reduced_ || save_.settings.reduced_motion;
    if (quiet) {
        displayed.girl.hair_lag = {};
        displayed.girl.ahoge = 0;
        displayed.girl.shake = 0;
        displayed.fx.hearts = displayed.fx.sparkle = displayed.fx.steam = 0;
        for (SwitchVis& lever : displayed.sw) { lever.wiggle = 0; }
    }
    stage_.render(displayed, quiet ? 0 : t_);
    compose();
    publish();
}

void SwitchboxView::publish() {
    if (!surface_) {  // Shared cross-platform presentation
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

// Holding a switch (her hand on the pointer), the pointer she carries off, and
// the system cursor that hides while the game draws its own.
void SwitchboxView::mischief(double) {
    gf::Window* window = attached_window();
    if (!cab_front_ || !visible() || window == nullptr || !(*window).active()) {
        cancel_cursor_interaction();
        return;
    }
    if (cursor_hidden_ && cursor_lease_.snapshot().phase != gf::CursorLeasePhase::active) {
        cancel_cursor_interaction();
        return;
    }
    const bool forced_release = actor_.take_forced_release();
    if (forced_release) { hold_blocked_ = true; }
    V3 drop{};
    const bool dropped = actor_.take_pointer_drop(drop);
    if (dropped) {
        const gf::CursorMetrics metrics = (*window).cursor_metrics();
        double sx{}, sy{};
        stage_.to_screen(drop, sx, sy);
        const gf::Rect bounds = absolute_bounds();
        const gf::CursorStatus placed = (*window).warp_cursor(
            metrics, {bounds.x + sx * pixel_, bounds.y + sy * pixel_});
        if (!placed.accepted()) { cursor_refused(placed); return; }
        mouse_x_ = sx;
        mouse_y_ = sy;
        hold_blocked_ = true;
    }
    st_.cursor = false;
    if (actor_.carrying_pointer()) {
        st_.cursor = true;
        st_.cursor_at = actor_.pointer_at();
    } else if (hold_sw_ >= 0 && !hold_blocked_ && actor_.resting_on_hold()) {
        st_.cursor = true;
        st_.cursor_at = Stage::knob(hold_sw_, st_.sw[static_cast<size_t>(hold_sw_)].on) + V3{0, -.06, .09};
    }
    if (st_.cursor && !cursor_hidden_) {
        gf::CursorLeaseResult hidden = (*window).begin_cursor_hidden();
        if (!hidden.status.accepted()) { cursor_refused(hidden.status); return; }
        cursor_lease_ = std::move(hidden.lease);
        cursor_hidden_ = true;
    } else if (!st_.cursor && cursor_hidden_) {
        const gf::CursorStatus released = cursor_lease_.release();
        cursor_hidden_ = false;
        if (!released.accepted()) { cursor_refused(released); }
    }
}
// Whack-a-mole: she hides, the switches drop into their sockets and pop up at
// random for thirty seconds; then they settle back exactly as they were.
void SwitchboxView::mole_tick(double dt) {
    if (actor_.take_mole_request()) {
        mole_pending_ = 1.1;
        hold_sw_ = -1;
    }
    if (mole_pending_ >= 0) {
        mole_pending_ -= dt;
        if (mole_pending_ < 0) {
            mole_.start(static_cast<std::uint64_t>(t_ * 1000) + 17);
            actor_.mole_begin();
            say("mole_go");
        }
    }
    if (mole_.active() || mole_.finished()) {
        mole_.update(dt);
        int watch = -1;
        for (int i = 0; i < kSwitches; ++i) {
            SwitchVis& v = st_.sw[static_cast<size_t>(i)];
            const bool up = mole_.up(i);
            if (up) watch = i;
            const double want = up ? 0 : 1;
            v.sink += (want - v.sink) * std::min(1.0, dt * (up ? 16 : 11));
            v.on = 0;
            v.lamp = std::max(up ? 1.0 : 0.0, v.lamp - dt * 5);
        }
        actor_.mole_watch(watch);
        if (mole_.finished()) {
            result_hits_ = mole_.hits();
            result_t_ = 4;
            actor_.mole_end(result_hits_ >= Mole::kGreat);
            mole_after_ = 1.2;
        }
    } else if (mole_after_ > 0) {
        mole_after_ -= dt;
        for (int i = 0; i < kSwitches; ++i) {
            SwitchVis& v = st_.sw[static_cast<size_t>(i)];
            const double want_sink = puzzle_.present(i) && i != actor_.stolen() ? 0 : 1;
            v.sink += (want_sink - v.sink) * std::min(1.0, dt * 8);
            v.on += ((puzzle_.lamp(i) ? 1.0 : 0.0) - v.on) * std::min(1.0, dt * 8);
            if (mole_after_ <= 0) { v.sink = want_sink; v.on = puzzle_.lamp(i) ? 1 : 0; }
        }
    }
    result_t_ = std::max(0.0, result_t_ - dt);
}

// ------------------------------------------------------------------ input
void SwitchboxView::on_pointer(gf::PointerEvent& e) {
    const gf::Point local = point_from_window(e.position);
    mouse_x_ = local.x / pixel_;
    mouse_y_ = local.y / pixel_;
    mouse_in_ = e.action != gf::PointerAction::leave;
    auto hit = [&]() -> std::string {
        for (const Button& b : buttons_)
            if (mouse_x_ >= b.x && mouse_x_ < b.x + b.w && mouse_y_ >= b.y && mouse_y_ < b.y + b.h) return b.id;
        return {};
    };
    if (e.action == gf::PointerAction::move) {
        hover_ = hit();
        // dragging off the switch ends the hold
        if (hold_sw_ >= 0 && stage_.pick_socket(mouse_x_, mouse_y_) != hold_sw_ && stage_.pick_switch(mouse_x_, mouse_y_, st_) != hold_sw_) hold_sw_ = -1;
        const bool over = !hover_.empty() || stage_.pick_switch(mouse_x_, mouse_y_, st_) >= 0 || stage_.pick_socket(mouse_x_, mouse_y_) >= 0 ||
                          (!actor_.hidden() && stage_.pick_head(mouse_x_, mouse_y_, st_.girl));
        set_cursor(over ? gf::CursorKind::hand : gf::CursorKind::arrow);
    }
    if (e.action == gf::PointerAction::down && e.button == gf::PointerButton::primary) {
        activate();
        const std::string h = hit();
        if (!h.empty()) { pressed_ = h; e.handled = true; return; }
        if (panel_ != Panel::none) { e.handled = true; return; }
        mouse_down_ = true;
        set_pointer_capture(true);
        if (mole_.active()) {
            const int s = stage_.pick_socket(mouse_x_, mouse_y_);
            if (s >= 0 && mole_.hit(s)) {
                st_.sw[static_cast<size_t>(s)].wiggle = 1;
                play("switchbox_switch_off_0" + std::to_string(1 + std::rand() % 3), 1, 1.35f);
                if (mole_.hits() % 10 == 0) play("switchbox_giggle_boing", .5f, 1.3f);
            }
            e.handled = true;
            return;
        }
        const int sw = stage_.pick_switch(mouse_x_, mouse_y_, st_);
        if (sw >= 0) {
            flip(sw);
            hold_sw_ = sw;
            hold_blocked_ = false;
        } else if (const int hole = stage_.pick_socket(mouse_x_, mouse_y_); hole >= 0 && hole == actor_.stolen()) {
            actor_.hole_clicked(hole);
        } else if (stage_.pick_head(mouse_x_, mouse_y_, st_.girl)) {
            actor_.head_clicked();
        }
        e.handled = true;
    } else if (e.action == gf::PointerAction::up) {
        if (!pressed_.empty() && hit() == pressed_) action(pressed_);
        pressed_.clear();
        if (mouse_down_) set_pointer_capture(false);
        mouse_down_ = false;
        hold_sw_ = -1;
        hold_blocked_ = false;
        e.handled = true;
    }
}

void SwitchboxView::on_key(gf::KeyEvent& e) {
    if (e.handled) return;
    using K = gf::PhysicalKey;
    if (e.action != gf::KeyAction::down) return;
    const std::uint32_t k = e.physical_key;
    if (panel_ == Panel::name) {
        if (k == K::backspace && !name_entry_.empty()) { name_entry_.pop_back(); play("ui_name_backspace", .6f); }
        else if (k == K::enter) action("save_name");
        else if (k == K::escape) open(Panel::none);
        e.handled = true;
        return;
    }
    const std::uint32_t digits[kSwitches] = {0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23};  // HID usages for 1..6
    for (int i = 0; i < kSwitches; ++i)
        if (k == digits[i]) { if (!e.repeat) flip(i); e.handled = true; return; }
    if (k == K::escape) { if (panel_ != Panel::none) open(Panel::none); e.handled = true; return; }
    if (k == K::f1) { open(panel_ == Panel::help ? Panel::none : Panel::help); e.handled = true; return; }
    if (k == K::t) { open(panel_ == Panel::scores ? Panel::none : Panel::scores); e.handled = true; return; }
    if (k == K::m) { action("music"); e.handled = true; return; }
    if (k == K::n) { action("sound"); e.handled = true; return; }
    if (k == K::enter && panel_ != Panel::none) { open(Panel::none); e.handled = true; }
}

void SwitchboxView::on_text_input(gf::TextInputEvent& e) {
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

void SwitchboxView::action(const std::string& id) {
    if (id == "close") open(Panel::none);
    else if (id == "help") open(Panel::help);
    else if (id == "scores") open(Panel::scores);
    else if (id == "sound") { save_.settings.sound = !save_.settings.sound; dirty_ = true; layout_buttons(); }
    else if (id == "music") { save_.settings.music = !save_.settings.music; dirty_ = true; layout_buttons(); }
    else if (id == "save_name") {
        TopScore t;
        t.name = name_entry_.empty() ? "BOX FRIEND" : name_entry_;
        t.steps = last_steps_;
        t.when = wall_clock();
        add_score(save_.scores, t);
        save_.settings.player_name = t.name;
        play("ui_name_confirm", .8f);
        persist();
        open(Panel::scores);
    }
}

void SwitchboxView::open(Panel p) {
    panel_ = p;
    pressed_.clear();
    layout_buttons();
}

// ------------------------------------------------------------------ drawing
const Mask& SwitchboxView::tmask(const std::string& s, bool bold, double size, int wrap_game) const {
    return text_mask(s, bold ? Font::speech_bold : Font::speech, size, wrap_game > 0 ? wrap_game * pixel_ : 0);
}
int SwitchboxView::text(const std::string& s, int x, int y, Col c, double size, bool bold, int wrap) {
    texts_.push_back({s, bold, size, wrap, x, y, c});
    return text_w(s, size, bold);
}
int SwitchboxView::text_w(const std::string& s, double size, bool bold) const {
    return static_cast<int>(std::ceil(tmask(s, bold, size, 0).w / static_cast<double>(pixel_)));
}
int SwitchboxView::text_h(const std::string& s, double size, bool bold, int wrap) const {
    return static_cast<int>(std::ceil(tmask(s, bold, size, wrap).h / static_cast<double>(pixel_)));
}

void SwitchboxView::layout_buttons() {
    buttons_.clear();
    if (pw_ <= 0) return;
    const int bh = 12, y = ph_ - bh - 4;
    int x = pw_ - 4;
    auto add_right = [&](const std::string& id, const std::string& label) {
        const int w = text_w(label, 11, true) + 10;
        x -= w;
        buttons_.push_back({id, label, x, y, w, bh});
        x -= 3;
    };
    if (panel_ == Panel::none) {
        add_right("music", save_.settings.music ? "Music on" : "Music off");
        add_right("sound", save_.settings.sound ? "Sound on" : "Sound off");
        add_right("scores", "Top scores");
        add_right("help", "Help");
        return;
    }
    const int ww = std::min(pw_ - 20, 300), wh = std::min(ph_ - 20, 210);
    const int wx = (pw_ - ww) / 2, wy = (ph_ - wh) / 2;
    const std::string label = panel_ == Panel::name ? "Save" : "Close";
    const int w = text_w(label, 11, true) + 14;
    buttons_.push_back({panel_ == Panel::name ? "save_name" : "close", label, wx + ww - w - 8, wy + wh - 18, w, 13});
}

void SwitchboxView::draw_button(const Button& b) {
    const bool down = pressed_ == b.id, over = hover_ == b.id;
    frame_.begin(); frame_.rrect(b.x + 1, b.y + 1, b.w, b.h, 4); frame_.fill(hex(0x8A4A66, .35f));
    frame_.begin(); frame_.rrect(b.x, b.y + (down ? 1 : 0), b.w, b.h, 4); frame_.fill(over ? kPinkSoft : kPaper);
    frame_.begin(); frame_.rrect(b.x + .5, b.y + .5 + (down ? 1 : 0), b.w - 1, b.h - 1, 4); frame_.stroke(kPink, 1);
    const int tw = text_w(b.label, 11, true), th = text_h(b.label, 11, true);
    text(b.label, b.x + (b.w - tw) / 2, b.y + (b.h - th) / 2 + (down ? 1 : 0), kInk, 11, true);
}

void SwitchboxView::draw_hud() {
    const int x = 6, y = 6;
    const std::string steps = "Steps  " + std::to_string(puzzle_.steps());
    const std::string best = save_.scores.empty() ? "Best  -" : "Best  " + std::to_string(save_.scores.front().steps);
    const int w = std::max(text_w(steps, 13, true), text_w(best, 11, false)) + 14, h = 27;
    frame_.begin(); frame_.rrect(x + 1, y + 1, w, h, 6); frame_.fill(hex(0x8A4A66, .3f));
    frame_.begin(); frame_.rrect(x, y, w, h, 6); frame_.fill(alpha(kPaper, .92f));
    frame_.begin(); frame_.rrect(x + .5, y + .5, w - 1, h - 1, 6); frame_.stroke(kPink, 1);
    text(steps, x + 7, y + 3, kInk, 13, true);
    text(best, x + 7, y + 15, hex(0x9A6A80), 11, false);
    if (save_.cracked > 0) {
        const std::string c = "Cracked " + std::to_string(save_.cracked);
        text(c, x + 2, y + h + 3, hex(0x9A6A80), 10, false);
    }
    for (const Button& b : buttons_) draw_button(b);
}

void SwitchboxView::draw_mole_hud() {
    if (!mole_.active() && result_t_ <= 0) return;
    std::string big, small;
    if (mole_.active() && !mole_.playing()) { big = "Ready..."; small = "Whack the switches as they pop up!"; }
    else if (mole_.active()) {
        const int s = static_cast<int>(std::ceil(mole_.left()));
        big = "0:" + std::string(s < 10 ? "0" : "") + std::to_string(s) + "    " + std::to_string(mole_.hits()) + " whacked";
        small = mole_.hits() >= Mole::kGreat ? "Amazing! Keep going!" : std::to_string(Mole::kGreat - mole_.hits()) + " more for a surprise";
    } else {
        big = std::to_string(result_hits_) + " whacked!";
        small = result_hits_ >= Mole::kGreat ? "" : "Get " + std::to_string(Mole::kGreat) + " for a surprise.";
    }
    const int bw = std::max(text_w(big, 20, true), text_w(small, 11, false)) + 24, bh = small.empty() ? 30 : 42;
    const int bx = (pw_ - bw) / 2, by = 8;
    frame_.begin(); frame_.rrect(bx + 2, by + 2, bw, bh, 10); frame_.fill(hex(0x8A4A66, .3f));
    frame_.begin(); frame_.rrect(bx, by, bw, bh, 10); frame_.fill(alpha(kPaper, .95f));
    frame_.begin(); frame_.rrect(bx + 1, by + 1, bw - 2, bh - 2, 9); frame_.stroke(kPink, 2);
    text(big, (pw_ - text_w(big, 20, true)) / 2, by + 5, kInk, 20, true);
    if (!small.empty()) text(small, (pw_ - text_w(small, 11, false)) / 2, by + 28, hex(0xB0507A), 11, false);
}

void SwitchboxView::draw_bubble() {
    if (bubble_.text.empty()) return;
    // anchor: her mouth when she is out, the lid's gap while she is inside
    double ax, ay;
    if (bubble_.muffled && actor_.hidden()) stage_.to_screen({0, Stage::kOpenY0 + .2, Stage::kBoxZ + .1}, ax, ay);
    else {
        const M34 h = head_frame(st_.girl);
        stage_.to_screen(h.apply({0, -.4, -.2}), ax, ay);
    }
    const int wrap = bubble_.special ? 170 : 130;
    const double size = bubble_.special ? 17 : bubble_.muffled ? 11 : 12.5;
    const std::string shown = bubble_.text.substr(0, static_cast<size_t>(std::max(1, bubble_.shown)));
    const int tw = std::min(wrap, text_w(bubble_.text, size, bubble_.special)), th = text_h(bubble_.text, size, bubble_.special, wrap);
    const int bw = tw + 12, bh = th + 7;
    const double pop = cab_reduced_ || save_.settings.reduced_motion ? 1.0 : std::min(1.0, bubble_.age * 7);
    const bool right = ax < pw_ * .5;
    int bx = static_cast<int>(right ? ax + 26 : ax - 26 - bw), by = static_cast<int>(ay - bh - 18 - (1 - pop) * 4);
    bx = std::clamp(bx, 4, pw_ - bw - 4);
    by = std::clamp(by, 4, ph_ - bh - 24);
    const Col bg = bubble_.special ? hex(0xFFE3EE) : bubble_.muffled ? hex(0xF7EEF4) : kPaper;
    const Col line = bubble_.special ? hex(0xE0508A) : bubble_.muffled ? hex(0xC9A3B8) : kPink;
    // tail toward the speaker
    const double tx = std::clamp(ax, bx + 10.0, bx + bw - 10.0);
    frame_.begin(); frame_.move(tx - 6, by + bh - 1); frame_.line(ax, ay - 6); frame_.line(tx + 6, by + bh - 1); frame_.close(); frame_.fill(line);
    frame_.begin(); frame_.rrect(bx + 1, by + 2, bw, bh, 8); frame_.fill(hex(0x8A4A66, .25f));
    frame_.begin(); frame_.rrect(bx, by, bw, bh, 8); frame_.fill(bg);
    frame_.begin(); frame_.rrect(bx + .5, by + .5, bw - 1, bh - 1, 8); frame_.stroke(line, bubble_.muffled ? 1 : 1.5);
    frame_.begin(); frame_.move(tx - 4.5, by + bh - 1.5); frame_.line(ax, ay - 8); frame_.line(tx + 4.5, by + bh - 1.5); frame_.close(); frame_.fill(bg);
    text(shown, bx + 6, by + 3, bubble_.special ? hex(0xB02A66) : bubble_.muffled ? hex(0x8E6E80) : kInk, size, bubble_.special, wrap);
}

void SwitchboxView::draw_panel() {
    if (panel_ == Panel::none) return;
    frame_.fill_rect(0, 0, pw_, ph_, hex(0x4A2C40, .35f));
    const int ww = std::min(pw_ - 20, 300), wh = std::min(ph_ - 20, 210);
    const int wx = (pw_ - ww) / 2, wy = (ph_ - wh) / 2;
    frame_.begin(); frame_.rrect(wx + 2, wy + 3, ww, wh, 10); frame_.fill(hex(0x4A2C40, .35f));
    frame_.begin(); frame_.rrect(wx, wy, ww, wh, 10); frame_.fill(kPaper);
    frame_.begin(); frame_.rrect(wx + 1, wy + 1, ww - 2, wh - 2, 9); frame_.stroke(kPink, 2);
    frame_.begin(); frame_.rrect(wx + 1, wy + 1, ww - 2, 22, 9); frame_.fill(kPinkSoft);
    int ly = wy + 28;
    auto line = [&](const std::string& t, Col c = kInk, bool bold = false, double size = 11) {
        text(t, wx + 10, ly, c, size, bold, ww - 20);
        ly += text_h(t, size, bold, ww - 20) + 3;
    };
    auto title = [&](const std::string& t) { text(t, wx + 10, wy + 5, kInk, 14, true); };
    switch (panel_) {
        case Panel::help:
            title("Switchbox");
            line("She lives in the box, and those are her switches. Flip one and she pops up to push it back down.");
            line("The box knows a secret order for all six switches. Flip the right one next and its lamp lights. "
                 "Flip a wrong one and every lamp goes out.");
            line("Light all six to crack the combination. Fewer flips is a better score. Then she makes a new one.");
            line("Flip quickly and she has to hurry. She has two hands.");
            line("Click a switch or press 1 to 6.   T top scores   M music   N sound   F1 help   Esc close", hex(0xB0507A), true, 10);
            break;
        case Panel::scores:
            title("Fewest flips");
            if (save_.scores.empty()) line("No combinations cracked yet.", hex(0x9A6A80));
            for (size_t i = 0; i < save_.scores.size(); ++i) {
                const TopScore& t = save_.scores[i];
                char buf[64];
                std::snprintf(buf, sizeof buf, "%2zu.  %-14s  %d", i + 1, t.name.c_str(), t.steps);
                line(buf, i == 0 ? hex(0xB0507A) : kInk, i == 0);
            }
            break;
        case Panel::name:
            title("A top score!");
            line("You cracked it in " + std::to_string(last_steps_) + " flips.", kInk, true, 12);
            line("Your name for the box:");
            line(name_entry_ + (std::sin(t_ * 6) > 0 ? "_" : " "), hex(0xB0507A), true, 14);
            break;
        case Panel::none: break;
    }
    for (const Button& b : buttons_) draw_button(b);
}

void SwitchboxView::compose() {
    texts_.clear();
    stage_.r.present(frame_, 1, 0, 0, true);
    draw_hud();
    draw_mole_hud();
    draw_bubble();
    draw_panel();
}

void SwitchboxView::blit_texts(std::uint32_t* dst, size_t stride_px, double k) {
    const int sc = std::max(1, static_cast<int>(std::lround(bs_)));
    for (const HiText& h : texts_) {
        const Mask& m = tmask(h.s, h.bold, h.size, h.wrap);
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
                const auto ch = [&](int sh, float src) {
                    return static_cast<std::uint32_t>(std::min(255.f, src * cov * 255 + static_cast<float>((d >> sh) & 255) * kk + .5f)) << sh;
                };
                drow[dx] = ch(0, pb) | ch(8, pg) | ch(16, pr) | (0xFFu << 24);
            }
        }
    }
}

}  // namespace sbx
