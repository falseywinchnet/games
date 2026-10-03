#include "fourpegs_view.hpp"

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

namespace fp {

namespace {
const Col kInk = hex(0x1E1A1C), kCard = hex(0xF6F0E4), kBrass = hex(0xB89350), kBrassDark = hex(0x6E5428), kPanel = hex(0x17121A);
const Col kPegCol[kColors] = {hex(0xF0324A), hex(0xFFC93A), hex(0x46D46A), hex(0x2EA8FF), hex(0xB063FF), hex(0xF4F1EA)};
const char* kTier[4] = {"", "fp_music_t1", "fp_music_t2", "fp_music_t3"};

std::uint64_t seed_now() { return static_cast<std::uint64_t>(wall_clock() * 1000) ^ 0xF0B5EEDULL; }

// a tiny peg symbol for the test sheet, in game pixels
void mini_symbol(Canvas& c, int color, double x, double y, double r, Col col) {
    c.begin();
    switch (color) {
        case 0: c.circle(x, y, r * .5); break;
        case 1: c.move(x, y - r * .7); c.line(x + r * .65, y + r * .5); c.line(x - r * .65, y + r * .5); c.close(); break;
        case 2: c.rect(x - r * .5, y - r * .5, r, r); break;
        case 3: c.move(x, y - r * .75); c.line(x + r * .6, y); c.line(x, y + r * .75); c.line(x - r * .6, y); c.close(); break;
        case 4:
            for (int i = 0; i < 10; ++i) {
                const double a = -std::numbers::pi / 2 + i * std::numbers::pi / 5, rr = i % 2 ? r * .32 : r * .78;
                i == 0 ? c.move(x + std::cos(a) * rr, y + std::sin(a) * rr) : c.line(x + std::cos(a) * rr, y + std::sin(a) * rr);
            }
            c.close();
            break;
        default: c.rect(x - r * .65, y - r * .18, r * 1.3, r * .36); c.rect(x - r * .18, y - r * .65, r * .36, r * 1.3); break;
    }
    c.fill(col);
}
}  // namespace

FourPegsView::FourPegsView(gf::StableId id, Options opt) : Control(std::move(id)), opt_(opt), board_(seed_now()), actor_(seed_now() ^ 0x5EED) {
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    {
        gf::SurfaceMaterial none;
        none.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(0, 0, 0))};
        set_authored_surface_material(none);
    }
    set_accessible_name("Four Pegs. A composed gentleman has locked his plan behind a code of four pegs in six colours. "
                        "Name it in ten attempts. Drag or click pegs into the four sockets, then press Check. "
                        "Keys: 1 to 6 place a peg, Backspace removes one, Enter checks. N or F2 starts a new game. F1 help, T top scores, M music, S sound.");
    bool resumed = false;
    if (load_save(save_path(opt_.dev), save_) && save_.has_board && board_.restore(save_.board)) {
        resumed = true;
        actor_.set_plan(save_.plan.empty() ? "carry out his plan" : save_.plan);
        sheet_rows_ = board_.turns_used();
        if (!board_.rows().empty()) { st_.console.last = board_.rows().back().score; st_.console.pins = 1; }
        if (board_.over()) { st_.console.secret = board_.secret(); st_.console.reveal = 1; }
        if (board_.lost()) panel_ = Panel::gameover;
        else actor_.speech.push_back({"Ah. You have returned. Shall we continue?", Gesture::offer, VFace{VEyes::menace, VBrow::arched, VMouth::grin}, .4});
    }
    name_entry_ = save_.settings.player_name;
    if (!resumed) new_game();
    const int left = board_.turns_left();
    tier_ = left >= 6 ? 1 : left >= 3 ? 2 : 3;
    if (const char* sc = std::getenv("FP_SCRIPT"); sc && opt_.dev) {
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

void FourPegsView::on_attached_to_window() {
    audio_start(asset_dir());
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(33));
    subs_.push_back((*timer_).tick().subscribe(*this, gf::Delegate<>::bind<FourPegsView, &FourPegsView::tick>(*this)));
    subs_.push_back((*attached_window()).active_changed().subscribe(
        *this, gf::Delegate<bool>::bind<FourPegsView, &FourPegsView::active_changed>(*this)));
    subs_.push_back((*attached_window()).pointer_capture_changed().subscribe(
        *this, gf::Delegate<const gf::PointerCaptureChange&>::bind<FourPegsView, &FourPegsView::capture_changed>(*this)));
    last_ = std::chrono::steady_clock::now();
    (*timer_).start();
}

void FourPegsView::on_detaching_from_window(gf::Window&) noexcept {
    try { persist(); } catch (...) {}
    if (timer_) (*timer_).stop();
    timer_.reset();
    subs_.clear();
    cancel_drag();
    audio_stop();
}

void FourPegsView::set_cabinet(bool foreground, bool music, bool sound, bool reduced_motion) {
    if (timer_) {
        if (foreground) {
            last_ = std::chrono::steady_clock::now();
            (*timer_).start();
        } else {
            (*timer_).stop();
        }
    }

    cab_front_ = foreground;
    cab_music_ = music;
    cab_sound_ = sound;
    cab_reduced_ = reduced_motion;
    audio_cabinet(foreground, music, sound);
    if (!foreground) {
        cancel_drag();
        audio_stop();
    }
    music_tick();
}

