#include "maze_view.hpp"

#include "cast.hpp"
#include "help_route.hpp"
#include "platform/audio.hpp"
#include "platform/text.hpp"
#include "rewards.hpp"
#include "textures.hpp"
#include "world.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mz {

namespace {
const Col kFace = hex(0xC0C0C0), kLight = hex(0xFFFFFF), kShadow = hex(0x808080), kDark = hex(0x202020), kInk = hex(0x000000);
const Col kPink = hex(0xF060C0), kTeal = hex(0x30C8D8);

std::uint64_t seed_now() { return static_cast<std::uint64_t>(wall_clock() * 1000) ^ 0x3A2E95ULL; }

std::string replace_all(std::string s, const std::string& a, const std::string& b) {
    for (size_t p = s.find(a); p != std::string::npos; p = s.find(a, p + b.size())) s.replace(p, a.size(), b);
    return s;
}
}  // namespace

MazeView::MazeView(gf::StableId id, Options opt) : Control(std::move(id)), opt_(opt) {
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    {
        gf::SurfaceMaterial none;
        none.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(0, 0, 0))};
        set_authored_surface_material(none);
    }
    set_accessible_name("Maze 95. A first-person maze: find the reward named in the briefing. Arrow keys or WASD: up walks forward, down steps back, left and right turn. "
                        "Pads open doors of their colour. F1 help, T trophy shelf, M music.");
    load_save(save_path(opt_.dev), save_);
    if (!save_.run_seed) { save_.run_seed = seed_now() | 1; dirty_ = true; }
    start_level(save_.level);
    if (const char* sc = std::getenv("MZ_SCRIPT"); sc && opt_.dev) {
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

void MazeView::on_attached_to_window() {
    audio_start(asset_dir());
    subs_.push_back(visible_changed().subscribe(*this, gf::Delegate<bool>::bind<MazeView, &MazeView::on_visible_changed>(*this)));
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(33));
    subs_.push_back((*timer_).tick().subscribe(*this, gf::Delegate<>::bind<MazeView, &MazeView::tick>(*this)));
    last_ = std::chrono::steady_clock::now();
    request_frame();
}

void MazeView::on_detaching_from_window(gf::Window&) noexcept {
    try { persist(); } catch (...) {}
    if (timer_) (*timer_).stop();
    timer_.reset();
    audio_stop();
}

void MazeView::activate() {
    if (attached_window()) static_cast<void>((*attached_window()).request_focus(shared_from_this()));
    request_frame();
}

void MazeView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    pixel_ = std::clamp(std::min(bounds.width/320.0,bounds.height/200.0),1.0,3.0);
    pw_ = std::max(64, static_cast<int>(std::ceil(bounds.width / pixel_)));
    ph_ = std::max(64, static_cast<int>(std::ceil(bounds.height / pixel_)));
    frame_.resize(pw_, ph_);
    r_.resize(pw_, ph_);
    bs_ = attached_window() ? (*attached_window()).scale() : 1.0;
    phys_w_ = std::max(1, static_cast<int>(std::lround(bounds.width * bs_)));
    phys_h_ = std::max(1, static_cast<int>(std::lround(bounds.height * bs_)));
    xmap_.clear();
    if (surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        d.opaque = true;  // every pixel is drawn opaque: the window copies, never blends
        static_cast<void>((*surface_).reconfigure(d));
    }
    layout_buttons();
    request_frame();
}

void MazeView::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    if (surface_) p.draw_live_surface(surface_, b);
    else p.fill_rect(b, gf::Color::rgba(0, 128, 128));
}