void FourPegsView::cancel_drag() {
    mouse_down_ = false;
    dragging_ = false;
    drag_color_ = -1;
    drag_from_ = -1;
    st_.console.drag_color = -1;
    pressed_.clear();
    set_pointer_capture(false);
}

void FourPegsView::on_focus_changed(bool focused) {
    if (!focused) { cancel_drag(); }
}

void FourPegsView::active_changed(bool active) {
    if (!active) { cancel_drag(); }
}

void FourPegsView::capture_changed(const gf::PointerCaptureChange& change) {
    const bool own = change.captured && change.control_id == runtime_id();
    // Window releases capture before delivering primary mouse-up. That release
    // still belongs to our gesture; another owner, focus loss or deactivation
    // cancels it instead.
    if (change.captured && !own && mouse_down_) { cancel_drag(); }
}

void FourPegsView::activate() {
    if (attached_window()) static_cast<void>((*attached_window()).request_focus(shared_from_this()));
}

void FourPegsView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    pw_ = std::max(64, static_cast<int>(std::ceil(bounds.width / pixel_)));
    ph_ = std::max(64, static_cast<int>(std::ceil(bounds.height / pixel_)));
    frame_.resize(pw_, ph_);
    lair_.resize(pw_, ph_);
    lair_.r.ax = .42;  // leave the right of the frame for the test sheet
    lair_.r.set_camera();
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

void FourPegsView::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    if (surface_ && !direct_) p.draw_live_surface(surface_, b);
    else p.fill_rect(b, gf::Color::rgba(26, 14, 30));
}

// ------------------------------------------------------------------ game
void FourPegsView::play(const std::string& name, float gain, float rate) {
    audio_sfx(name, gain, rate, save_.settings.sound && cab_sound_ && cab_front_ && visible());
}

void FourPegsView::new_game() {
    selected_color_ = selected_slot_ = auto_slot_ = -1;
    board_.new_game();
    st_.console = ConsoleState{};
    st_.console.reboot = .01;
    st_.doom = 0;
    doom_t_ = -1;
    blackout_ = 0;
    crashes_ = 0;
    checking_ = false;
    pins_t_ = -1;
    sheet_rows_ = 0;
    pending_score_ = 0;
    shown_.fill(-1);
    say_text_.clear();
    actor_.new_game();
    save_.plan = actor_.plan();
    dirty_ = true;
}

void FourPegsView::place(int slot, int color) {
    if (checking_ || board_.over() || slot < 0) return;
    if (board_.set(slot, color)) {
        st_.console.pop[static_cast<size_t>(slot)] = 1;
        play("fp_place_0" + std::to_string(1 + std::rand() % 3), .9f, .97f + .06f * static_cast<float>(color) / kColors);
        actor_.placed(slot);
        dirty_ = true;
    }
}

void FourPegsView::clear_slot(int slot) {
    if (checking_ || board_.over()) return;
    if (board_.clear(slot)) { play("fp_remove", .7f); dirty_ = true; }
}

void FourPegsView::check() {
    if (checking_ || board_.over() || panel_ != Panel::none) return;
    if (!board_.complete()) { play("fp_invalid", .6f); return; }
    selected_color_ = selected_slot_ = auto_slot_ = -1;
    shown_ = board_.draft();
    const Score s = board_.submit();
    checking_ = true;
    st_.console.check_down = true;
    st_.console.press = 1;
    st_.console.pins = 0;
    st_.console.last = s;
    pins_played_ = 0;
    play("fp_check", .9f);
    actor_.submitted(s, board_.turns_left(), board_.won(), board_.lost());
    if (board_.won()) { ++save_.foiled; pending_score_ = board_.turns_used(); }
    if (board_.lost()) ++save_.triumphs;
    dirty_ = true;
}

// After a guess: his inspection, the pins lighting one by one, then the test sheet.
void FourPegsView::judged_tick(double dt) {
    if (actor_.take_reveal_pins()) pins_t_ = 0;
    if (actor_.take_reveal_secret()) {
        st_.console.secret = board_.secret();
        st_.console.reveal = .01;
        if (board_.won()) { play("fp_stinger_defeat", .9f); audio_duck_music(1); }
        else { play("fp_stinger_triumph", .9f); audio_duck_music(1); }
    }
    if (st_.console.reveal > 0 && st_.console.reveal < 1) st_.console.reveal = std::min(1.0, st_.console.reveal + dt / 1.2);
    if (pins_t_ >= 0) {
        pins_t_ += dt;
        const Score s = st_.console.last;
        st_.console.pins = std::clamp(pins_t_ / .9, 0.0, 1.0);
        const int lit = static_cast<int>(std::floor(st_.console.pins * kPegs + 1e-6));
        // each pin sounds as it lights; the sound says only what the pin shows
        while (pins_played_ < lit) {
            const int k = pins_played_++;
            if (k < s.exact) play("fp_pin_exact", .8f, 1 + .03f * static_cast<float>(k));
            else if (k < s.exact + s.near) play("fp_pin_near", .7f, 1 + .03f * static_cast<float>(k));
            else if (k == 0) play("fp_pin_none", .6f);
        }
        if (pins_t_ > 1.6) {
            pins_t_ = -1;
            checking_ = false;
            st_.console.check_down = false;
            shown_.fill(-1);
            sheet_rows_ = board_.turns_used();
            const int left = board_.turns_left();
            tier_ = board_.over() ? 1 : left >= 6 ? 1 : left >= 3 ? 2 : 3;
        }
    }
}

void FourPegsView::speech_tick(double dt) {
    if (actor_.speech.empty()) { say_text_.clear(); actor_.set_talking(false); return; }
    const Speech& sp = actor_.speech.front();
    if (say_text_ != sp.text) { say_text_ = sp.text; say_age_ = 0; say_shown_ = 0; }
    const int before = say_shown_;
    say_age_ += dt;
    say_shown_ = std::min(static_cast<int>(say_text_.size()), static_cast<int>(say_age_ * 30));
    const bool typing = say_shown_ < static_cast<int>(say_text_.size());
    actor_.set_talking(typing);
    // his voice: a low murmured syllable every few letters
    if (typing && sp.gesture != Gesture::triumph && say_shown_ / 3 != before / 3 && say_text_[static_cast<size_t>(say_shown_)] != ' ')
        play("fp_voice_0" + std::to_string(1 + std::rand() % 8), .5f, .94f + .1f * static_cast<float>(std::rand() % 100) / 100);
    if (say_age_ > say_text_.size() / 30.0 + sp.hold + .6) {
        actor_.speech.pop_front();
        say_text_.clear();
    }
}

// He has won: the lair trembles, cracks, and comes down; the screen goes dark; GAME OVER.
void FourPegsView::doom_tick(double dt) {
    if (actor_.take_apocalypse()) { doom_t_ = 0; crashes_ = 0; play("fp_rumble", 1); }
    if (doom_t_ < 0) return;
    doom_t_ += dt;
    st_.doom = std::clamp(doom_t_ / 6.5, 0.0, 1.0);
    shake_ = std::max(shake_, .3 + 2.2 * st_.doom * st_.doom);
    static const double crash_at[] = {.3, .42, .55, .7, .85};
    while (crashes_ < 5 && st_.doom >= crash_at[crashes_]) { play("fp_crash", .9f, .85f + .1f * static_cast<float>(crashes_)); ++crashes_; }
    if (doom_t_ > 7.2) {
        blackout_ = std::min(1.0, blackout_ + dt / 1.2);
        if (blackout_ >= 1) { doom_t_ = -1; open(Panel::gameover); }
    }
}

// The score follows the turns left; a change cuts over on the next bar line.
void FourPegsView::music_tick() {
    const bool on = save_.settings.music && cab_music_ && cab_front_;
    const bool silent = panel_ == Panel::gameover || blackout_ > 0;
    audio_music_on_bar(visible() && cab_front_ && !silent ? kTier[doom_t_ >= 0 ? 3 : tier_] : "", on);
    st_.console.alarm += ((tier_ == 3 ? 1.0 : tier_ == 2 ? .3 : 0.0) - st_.console.alarm) * .05;
}

void FourPegsView::persist() {
    save_.board = board_.state();
    save_.has_board = true;
    save_.plan = actor_.plan();
    write_save(save_path(opt_.dev), save_);
    dirty_ = false;
}

void FourPegsView::run_script() {
    while (!script_.empty() && script_.front().first <= t_) {
        const std::string code = script_.front().second;
        script_.erase(script_.begin());
        if (code == "n") new_game();
        else if (code == "c") check();
        else if (code == "p") actor_.poked();
        else if (code == "s") for (int i = 0; i < kPegs; ++i) place(i, board_.secret()[static_cast<size_t>(i)]);       // the answer
        else if (code == "w") for (int i = 0; i < kPegs; ++i) place(i, (board_.secret()[static_cast<size_t>(i)] + 1 + i) % kColors);  // some guess
        else if (code == "x") for (int i = 0; i < kPegs; ++i) place(i, (board_.secret()[static_cast<size_t>(i)] + (i == 3 ? 1 : 0)) % kColors);  // three exact
    }
}