// ------------------------------------------------------------------ levels
void MazeView::start_level(int n) {
    awarded_ = false;
    level_ = std::max(1, n);
    save_.level = level_;
    const std::uint64_t seed = save_.run_seed * 0x9E3779B97F4A7C15ULL + static_cast<std::uint64_t>(level_) * 7919ULL;
    const Level lv = generate(params_for(level_, seed));
    // what to seek: the cheese first, then something not yet on the shelf
    if (level_ == 1) reward_ = 0;
    else {
        std::uint64_t h = seed ^ 0xC0FFEEULL;
        std::vector<int> fresh;
        for (int i = 1; i < reward_count(); ++i)
            if (!(save_.collected >> i & 1)) fresh.push_back(i);
        h ^= h >> 31; h *= 0x7FB5D329728EA185ULL; h ^= h >> 27;
        reward_ = fresh.empty() ? 1 + static_cast<int>(h % static_cast<std::uint64_t>(reward_count() - 1)) : fresh[static_cast<size_t>(h % fresh.size())];
    }
    sess_.reward_tex = &reward_tex(reward_);
    sess_.reward_name = reward_name(reward_);
    sess_.cast.clear();
    for (int i = 0; i < cast_count(); ++i) {
        const CastMember& m = cast_member(i);
        VisitorDef v;
        v.name = m.name;
        v.tex = &cast_tex(i);
        for (const std::string& l : m.lines) v.lines.push_back(replace_all(l, "{R}", reward_the(reward_)));
        v.w = m.w;
        v.h = m.h;
        sess_.cast.push_back(v);
    }
    sess_.start(lv, seed ^ 0xABCDEFULL);
    autowalk_ = 0;
    // the briefing mentions whatever this maze has that the last did not
    news_.clear();
    const LevelParams& p = lv.params;
    const LevelParams prev = params_for(level_ - 1, seed);
    if (p.doors && (level_ == 2 || !prev.doors)) news_.push_back("Locked doors. Step on the pad of the same colour to open one.");
    if ((p.portals || p.sealed_portal) && (level_ == 4 || (!prev.portals && !prev.sealed_portal))) news_.push_back("Portals. Nothing marks them but the carpet, which is a little... off.");
    if (p.marble && !prev.marble) news_.push_back("A giant marble rolls these halls. It will block you, never crush you.");
    if (p.bulbs && !prev.bulbs) news_.push_back("Floating light bulbs. Touch one and the lights go out. Touch another to bring them back.");
    if (p.snail && !prev.snail) news_.push_back("A paint snail. It redecorates. You'll see.");
    if (p.floors > 1 && prev.floors <= 1) news_.push_back("An elevator tile. There's another maze up there.");
    if (p.ceiling_doors && !prev.ceiling_doors) news_.push_back("A flip stone, and a pad on the ceiling. Touch the stone and walk upside down.");
    open(Panel::briefing);
    play("mz_start", .8f);
    dirty_ = true;
    persist();
}

void MazeView::finish_level() {
    if (awarded_) return;
    awarded_ = true;
    save_.collected |= 1ULL << reward_;
    ++save_.cleared;
    save_.steps += sess_.play.steps;
    if (sess_.play.steps <= sess_.lv.optimal_steps) ++save_.perfect;
    persist();
    open(Panel::won);
}

void MazeView::persist() {
    write_save(save_path(opt_.dev), save_);
    dirty_ = false;
}

void MazeView::play(const std::string& name, float gain, float rate) {
    audio_sfx(name, gain, rate, (opt_.hosted || save_.sound) && cab_sound_ && cab_front_ && visible());
}