void FourPegsView::tick() {
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - last_).count(), 0.0, .25);
    last_ = now;
    t_ += dt;
    run_script();
    // the console mirrors the board, except a guess being judged stays in its sockets
    ConsoleState& cs = st_.console;
    cs.draft = checking_ ? shown_ : board_.draft();
    if (dragging_ && drag_from_ >= 0 && !checking_) cs.draft[static_cast<size_t>(drag_from_)] = -1;
    cs.can_check = !checking_ && board_.complete() && !board_.over();
    cs.turn = std::min(kTurns, board_.turns_used() + (checking_ ? 0 : 1));
    cs.turns = kTurns;
    for (double& p : cs.pop) p = std::max(0.0, p - dt * 4);
    cs.press = std::max(0.0, cs.press - dt * 3.5);
    if (cs.reboot > 0) { cs.reboot += dt / .9; if (cs.reboot >= 1) cs.reboot = 0; }
    {
        V3 w;
        const bool ok = mouse_in_ && lair_.console_point(mouse_x_, mouse_y_, w) && w.y > Lair::kDeskFront && w.y < Lair::kDeskBack;
        actor_.watch(dragging_ ? cs.drag_at : w, dragging_ || ok);
    }
    actor_.update(dt, st_);
    for (const VCue& c : actor_.cues) {
        if (c.kind == VCue::sound) play(c.name, c.gain, c.rate);
        else shake_ = std::max(shake_, static_cast<double>(c.gain));
    }
    actor_.cues.clear();
    judged_tick(dt);
    doom_tick(dt);
    speech_tick(dt);
    music_tick();
    audio_tick(dt);
    shake_ = std::max(0.0, shake_ - dt * 3);
    // the top-score book opens once he has finished speaking
    if (pending_score_ > 0 && actor_.speech.empty() && !checking_ && panel_ == Panel::none) {
        if (qualifies(save_.scores, pending_score_)) { open(Panel::name); play("fp_stinger_topscore", .9f); audio_duck_music(1); }
        else pending_score_ = 0;
    }
    save_t_ += dt;
    if (save_t_ > 10 || (dirty_ && save_t_ > 2)) { save_t_ = 0; persist(); }
    gf::Window* win = attached_window();
    const bool hidden = win && (*win).occluded();
    const bool front = !win || (*win).active();
    const bool shown = visible();
    const auto pace = std::chrono::milliseconds(hidden || !shown || !cab_front_ ? 500 : front ? 33 : 100);
    if (timer_ && (*timer_).interval() != pace) (*timer_).set_interval(pace);
    if (hidden || !shown || lair_.r.rgb.empty() || frame_.px.empty()) return;
    if (!rendering_pending_ && shake_ > 0 && !cab_reduced_) {
        lair_.r.target.x = std::sin(t_ * 70) * .03 * shake_;
        lair_.r.target.z = 2.0 + std::cos(t_ * 53) * .02 * shake_;
        lair_.r.set_camera();
    } else if (!rendering_pending_ && (lair_.r.target.x != 0 || lair_.r.target.z != 2.0)) {
        lair_.r.target.x = 0;
        lair_.r.target.z = 2.0;
        lair_.r.set_camera();
    }
    LairState displayed = st_;
    if (cab_reduced_) {
        // Keep the narrative, pin feedback and blackout timeline. Suppress
        // decorative shaking, debris, peg bounce and secondary coat motion.
        displayed.doom = 0;
        displayed.console.pop.fill(0);
        displayed.villain.breathe = 0;
        displayed.villain.cape_sway = 0;
    }
    if (!game_text_) {
        game_text_ = std::make_unique<games::GameText>(
            std::filesystem::path(games::asset_directory()) / "fonts", "LibreBaskerville");
    }
    if (!rendering_pending_) {
        capture_render_state();
        lair_.render(displayed, cab_reduced_ ? 0 : t_);
    }
    (*game_text_).begin();
    compose();
    publish();
}