bool MazeView::scripted_action(std::string_view code_view) {
    if (!opt_.dev || !cab_front_ || !visible()) return false;
    const std::string code(code_view);
    if (code == "f") sess_.command(Cmd::forward);
    else if (code == "b") sess_.command(Cmd::back);
    else if (code == "l") sess_.command(Cmd::left);
    else if (code == "r") sess_.command(Cmd::right);
    else if (code == "ok") { if (panel_ == Panel::won) action("next"); else open(Panel::none); }
    else if (code == "shelf") open(Panel::shelf);
    else if (code == "help") open(Panel::help);
    else if (code == "credits" || code == "more" || code == "next") action(code);
    else if (code == "auto") autowalk_ = 100000;
    else if (code.rfind("auto",0) == 0) autowalk_ = std::clamp(std::atoi(code.c_str()+4),0,100000);
    else if (code.rfind("lvl",0) == 0) start_level(std::clamp(std::atoi(code.c_str()+3),1,100000));
    else return false;
    request_frame();
    return true;
}
void MazeView::run_script() {
    while (!script_.empty() && script_.front().first <= t_) {
        const std::string code = script_.front().second;
        script_.erase(script_.begin());
        static_cast<void>(scripted_action(code));
    }
}
void MazeView::request_frame() {
    render_dirty_ = true;
    if (timer_ && !(*timer_).enabled() && visible() && cab_front_) {
        last_ = std::chrono::steady_clock::now();
        (*timer_).set_interval(std::chrono::milliseconds(33));
        (*timer_).start();
    }
}
void MazeView::on_visible_changed(bool) {
    if (!visible()) {
        if (timer_) (*timer_).stop();
        audio_cabinet(false,cab_music_,cab_sound_);
    } else {
        audio_cabinet(cab_front_,cab_music_,cab_sound_);
        request_frame();
    }
}
void MazeView::set_cabinet(bool foreground, bool music, bool sound, bool reduced) {
    const bool changed = cab_front_ != foreground || cab_music_ != music ||
                         cab_sound_ != sound || cab_reduced_ != reduced;
    cab_front_ = foreground; cab_music_ = music; cab_sound_ = sound; cab_reduced_ = reduced;
    sess_.set_reduced_motion(reduced);
    audio_cabinet(foreground && visible(),music,sound);
    if (!foreground || !visible()) { if (timer_) (*timer_).stop(); }
    else if (changed) request_frame();
}
std::vector<games::GameCommand> MazeView::commands() const {
    return {{"restart","Restart maze",true,false,true},
            {"help","Help",true,panel_ == Panel::help},
            {"trophies","Trophy shelf",true,panel_ == Panel::shelf},
            {"credits","Who's who",true,panel_ == Panel::credits}};
}
void MazeView::run_command(std::string_view id) {
    if (id == "help" && games::route_help(*this)) return;
    if (id == "help" && panel_ == Panel::help) open(Panel::none);
    else action(id == "trophies" ? "shelf" : std::string(id));
    request_frame();
}
bool MazeView::controls_fit() const {
    for (const Button& button : buttons_)
        if (button.x < 0 || button.y < 0 || button.x+button.w > pw_ || button.y+button.h > ph_) return false;
    return true;
}

void MazeView::tick() {
    if (!visible() || !cab_front_) { if (timer_) (*timer_).stop(); return; }
    ++timer_callbacks_;
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - last_).count(), 0.0, .25);
    last_ = now;
    t_ += dt;
    run_script();
    // dev: walk a best route, re-planned from wherever we are (the marble or a visitor may be in the way)
    if (autowalk_ > 0 && !sess_.busy() && panel_ == Panel::none && sess_.won_t < 0) {
        const std::vector<int> rt = solve_route(sess_.lv, sess_.play);
        if (rt.empty()) autowalk_ = 0;
        else {
            const int d = rt.front(), me = sess_.play.dir;
            const Pos next{sess_.play.at.f, sess_.play.at.x + kDX[d], sess_.play.at.y + kDY[d]};
            const bool in_way = (sess_.world.marble.alive && (sess_.world.marble.at == next || sess_.world.marble.to == next)) ||
                                (sess_.world.visitor.alive && sess_.world.visitor.at == next);
            if (d != me) sess_.command(d == (me + 1) % 4 ? Cmd::right : d == (me + 3) % 4 ? Cmd::left : Cmd::right);
            else if (!in_way) { sess_.command(Cmd::forward); --autowalk_; }
        }
    }
    if (panel_ == Panel::none || panel_ == Panel::won) sess_.update(dt);
    for (const Event& ev : sess_.events) {
        if (ev.kind == Event::sound) play(ev.text, ev.gain, ev.rate);
        else if (ev.kind == Event::won) { /* the dialog opens once the room has been admired */ }
    }
    sess_.events.clear();
    if (sess_.won_t > 1.6 && panel_ == Panel::none) finish_level();
    {
        const bool on = (opt_.hosted || save_.music) && cab_music_ && cab_front_;
        const char* track = sess_.play.blackout ? "mz_music_dark" : sess_.play.flipped ? "mz_music_flip" : "mz_music";
        audio_music(visible() && cab_front_ ? track : "", on);
    }
    audio_tick(dt);
    save_t_ += dt;
    if (dirty_ && save_t_ > 2) { save_t_ = 0; persist(); }
    gf::Window* win = attached_window();
    const bool hidden = win && (*win).occluded();
    if (!hidden && !frame_.px.empty() && (render_dirty_ || panel_ == Panel::none || panel_ == Panel::won)) {
        compose(); publish(); render_dirty_ = false;
    }
    // The marble, paint snail and timed encounters keep a live maze in motion.
    // Dialogs freeze that simulation and stop once audio and scripted input settle.
    const bool moving = panel_ == Panel::none || (panel_ == Panel::won && sess_.won_t < 4);
    if (timer_) {
        if (!moving && script_.empty() && !audio_needs_tick()) (*timer_).stop();
        else (*timer_).set_interval(std::chrono::milliseconds(hidden ? 250 : 33));
    }
}

void MazeView::publish() {
    if (!surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        d.opaque = true;  // every pixel is drawn opaque: the window copies, never blends
        surface_ = gf::LiveSurface::create(d);
        if (surface_ && attached_window()) {
            direct_ = (*attached_window()).queue_live_surface_presentation(shared_from_this(), surface_);
            invalidate(gf::Dirty::paint);  // the paint now shows the live surface
        }
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
        static_cast<void>(lease.publish());
        ++published_frames_;
    }
    if (attached_window() && surface_) {
        direct_ = (*attached_window()).queue_live_surface_presentation(shared_from_this(),surface_);
        invalidate(gf::Dirty::paint);  // the paint now shows the live surface
    }
    if (!direct_) invalidate(gf::Dirty::paint);
    text_cache_trim();
}

// ------------------------------------------------------------------ input
void MazeView::on_pointer(gf::PointerEvent& e) {
    const gf::Point local = point_from_window(e.position);
    mouse_x_ = local.x / pixel_;
    mouse_y_ = local.y / pixel_;
    request_frame();
    std::string hit;
    for (const Button& button : buttons_)
        if (mouse_x_ >= button.x && mouse_x_ < button.x + button.w &&
            mouse_y_ >= button.y && mouse_y_ < button.y + button.h) { hit = button.id; break; }
    if (e.action == gf::PointerAction::move) {
        hover_ = hit;
        set_cursor(!hover_.empty() ? gf::CursorKind::hand : gf::CursorKind::arrow);
    }
    if (e.action == gf::PointerAction::down && e.button == gf::PointerButton::primary) {
        activate();
        const std::string h = hit;
        if (!h.empty()) { pressed_ = h; e.handled = true; return; }
        if (panel_ == Panel::none) {
            // the view itself: left third turns left, right third turns right, the middle walks on
            if (mouse_x_ < pw_ / 3.0) sess_.command(Cmd::left);
            else if (mouse_x_ > pw_ * 2 / 3.0) sess_.command(Cmd::right);
            else if (mouse_y_ > ph_ * .8) sess_.command(Cmd::back);
            else sess_.command(Cmd::forward);
        }
        e.handled = true;
    } else if (e.action == gf::PointerAction::up) {
        if (!pressed_.empty() && hit == pressed_) action(pressed_);
        pressed_.clear();
        e.handled = true;
    }
}

void MazeView::on_key(gf::KeyEvent& e) {
    if (e.handled) return;
    request_frame();
    if (e.action == gf::KeyAction::down && (e.physical_key == gf::PhysicalKey::h || e.physical_key == gf::PhysicalKey::f1) && games::route_help(*this)) { e.handled = true; return; }
    using K = gf::PhysicalKey;
    if (e.action != gf::KeyAction::down) return;
    const std::uint32_t k = e.physical_key;
    if (k == K::f1 || k == K::h) { open(panel_ == Panel::help ? Panel::none : Panel::help); e.handled = true; return; }
    if (panel_ != Panel::none) {
        if (k == K::enter || k == K::space || k == K::escape) {
            if (panel_ == Panel::won && k != K::escape) action("next");
            else if (panel_ == Panel::won) open(Panel::shelf);
            else open(Panel::none);
        } else if (k == K::t && panel_ == Panel::shelf) open(Panel::none);
        else if (k == K::f1 && panel_ == Panel::help) open(Panel::none);
        e.handled = true;
        return;
    }
    if (k == K::up || k == K::w) sess_.command(Cmd::forward);
    else if (k == K::down || k == K::s) sess_.command(Cmd::back);
    else if (k == K::left || k == K::a) sess_.command(Cmd::left);
    else if (k == K::right || k == K::d) sess_.command(Cmd::right);
    else if (k == K::f1 || k == K::h) open(Panel::help);
    else if (k == K::t) open(Panel::shelf);
    else if (k == K::m && !opt_.hosted) action("music");
    else return;
    e.handled = true;
}