void FourPegsView::publish() {
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
void FourPegsView::on_pointer(gf::PointerEvent& e) {
    const gf::Point local = point_from_window(e.position);
    mouse_x_ = local.x / pixel_;
    mouse_y_ = local.y / pixel_;
    mouse_in_ = e.action != gf::PointerAction::leave;
    auto hit = [&]() -> std::string {
        for (const Button& b : buttons_)
            if (mouse_x_ >= b.x && mouse_x_ < b.x + b.w && mouse_y_ >= b.y && mouse_y_ < b.y + b.h) return b.id;
        return {};
    };
    ConsoleState& cs = st_.console;
    if (e.action == gf::PointerAction::move) {
        if (mouse_down_ && !has_pointer_capture()) cancel_drag();
        hover_ = hit();
        cs.hover_palette = panel_ == Panel::none ? lair_.pick_palette(mouse_x_, mouse_y_) : -1;
        cs.hover_socket = panel_ == Panel::none ? lair_.pick_socket(mouse_x_, mouse_y_) : -1;
        if (mouse_down_ && drag_color_ >= 0 && !dragging_ && std::hypot(mouse_x_ - down_x_, mouse_y_ - down_y_) > 4) {
            dragging_ = true;
            cs.drag_color = drag_color_;
            play("fp_pick", .7f);
        }
        if (dragging_) { V3 w; if (lair_.console_point(mouse_x_, mouse_y_, w)) cs.drag_at = w; }
        const bool over = !hover_.empty() || cs.hover_palette >= 0 || cs.hover_socket >= 0 || lair_.pick_check(mouse_x_, mouse_y_);
        set_cursor(dragging_ ? gf::CursorKind::hand : over ? gf::CursorKind::hand : gf::CursorKind::arrow);
    }
    if (e.action == gf::PointerAction::down && e.button == gf::PointerButton::primary) {
        activate();
        const std::string h = hit();
        if (!h.empty()) { pressed_ = h; e.handled = true; return; }
        if (panel_ != Panel::none) { e.handled = true; return; }
        mouse_down_ = true;
        down_x_ = mouse_x_;
        down_y_ = mouse_y_;
        set_pointer_capture(true);
        drag_color_ = -1;
        drag_from_ = -1;
        if (!checking_ && !board_.over()) {
            const int pc = lair_.pick_palette(mouse_x_, mouse_y_);
            const int sk = lair_.pick_socket(mouse_x_, mouse_y_);
            if (pc >= 0) drag_color_ = pc;
            else if (sk >= 0 && board_.draft()[static_cast<size_t>(sk)] >= 0) { drag_color_ = board_.draft()[static_cast<size_t>(sk)]; drag_from_ = sk; }
        }
        e.handled = true;
    } else if (e.action == gf::PointerAction::up) {
        if (!pressed_.empty() && hit() == pressed_) action(pressed_);
        pressed_.clear();
        if (mouse_down_) {
            const int sk = lair_.pick_socket(mouse_x_, mouse_y_);
            if (dragging_) {
                selected_color_ = selected_slot_ = auto_slot_ = -1;
                // a drop: into a socket, or off the console to remove it
                if (drag_from_ >= 0) {
                    if (sk >= 0 && sk != drag_from_) { const int c = drag_color_; clear_slot(drag_from_); place(sk, c); }
                    else if (sk < 0) clear_slot(drag_from_);
                } else if (sk >= 0) {
                    place(sk, drag_color_);
                }
            } else if (panel_ == Panel::none) {
                // a click: a palette peg fills the next empty socket; a filled socket empties; CHECK checks; he can be prodded
                const int pc = lair_.pick_palette(mouse_x_, mouse_y_);
                if (pc >= 0) {
                    selected_color_ = pc;
                    auto_slot_ = -1;
                    if (selected_slot_ >= 0) {
                        place(selected_slot_, pc);
                        selected_slot_ = -1;
                        selected_color_ = -1;
                    } else {
                        for (int i = 0; i < kPegs; ++i)
                            if (board_.draft()[static_cast<size_t>(i)] < 0) {
                                place(i, pc);
                                auto_slot_ = i;
                                break;
                            }
                    }
                } else if (sk >= 0) {
                    if (selected_color_ >= 0) {
                        if (auto_slot_ >= 0 && auto_slot_ != sk) clear_slot(auto_slot_);
                        place(sk, selected_color_);
                        selected_color_ = auto_slot_ = selected_slot_ = -1;
                    } else {
                        clear_slot(sk);
                        selected_slot_ = sk;
                    }
                } else if (lair_.pick_check(mouse_x_, mouse_y_)) {
                    check();
                } else if (lair_.pick_villain(mouse_x_, mouse_y_, st_.villain)) {
                    actor_.poked();
                }
            }
            set_pointer_capture(false);
        }
        mouse_down_ = dragging_ = false;
        drag_color_ = drag_from_ = -1;
        cs.drag_color = -1;
        e.handled = true;
    }
}

void FourPegsView::on_key(gf::KeyEvent& e) {
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
    const std::uint32_t digits[kColors] = {0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23};  // HID usages for 1..6
    for (int c = 0; c < kColors; ++c)
        if (k == digits[c] && panel_ == Panel::none) {
            for (int i = 0; i < kPegs; ++i)
                if (board_.draft()[static_cast<size_t>(i)] < 0) { place(i, c); break; }
            e.handled = true;
            return;
        }
    if (k == K::backspace) {
        for (int i = kPegs - 1; i >= 0; --i)
            if (board_.draft()[static_cast<size_t>(i)] >= 0) { clear_slot(i); break; }
        e.handled = true;
        return;
    }
    if (k == K::enter || k == K::space) {
        if (panel_ == Panel::gameover) action("retry");
        else if (panel_ != Panel::none) open(Panel::none);
        else if (board_.over()) action("new");
        else check();
        e.handled = true;
        return;
    }
    if (k == K::escape) { if (panel_ != Panel::none) open(Panel::none); e.handled = true; return; }
    if (k == K::f1) { open(panel_ == Panel::help ? Panel::none : Panel::help); e.handled = true; return; }
    if (k == K::t) { open(panel_ == Panel::scores ? Panel::none : Panel::scores); e.handled = true; return; }
    if (k == K::n || k == K::f2) { action(panel_ == Panel::gameover ? "retry" : "new"); e.handled = true; return; }
    if (k == K::m) { action("music"); e.handled = true; return; }
    if (k == K::s) { action("sound"); e.handled = true; return; }
}

void FourPegsView::on_text_input(gf::TextInputEvent& e) {
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

void FourPegsView::action(const std::string& id) {
    if (id == "close") open(Panel::none);
    else if (id == "retry") { open(Panel::none); new_game(); play("fp_console_boot", .8f); }
    else if (id == "new") { open(Panel::none); new_game(); }
    else if (id == "help") open(Panel::help);
    else if (id == "scores") open(Panel::scores);
    else if (id == "sound") { save_.settings.sound = !save_.settings.sound; dirty_ = true; layout_buttons(); }
    else if (id == "music") { save_.settings.music = !save_.settings.music; dirty_ = true; layout_buttons(); }
    else if (id == "check") check();
    else if (id == "save_name") {
        TopScore t;
        t.name = name_entry_.empty() ? "THE GUEST" : name_entry_;
        t.guesses = pending_score_;
        t.when = wall_clock();
        add_score(save_.scores, t);
        save_.settings.player_name = t.name;
        pending_score_ = 0;
        play("ui_name_confirm", .8f);
        persist();
        open(Panel::scores);
    }
}

void FourPegsView::open(Panel p) {
    // A lost board has no play left; closing Help or Scores returns to its Game Over card.
    if (p == Panel::none && board_.lost()) p = Panel::gameover;
    panel_ = p;
    pressed_.clear();
    layout_buttons();
}

// ------------------------------------------------------------------ drawing
games::TextImage FourPegsView::tmask(const std::string& s, bool bold, double size, int wrap_game) {
    games::TextImage image = (*game_text_).get(s, bold, size,
        wrap_game > 0 ? wrap_game * pixel_ : 0, bs_);
    return image;
}
int FourPegsView::text(const std::string& s, int x, int y, Col c, double size, bool bold, int wrap) {
    texts_.push_back({s, bold, size, wrap, x, y, c});
    return text_w(s, size, bold);
}
int FourPegsView::text_w(const std::string& s, double size, bool bold) {
    return static_cast<int>(std::ceil(tmask(s, bold, size, 0).w / static_cast<double>(pixel_)));
}
int FourPegsView::text_h(const std::string& s, double size, bool bold, int wrap) {
    return static_cast<int>(std::ceil(tmask(s, bold, size, wrap).h / static_cast<double>(pixel_)));
}

void FourPegsView::layout_render_buttons() {
    (*rendering_).buttons_.clear();
    if (pw_ <= 0) return;
    const int bh = 13, y = ph_ - bh - 5;
    int x = 6;
    auto add = [&](const std::string& id, const std::string& label) {
        const int w = text_w(label, 11, true) + 12;
        (*rendering_).buttons_.push_back({id, label, x, y, w, bh});
        x += w + 4;
    };
    if ((*rendering_).panel_ == Panel::none) {
        if (!opt_.hosted) {
            add("new", "New game");
            add("help", "Help");
            add("scores", "Top scores");
            add("music", (*rendering_).save_.settings.music ? "Music on" : "Music off");
            add("sound", (*rendering_).save_.settings.sound ? "Sound on" : "Sound off");
        }
        return;
    }
    if ((*rendering_).panel_ == Panel::gameover) {
        const std::string label = "Try again";
        const int w = text_w(label, 14, true) + 30;
        (*rendering_).buttons_.push_back({"retry", label, (pw_ - w) / 2, ph_ * 3 / 4, w, 20});
        return;
    }
    const int ww = std::min(pw_ - 30, 300), wh = std::min(ph_ - 30, 220);
    const int wx = (pw_ - ww) / 2, wy = (ph_ - wh) / 2;
    const std::string label = (*rendering_).panel_ == Panel::name ? "Sign the book" : "Close";
    const int w = text_w(label, 11, true) + 16;
    (*rendering_).buttons_.push_back({(*rendering_).panel_ == Panel::name ? "save_name" : "close", label, wx + ww - w - 10, wy + wh - 20, w, 14});
}

void FourPegsView::draw_button(const Button& b) {
    const bool down = (*rendering_).pressed_ == b.id, over = (*rendering_).hover_ == b.id;
    frame_.fill_rect(b.x + 1, b.y + 1, b.w, b.h, hex(0x000000, .4f));
    frame_.begin(); frame_.rect(b.x, b.y + (down ? 1 : 0), b.w, b.h); frame_.fill(over ? hex(0x2E2430) : kPanel);
    frame_.begin(); frame_.rect(b.x + .5, b.y + .5 + (down ? 1 : 0), b.w - 1, b.h - 1); frame_.stroke(over ? kBrass : kBrassDark, 1);
    const int tw = text_w(b.label, 11, true), th = text_h(b.label, 11, true);
    text(b.label, b.x + (b.w - tw) / 2, b.y + (b.h - th) / 2 + (down ? 1 : 0), over ? hex(0xF3DDA6) : hex(0xD9C7A0), 11, true);
}

// The test sheet: every judged guess and its pins, in a brass-edged panel.
void FourPegsView::draw_sheet() {
    const int w = 112, x = pw_ - w - 6, y = 6, h = ph_ - 12;
    frame_.fill_rect(x + 2, y + 2, w, h, hex(0x000000, .45f));
    frame_.fill_rect(x, y, w, h, kPanel);
    frame_.begin(); frame_.rect(x + .5, y + .5, w - 1, h - 1); frame_.stroke(kBrass, 1);
    frame_.begin(); frame_.rect(x + 2.5, y + 2.5, w - 5, h - 5); frame_.stroke(kBrassDark, 1);
    text("TEST SHEET", x + (w - text_w("TEST SHEET", 12, true)) / 2, y + 6, hex(0xE7D3A4), 12, true);
    // Rows tighten (and pegs shrink) so all ten attempts fit in a short window.
    const int top = y + 24, rowh = std::max(12, (h - 52) / kTurns);
    const double peg = std::min(5.0, (rowh - 2) / 2.0);
    const int current = (*rendering_).board_.turns_used() + ((*rendering_).checking_ ? -1 : 0);
    for (int i = 0; i < kTurns; ++i) {
        const int ry = top + i * rowh;
        const bool cur = i == current && !(*rendering_).board_.over();
        if (cur) frame_.fill_rect(x + 4, ry - 1, w - 8, rowh - 2, hex(0x3A2C1E, .9f));
        text(std::to_string(i + 1), x + 7, ry + (rowh - 12) / 2, cur ? hex(0xF3DDA6) : hex(0x8E7C5C), 10, cur);
        const bool shown = i < (*rendering_).sheet_rows_;
        for (int p = 0; p < kPegs; ++p) {
            const double cx = x + 30 + p * 14, cy = ry + rowh / 2.0 - 1;
            if (shown) {
                const int c = (*rendering_).board_.rows()[static_cast<size_t>(i)].guess[static_cast<size_t>(p)];
                frame_.fill_circle(cx, cy, peg + .5, hex(0x000000, .6f));
                frame_.fill_circle(cx, cy, peg, kPegCol[c]);
                mini_symbol(frame_, c, cx, cy, peg * .8, c == 5 ? hex(0x2A2030) : hex(0xFFFFFF, .9f));
            } else {
                frame_.begin(); frame_.circle(cx, cy, peg - .5); frame_.stroke(hex(0x4A3E50), 1);
            }
        }
        // pins: gold dots for exact, white rings for elsewhere
        const Score s = shown ? (*rendering_).board_.rows()[static_cast<size_t>(i)].score : Score{};
        for (int k = 0; k < kPegs; ++k) {
            const double px = x + 90 + (k % 2) * 7, py = ry + rowh / 2.0 - 4.5 + (k / 2) * 7;
            if (shown && k < s.exact) frame_.fill_circle(px, py, 2.6, hex(0xFFD24A));
            else if (shown && k < s.exact + s.near) { frame_.begin(); frame_.circle(px, py, 2.2); frame_.stroke(hex(0xFFFFFF), 1.2); }
            else frame_.fill_circle(px, py, 1.2, hex(0x4A3E50));
        }
    }
    // the key
    const int ky = top + kTurns * rowh + 4;
    frame_.fill_circle(x + 10, ky + 5, 2.6, hex(0xFFD24A));
    text("exact place", x + 16, ky, hex(0xBFAE8C), 9, false);
    frame_.begin(); frame_.circle(x + 62, ky + 5, 2.2); frame_.stroke(hex(0xFFFFFF), 1.2);
    text("elsewhere", x + 67, ky, hex(0xBFAE8C), 9, false);
}

void FourPegsView::draw_bubble() {
    if ((*rendering_).say_text_.empty()) return;
    // a calling card beside his head
    double hx, hy;
    lair_.to_screen(villain_head_center((*rendering_).st_.villain) + V3{.55, 0, .35}, hx, hy);
    const int wrap = 150;
    const double size = 13;
    const std::string shown = (*rendering_).say_text_.substr(0, static_cast<size_t>(std::max(1, (*rendering_).say_shown_)));
    const int tw = std::min(wrap, text_w((*rendering_).say_text_, size, false)), th = text_h((*rendering_).say_text_, size, false, wrap);
    const int bw = tw + 14, bh = th + 9;
    int bx = static_cast<int>(hx), by = static_cast<int>(hy - bh / 2.0);
    bx = std::clamp(bx, 6, pw_ - 124 - bw);
    by = std::clamp(by, 6, ph_ - bh - 30);
    double ax, ay;
    lair_.to_screen(villain_head_center((*rendering_).st_.villain) + V3{.2, -.3, -.05}, ax, ay);
    frame_.begin(); frame_.move(bx + 2, by + bh * .55); frame_.line(ax, ay); frame_.line(bx + 2, by + bh * .8); frame_.close(); frame_.fill(kCard);
    frame_.fill_rect(bx + 2, by + 2, bw, bh, hex(0x000000, .4f));
    frame_.fill_rect(bx, by, bw, bh, kCard);
    frame_.begin(); frame_.rect(bx + .5, by + .5, bw - 1, bh - 1); frame_.stroke(kBrassDark, 1);
    frame_.begin(); frame_.rect(bx + 2.5, by + 2.5, bw - 5, bh - 5); frame_.stroke(hex(0xD8C8A8), 1);
    text(shown, bx + 7, by + 4, kInk, size, false, wrap);
}

void FourPegsView::draw_gameover() {
    frame_.fill_rect(0, 0, pw_, ph_, hex(0x050204));
    // a slow red pulse behind the words
    const float pulse = static_cast<float>(.5 + .5 * std::sin((*rendering_).t_ * 1.5));
    frame_.begin(); frame_.rect(0, 0, pw_, ph_); frame_.fill(Paint::rad(pw_ / 2.0, ph_ * .35, pw_ * .6, {{0, hex(0x5A0A12, .5f + .2f * pulse)}, {1, hex(0x050204, 0)}}));
    const std::string title = "GAME OVER";
    text(title, (pw_ - text_w(title, 34, true)) / 2, static_cast<int>(ph_ * .14), hex(0xE8303C), 34, true);
    const std::string plan = (*rendering_).plan_;
    const std::string what = "He went on to " + plan + ".";
    const int wrap = std::min(pw_ - 60, 320);
    text(what, (pw_ - std::min(wrap, text_w(what, 14, false))) / 2, static_cast<int>(ph_ * .34), hex(0xE7D8C0), 14, false, wrap);
    const std::string code = "The code was";
    const int cy = static_cast<int>(ph_ * .55);
    text(code, (pw_ - text_w(code, 12, false)) / 2, cy - 22, hex(0x9A8A78), 12, false);
    for (int i = 0; i < kPegs; ++i) {
        const int c = (*rendering_).board_.secret()[static_cast<size_t>(i)];
        const double x = pw_ / 2.0 + (i - 1.5) * 26, y = cy + 8;
        frame_.fill_circle(x, y, 10.5, hex(0x000000, .7f));
        frame_.fill_circle(x, y, 10, kPegCol[c]);
        mini_symbol(frame_, c, x, y, 8, c == 5 ? hex(0x2A2030) : hex(0xFFFFFF, .95f));
    }
    for (const Button& b : (*rendering_).buttons_) draw_button(b);
}

void FourPegsView::draw_panel() {
    if ((*rendering_).panel_ == Panel::none) return;
    if ((*rendering_).panel_ == Panel::gameover) { draw_gameover(); return; }
    frame_.fill_rect(0, 0, pw_, ph_, hex(0x000000, .45f));
    const int ww = std::min(pw_ - 30, 300), wh = std::min(ph_ - 30, 220);
    const int wx = (pw_ - ww) / 2, wy = (ph_ - wh) / 2;
    frame_.fill_rect(wx + 3, wy + 3, ww, wh, hex(0x000000, .5f));
    frame_.fill_rect(wx, wy, ww, wh, kCard);
    frame_.begin(); frame_.rect(wx + .5, wy + .5, ww - 1, wh - 1); frame_.stroke(kBrassDark, 1);
    frame_.begin(); frame_.rect(wx + 3.5, wy + 3.5, ww - 7, wh - 7); frame_.stroke(kBrass, 1);
    int ly = wy + 34;
    auto line = [&](const std::string& t, Col c = kInk, bool bold = false, double size = 12) {
        text(t, wx + 14, ly, c, size, bold, ww - 28);
        ly += text_h(t, size, bold, ww - 28) + 4;
    };
    auto title = [&](const std::string& t) { text(t, wx + (ww - text_w(t, 16, true)) / 2, wy + 10, kInk, 16, true); };
    switch ((*rendering_).panel_) {
        case Panel::help:
            title("Four Pegs");
            line("He has locked his plan behind a code of four pegs, chosen from six colours. Colours may repeat.");
            line("Drag pegs into the four sockets, or click a peg to fill the next socket. Click a placed peg to remove it. Then press the red CHECK button.");
            line("Gold pins: a peg in exactly the right place. White rings: the right colour in the wrong place. The pins never say which peg.");
            line("Name the code within ten attempts and his plan is foiled. Every attempt is recorded on the test sheet. Fewer attempts make a better score.");
            line("Keys: 1-6 place, Backspace removes, Enter checks.  N new game  T scores  M music  S sound  F1 help", hex(0x6E5428), true, 10);
            break;
        case Panel::scores:
            title("The Book of Guests");
            if ((*rendering_).save_.scores.empty()) line("No one has foiled him yet.", hex(0x6E5428));
            for (size_t i = 0; i < (*rendering_).save_.scores.size(); ++i) {
                const TopScore& t = (*rendering_).save_.scores[i];
                char buf[64];
                std::snprintf(buf, sizeof buf, "%2zu.  %-14s  %d %s", i + 1, t.name.c_str(), t.guesses, t.guesses == 1 ? "attempt" : "attempts");
                line(buf, i == 0 ? hex(0x6E5428) : kInk, i == 0, 11);
            }
            break;
        case Panel::name:
            title("The Book of Guests");
            line("You named his code in " + std::to_string((*rendering_).pending_score_) + ((*rendering_).pending_score_ == 1 ? " attempt." : " attempts."), kInk, true, 13);
            line("He requests that you sign the book:");
            line((*rendering_).name_entry_ + (std::sin((*rendering_).t_ * 6) > 0 ? "_" : " "), hex(0x6E5428), true, 15);
            break;
        case Panel::none: case Panel::gameover: break;
    }
    for (const Button& b : (*rendering_).buttons_) draw_button(b);
}

void FourPegsView::compose() {
    texts_.clear();
    layout_render_buttons();
    lair_.r.present(frame_, 1, 0, 0, true);
    if ((*rendering_).panel_ == Panel::gameover) { draw_panel(); return; }  // a full screen of its own: no text from the table beneath
    draw_sheet();
    if ((*rendering_).panel_ == Panel::none) for (const Button& b : (*rendering_).buttons_) draw_button(b);
    if ((*rendering_).panel_ == Panel::none && (*rendering_).blackout_ <= 0) draw_bubble();
    if ((*rendering_).blackout_ > 0) frame_.fill_rect(0, 0, pw_, ph_, hex(0x050204, static_cast<float>((*rendering_).blackout_)));
    draw_panel();
}

void FourPegsView::blit_texts(std::uint32_t* dst, size_t stride_px, double k) {
    std::span<std::uint32_t> pixels(dst, stride_px * static_cast<std::size_t>(phys_h_));
    for (const HiText& h : texts_) {
        const games::TextImage image = tmask(h.s, h.bold, h.size, h.wrap);
        games::blit_game_text(pixels, phys_w_, phys_h_, stride_px, image,
            static_cast<int>(h.x * k), static_cast<int>(h.y * k),
            h.c.r, h.c.g, h.c.b, h.c.a);
    }
}

void FourPegsView::layout_buttons() {
    // New panel, size or settings invalidate a pending attempt, never the
    // currently displayed pixels. Hit regions follow the next published frame.
    rendering_pending_ = false;
    buttons_.clear();
    if (game_text_) (*game_text_).cancel();
}

void FourPegsView::capture_render_state() {
    if (!rendering_) rendering_ = std::make_unique<RenderState>();
    rendering_pending_ = true;
    RenderState& state = *rendering_;
    state.board_ = board_;
    state.save_ = save_;
    state.st_ = st_;
    state.plan_ = actor_.plan();
    state.say_text_ = say_text_;
    state.say_shown_ = say_shown_;
    state.name_entry_ = name_entry_;
    state.pressed_ = pressed_;
    state.hover_ = hover_;
    state.panel_ = panel_;
    state.checking_ = checking_;
    state.sheet_rows_ = sheet_rows_;
    state.pending_score_ = pending_score_;
    state.blackout_ = blackout_;
    state.t_ = t_;
}

void FourPegsView::host_command(const std::string& id) {
    if (id == "new") { action("new"); return; }
    const Panel wanted = id == "help" ? Panel::help : id == "scores" ? Panel::scores : Panel::none;
    if (wanted == Panel::none || panel_ == Panel::name) return;
    open(panel_ == wanted ? Panel::none : wanted);
}

std::string FourPegsView::host_panel() const {
    return panel_ == Panel::help ? "help" : panel_ == Panel::scores ? "scores" : "";
}

}  // namespace fp