void MazeView::action(const std::string& id) {
    play("mz_click", .4f);
    if (id == "ok" || id == "close") open(Panel::none);
    else if (id == "next") start_level(level_ + 1);
    else if (id == "shelf") { trophy_page_ = 0; open(Panel::shelf); }
    else if (id == "trophy_more") {
        const int cols = std::max(4,(pw_-36)/30), cell=(pw_-36)/cols;
        const int per_page=cols*std::max(1,(ph_-82)/cell);
        trophy_page_=(trophy_page_+1)%((reward_count()+per_page-1)/per_page);
    }
    else if (id == "help") { if (!games::route_help(*this)) open(Panel::help); }
    else if (id == "music") { save_.music = !save_.music; dirty_ = true; layout_buttons(); }
    else if (id == "sound") { save_.sound = !save_.sound; dirty_ = true; layout_buttons(); }
    else if (id == "restart") { open(Panel::none); start_level(level_); }
    else if (id == "credits") { credits_page_ = 0; open(Panel::credits); }
    else if (id == "more") { credits_page_ = (credits_page_ + 1) % ((cast_count() + 7) / 8 + 1); layout_buttons(); }
    if (dirty_) persist();
    request_frame();
}

void MazeView::open(Panel p) {
    request_frame();
    panel_ = p;
    pressed_.clear();
    layout_buttons();
}

// ------------------------------------------------------------------ drawing
const Mask& MazeView::tmask(const std::string& s, int font, double size, int wrap_game) const {
    const Font f = font == 1 ? Font::speech_bold : font == 2 ? Font::title : Font::speech;
    return text_mask(s, f, size, wrap_game > 0 ? wrap_game * pixel_ : 0);
}
int MazeView::text(const std::string& s, int x, int y, Col c, double size, int font, int wrap) {
    texts_.push_back({s, font, size, wrap, x, y, c});
    return text_w(s, size, font);
}
int MazeView::text_w(const std::string& s, double size, int font) const { return static_cast<int>(std::ceil(tmask(s, font, size, 0).w / static_cast<double>(pixel_))); }
int MazeView::text_h(const std::string& s, double size, int font, int wrap) const { return static_cast<int>(std::ceil(tmask(s, font, size, wrap).h / static_cast<double>(pixel_))); }

void MazeView::bevel(int x, int y, int w, int h, bool sunken) {
    frame_.fill_rect(x, y, w, h, kFace);
    frame_.fill_rect(x, y, w, 1, sunken ? kShadow : kLight);
    frame_.fill_rect(x, y, 1, h, sunken ? kShadow : kLight);
    frame_.fill_rect(x, y + h - 1, w, 1, sunken ? kLight : kDark);
    frame_.fill_rect(x + w - 1, y, 1, h, sunken ? kLight : kDark);
}

void MazeView::window(int x, int y, int w, int h, const std::string& title) {
    frame_.fill_rect(x + 3, y + 3, w, h, hex(0x000000, .35f));
    bevel(x, y, w, h, false);
    // the title bar: Windows 95, by way of a sunset
    frame_.begin(); frame_.rect(x + 2, y + 2, w - 4, 12); frame_.fill(Paint::lin(x, 0, x + w, 0, {{0, kPink}, {1, kTeal}}));
    text(title, x + 5, y + 2, kLight, 13.1, 1);
    // a close box
    bevel(x + w - 12, y + 3, 9, 8, false);
    frame_.stroke_line(x + w - 10, y + 5, x + w - 6, y + 9, kInk, 1);
    frame_.stroke_line(x + w - 6, y + 5, x + w - 10, y + 9, kInk, 1);
}

void MazeView::blit(const Tex32& t, int x, int y, int w, int h, float dim) {
    for (int yy = 0; yy < h; ++yy) {
        const int dy = y + yy;
        if (dy < 0 || dy >= frame_.h) continue;
        for (int xx = 0; xx < w; ++xx) {
            const int dx = x + xx;
            if (dx < 0 || dx >= frame_.w) continue;
            const std::uint32_t c = t.at(xx * t.w / w, yy * t.h / h);
            if ((c >> 24) < 128) continue;
            std::uint32_t o = c;
            if (dim < 1) {
                struct Channel { std::uint32_t c; float dim;
                    std::uint32_t operator()(int sh) const { return static_cast<std::uint32_t>(((c >> sh) & 255) * dim) << sh; }
                };
                const Channel ch{c,dim};
                o = ch(16) | ch(8) | ch(0);
            }
            reinterpret_cast<std::uint32_t*>(frame_.px.data())[static_cast<size_t>(dy) * frame_.w + dx] = 0xFF000000u | (o & 0xFFFFFF);
        }
    }
}

void MazeView::layout_buttons() {
    buttons_.clear();
    if (pw_ <= 0) return;
    const int bw = 52, bh = 14;
    switch (panel_) {
        case Panel::briefing: buttons_.push_back({"ok", "OK", pw_ / 2 - bw / 2, ph_ / 2 + 52, bw, bh}); break;
        case Panel::won:
            buttons_.push_back({"next", "Next maze", pw_ / 2 - bw - 4, ph_ / 2 + 50, bw + 6, bh});
            buttons_.push_back({"shelf", "Trophies", pw_ / 2 + 6, ph_ / 2 + 50, bw, bh});
            break;
        case Panel::shelf:
            buttons_.push_back({"trophy_more", "More...", pw_/2-bw-4,ph_-34,bw,bh});
            buttons_.push_back({"close", "Close", pw_/2+4,ph_-34,bw,bh});
            break;
        case Panel::help:
            buttons_.push_back({"close", "OK", pw_ / 2 + 70, ph_ / 2 + 62, bw, bh});
            if (!opt_.hosted) buttons_.push_back({"music", save_.music ? "Music: on" : "Music: off", pw_ / 2 - 120, ph_ / 2 + 62, 58, bh});
            if (!opt_.hosted) buttons_.push_back({"sound", save_.sound ? "Sound: on" : "Sound: off", pw_ / 2 - 58, ph_ / 2 + 62, 58, bh});
            buttons_.push_back({"restart", "Restart maze", pw_ / 2 + 4, ph_ / 2 + 62, 62, bh});
            buttons_.push_back({"credits", "Who's who", pw_ / 2 + 70, ph_ / 2 + 44, bw, bh});
            break;
        case Panel::credits:
            buttons_.push_back({"close", "OK", pw_ / 2 + 70, ph_ / 2 + 70, bw, bh});
            buttons_.push_back({"more", "More...", pw_ / 2 + 10, ph_ / 2 + 70, bw, bh});
            break;
        case Panel::none: break;
    }
}

void MazeView::draw_button(const Button& b) {
    const bool down = pressed_ == b.id;
    bevel(b.x, b.y, b.w, b.h, down);
    if (hover_ == b.id) { frame_.begin(); frame_.rect(b.x + 2.5, b.y + 2.5, b.w - 5, b.h - 5); frame_.stroke(hex(0x000000, .5f), 1); }
    const int tw = text_w(b.label, 12.5, 0), th = text_h(b.label, 12.5, 0);
    text(b.label, b.x + (b.w - tw) / 2 + (down ? 1 : 0), b.y + (b.h - th) / 2 + (down ? 1 : 0), kInk, 12.5, 0);
}

void MazeView::draw_status() {
    const int h = 15, y = ph_ - h;
    bevel(0, y, pw_, h, false);
    struct Field { MazeView& view; int y,h;
        void operator()(int x,int w,const std::string& s) const {
            view.bevel(x,y+2,w,h-4,true);
            view.text(s,x+3,y+2,kInk,11.9,0);
        }
    };
    const Field field{*this,y,h};
    field(2, 56, "Maze " + std::to_string(level_));
    field(60, pw_ - 60 - 74, "Seeking: " + reward_the(reward_));
    field(pw_ - 72, 70, "Steps: " + std::to_string(sess_.play.steps));
}

void MazeView::draw_speech() {
    if (sess_.speech.empty() || panel_ != Panel::none) return;
    const Speech& sp = sess_.speech.front();
    const int w = std::min(pw_ - 20, 300), x = (pw_ - w) / 2;
    const int wrap = w - (sp.who.empty() ? 14 : 52);
    const int th = text_h(sp.text, 13.1, 0, wrap);
    const int h = std::max(sp.who.empty() ? 24 : 54, th + 22), y = ph_ - 16 - h;
    window(x, y, w, h, sp.who.empty() ? "Maze" : sp.who);
    int tx = x + 7;
    if (!sp.who.empty()) {
        // the speaker's portrait, framed
        for (int i = 0; i < cast_count(); ++i)
            if (cast_member(i).name == sp.who) {
                bevel(x + 5, y + 15, 38, 36, true);
                frame_.fill_rect(x + 6, y + 16, 36, 34, hex(0xF0E8F8));
                blit(cast_tex(i), x + 6, y + 15, 36, 36);
                break;
            }
        tx = x + 47;
    }
    text(sp.text, tx, y + 16, kInk, 13.1, 0, wrap);
}

void MazeView::draw_panel() {
    if (panel_ == Panel::none) return;
    frame_.fill_rect(0, 0, pw_, ph_, hex(0x000000, .35f));
    switch (panel_) {
        case Panel::briefing: {
            const int w = std::min(pw_ - 20, 280), h = 132 + 11 * static_cast<int>(news_.size()), x = (pw_ - w) / 2, y = ph_ / 2 + 70 - h;
            window(x, y, w, h, "Maze " + std::to_string(level_) + " - Briefing");
            bevel(x + 10, y + 20, 52, 52, true);
            frame_.fill_rect(x + 11, y + 21, 50, 50, hex(0xFFFFFF));
            blit(reward_tex(reward_), x + 12, y + 22, 48, 48);
            text("Today you seek:", x + 70, y + 22, kInk, 13.1, 0);
            const std::string what = reward_a(reward_);
            text(what, x + 70, y + 34, hex(0x800060), 16.2, 1, w - 80);
            int ly = y + 78;
            if (!news_.empty()) {
                text("New in this maze:", x + 10, ly, kInk, 12.5, 1);
                ly += 11;
                for (const std::string& n : news_) { text("- " + n, x + 12, ly, kInk, 11.9, 0, w - 24); ly += text_h("- " + n, 11.9, 0, w - 24) + 1; }
            } else {
                text("Find it. Mind the marble, if there is one.", x + 10, ly, kInk, 12.5, 0, w - 20);
            }
            break;
        }
        case Panel::won: {
            const int w = std::min(pw_ - 20, 250), h = 112, x = (pw_ - w) / 2, y = ph_ / 2 + 68 - h;
            window(x, y, w, h, "Found it!");
            bevel(x + 10, y + 20, 52, 52, true);
            frame_.fill_rect(x + 11, y + 21, 50, 50, hex(0xFFF8E0));
            blit(reward_tex(reward_), x + 12, y + 22, 48, 48);
            const std::string head = "You found " + reward_the(reward_) + "!";
            text(head, x + 70, y + 22, hex(0x800060), 15.0, 1, w - 80);
            std::string sub = "In " + std::to_string(sess_.play.steps) + " steps (the shortest way is " + std::to_string(sess_.lv.optimal_steps) + ").";
            if (sess_.play.steps <= sess_.lv.optimal_steps) sub = "In " + std::to_string(sess_.play.steps) + " steps: the shortest way there is!";
            text(sub, x + 70, y + 22 + text_h(head, 15.0, 1, w - 80) + 3, kInk, 12.5, 0, w - 80);
            break;
        }
        case Panel::shelf: {
            const int w = pw_ - 20, h = ph_ - 20, x = 10, y = 6;
            window(x, y, w, h, "Trophy Shelf");
            int got = 0;
            for (int i = 0; i < reward_count(); ++i) got += save_.collected >> i & 1;
            text(std::to_string(got) + " of " + std::to_string(reward_count()) + " found.  Mazes cleared: " + std::to_string(save_.cleared) +
                     ".  Shortest-way finishes: " + std::to_string(save_.perfect) + ".",
                 x + 8, y + 16, kInk, 12.5, 0);
            const int cols = std::max(4, (w - 16) / 30), cell = (w - 16) / cols;
            const int per_page=cols*std::max(1,(ph_-82)/cell);
            const int first=trophy_page_*per_page;
            for (int i = first; i < std::min(reward_count(),first+per_page); ++i) {
                const int cx = x + 8 + ((i-first) % cols) * cell, cy = y + 30 + ((i-first) / cols) * cell;
                if (cy + cell > y + h - 22) break;
                bevel(cx, cy, cell - 2, cell - 2, true);
                frame_.fill_rect(cx + 1, cy + 1, cell - 4, cell - 4, (save_.collected >> i & 1) ? hex(0xFFFFFF) : hex(0x9A9A9A));
                blit(reward_tex(i), cx + 2, cy + 2, cell - 6, cell - 6, (save_.collected >> i & 1) ? 1.f : .0f);
            }
            break;
        }
        case Panel::help: {
            const int w = std::min(pw_ - 20, 290), h = 150, x = (pw_ - w) / 2, y = ph_ / 2 + 80 - h;
            window(x, y, w, h, "Help");
            const std::string s =
                "Find the thing named in the briefing. It waits in a room at the far end, along with plenty more of it.\n"
                "Up / W walks forward, Down / S steps back, Left and Right turn. Or click: the left and right of the view turn, the middle walks.\n"
                "Pads open doors of their colour. Pads on the ceiling need you upside down: find the spinning stone.\n"
                "T: trophy shelf.  M: music.  F1: this help.";
            text(s, x + 8, y + 17, kInk, 13.5, 0, w - 16);
            break;
        }
        case Panel::credits: {
            const int w = std::min(pw_ - 20, 300), h = 168, x = (pw_ - w) / 2, y = ph_ / 2 + 88 - h;
            window(x, y, w, h, "Who's who");
            int ly = y + 17;
            if (credits_page_ == 0) {
                const std::string s =
                    "Everyone you meet in the maze is in the public domain in the United States, drawn afresh after their original illustrators, with lines of "
                    "their own (and here and there their own words from the books).\n\nTux the penguin is Larry Ewing's, made with The GIMP, drawn here with "
                    "credit and thanks.\n\nNo trademarks: the rewards carry no brand marks, and the only operating system on the walls is our own Rainstar 95.";
                text(s, x + 8, ly, kInk, 12.5, 0, w - 16);
            } else {
                const int first = (credits_page_ - 1) * 8;
                for (int i = first; i < std::min(cast_count(), first + 8); ++i) {
                    const CastMember& m = cast_member(i);
                    blit(cast_tex(i), x + 6, ly - 2, 16, 16);
                    text(m.name, x + 25, ly, kInk, 11, 1);
                    text(m.source, x + 25, ly + 8, hex(0x404040), 9.5, 0, w - 34);
                    ly += 17;
                }
            }
            break;
        }
        case Panel::none: break;
    }
    for (const Button& b : buttons_) draw_button(b);
}

void MazeView::compose() {
    texts_.clear();
    sess_.camera(r_);
    r_.time = t_;
    r_.fog_rgb = 0x000000;
    r_.begin(0x000000);
    draw_world(r_, sess_.lv, sess_.world, t_);
    std::memcpy(frame_.px.data(), r_.color.data(), r_.color.size() * 4);
    // a portal's white blink
    if (sess_.flash() > 0) frame_.fill_rect(0, 0, pw_, ph_, hex(0xFFFFFF, static_cast<float>(sess_.flash())));
    draw_speech();
    draw_status();
    draw_panel();
}

void MazeView::blit_texts(std::uint32_t* dst, size_t stride_px, double k) {
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
                struct Channel { std::uint32_t d; float cov,kk;
                    std::uint32_t operator()(int sh,float src) const {
                        return static_cast<std::uint32_t>(std::min(255.f,src*cov*255+static_cast<float>((d>>sh)&255)*kk+.5f))<<sh;
                    }
                };
                const Channel ch{d,cov,kk};
                drow[dx] = ch(0, pb) | ch(8, pg) | ch(16, pr) | (0xFFu << 24);
            }
        }
    }
}

}  // namespace mz
