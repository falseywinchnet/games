#include "maze_view.hpp"
#include "band.hpp"
#include "art.hpp"

#include "idle.hpp"
#include "help_route.hpp"
#include "platform/audio.hpp"
#include "platform/text.hpp"
#include "residents.hpp"
#include "rewards.hpp"
#include "textures.hpp"
#include "world.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

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
    set_accessible_name("Maze 95. A first-person maze: find the reward each maze names as it begins, one maze after another. Arrow keys or WASD: up walks forward, down steps back, left and right turn. "
                        "Pads open doors of their colour. Left alone, the maze walks itself. F1 help, M music.");
    load_save(save_path(opt_.dev), save_);
    if (!save_.run_seed) { save_.run_seed = seed_now() | 1; dirty_ = true; }
    start_level(level_reached());
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
    use_poster_art(asset_dir() + "/maze/posters");
    use_resident_art(asset_dir() + "/maze/residents");
    subs_.push_back(visible_changed().subscribe(*this, gf::Delegate<bool>::bind<MazeView, &MazeView::on_visible_changed>(*this)));
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(33));
    subs_.push_back((*timer_).tick().subscribe(*this, gf::Delegate<>::bind<MazeView, &MazeView::tick>(*this)));
    // the passive player walks only while the window is in use: coming back to it wakes the maze
    subs_.push_back((*attached_window()).active_changed().subscribe(*this, gf::Delegate<bool>::bind<MazeView, &MazeView::on_window_active>(*this)));
    last_ = std::chrono::steady_clock::now();
    request_frame();
}

void MazeView::on_window_active(bool) {
    request_frame();
}

// The passive player walks while it has the maze, the window is in use and motion is not
// reduced: deliberate ambient motion, which stops for any of the three (HANDOFF.md).
bool MazeView::walking_passively() const {
    const gf::Window* window = attached_window();
    const bool result = passive_ && autowalk_ == 0 && panel_ == Panel::none && !cab_reduced_ && window != nullptr && (*window).active();
    return result;
}

void MazeView::on_detaching_from_window(gf::Window&) noexcept {
    try { persist(); } catch (...) {}
    if (timer_) (*timer_).stop();
    timer_.reset();
    pixels_attached_ = false;  // the surface is kept; a new window is given it again
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
    r_.resize(pw_ * kSamples, ph_ * kSamples);
    bs_ = attached_window() ? (*attached_window()).scale() : 1.0;
    phys_w_ = std::max(1, static_cast<int>(std::lround(bounds.width * bs_)));
    phys_h_ = std::max(1, static_cast<int>(std::lround(bounds.height * bs_)));
    // The surface takes its new size with the layout, before the next frame is drawn.
    if (pixels_.live()) static_cast<void>(pixels_.configure(phys_w_, phys_h_));
    layout_buttons();
    request_frame();
}

void MazeView::on_paint(gf::Painter& p, gf::Rect) {
    if (render_dirty_ && attached_window() && !(*attached_window()).occluded()) request_frame();
    const gf::Rect b = client_rectangle();
    if (pixels_.live()) p.draw_live_surface(pixels_.live(), b);
    else p.fill_rect(b, gf::Color::rgba(0, 128, 128));
}

// ------------------------------------------------------------------ levels
int& MazeView::level_reached() {
    return save_.difficulty == 2 ? save_.level_hard : save_.difficulty == 1 ? save_.level_medium : save_.level;
}
std::string MazeView::maze_name() const {
    const char* names[] = {"Maze ", "Medium maze ", "Hard maze "};
    return names[std::clamp(save_.difficulty, 0, 2)] + std::to_string(level_);
}
// The maze's surfaces: those chosen in Settings, and for "surprise me" a draw of the
// level's own. About one maze in three is a calm one, drawn only from the quiet surfaces.
namespace {
int surprise(int count, bool (*calm)(int), bool want_calm, std::uint64_t h) {
    std::vector<int> pool;
    for (int i = 0; i < count; ++i)
        if (calm(i) == want_calm) pool.push_back(i);
    return pool.empty() ? 0 : pool[static_cast<size_t>(h % pool.size())];
}
constexpr double kIdleSeconds = 20;       // left alone this long, the maze walks itself again
constexpr double kEncounterSeconds = 2;   // the passive player stands this long for anyone who speaks
constexpr double kAdmireSeconds = 5;      // in the reward room, before the next maze
std::uint64_t stir(std::uint64_t h) {
    h ^= h >> 31; h *= 0x7FB5D329728EA185ULL; h ^= h >> 27;
    return h;
}
}  // namespace

#ifdef GUI_FORMS_AUDIO_GENERATOR
// The band (band.hpp), synthesized on the audio thread as it plays; the view tells it the
// level's style, the blackout and the ceiling.
class LiveBand final : public gui_forms::AudioGenerator {
  public:
    LiveBand(int style, std::uint32_t seed) : band_(seed, static_cast<Style>(style)) {}
    void render(std::span<float> stereo) noexcept override {
        std::fill(stereo.begin(), stereo.end(), 0.0F);
        band_.render_add(stereo, 1.0);
    }
    MazeBand& band() { return band_; }

  private:
    MazeBand band_;
};
#endif
void MazeView::dress(Level& lv) const {
    const std::uint64_t h = stir(level_seed_ ^ 0xA27ULL);
    const bool calm = h % 3 == 0;
    lv.carpet = save_.floor_art > 0 ? std::min(save_.floor_art - 1, floor_count() - 1)
                                    : surprise(floor_count(), floor_calm, calm, stir(h + 1));
    lv.wall = save_.wall_art > 0 ? std::min(save_.wall_art - 1, wall_count() - 1)
                                 : surprise(wall_count(), wall_calm, calm, stir(h + 2));
    lv.ceiling = save_.ceiling_art > 0 ? std::min(save_.ceiling_art - 1, ceiling_count() - 1)
                                       : surprise(ceiling_count(), ceiling_calm, calm, stir(h + 3));
    hang_pictures(lv, stir(h + 4), poster_count(), art_count());
}
std::vector<games::GameSetting> MazeView::settings() const {
    std::vector<games::GameSetting> list;
    const char* labels[] = {"Walls", "Floor", "Ceiling"};
    const char* ids[] = {"walls", "floor", "ceiling"};
    const int values[] = {save_.wall_art, save_.floor_art, save_.ceiling_art};
    const int counts[] = {wall_count(), floor_count(), ceiling_count()};
    for (int k = 0; k < 3; ++k) {
        games::GameSetting s;
        s.id = ids[k];
        s.label = labels[k];
        s.kind = games::GameSetting::Kind::choice;
        s.value = values[k];
        s.choices.push_back("Surprise me");
        for (int i = 0; i < counts[k]; ++i)
            s.choices.push_back(k == 0 ? wall_name(i) : k == 1 ? floor_name(i) : ceiling_name(i));
        s.maximum = counts[k];
        s.step = 1;
        if (k == 0) s.note = "Surprise me varies them maze by maze, and now and then keeps a maze calm.";
        list.push_back(s);
    }
    return list;
}
void MazeView::change_setting(std::string_view id, double value) {
    const int v = static_cast<int>(std::lround(value));
    if (id == "walls") save_.wall_art = std::clamp(v, 0, wall_count());
    else if (id == "floor") save_.floor_art = std::clamp(v, 0, floor_count());
    else if (id == "ceiling") save_.ceiling_art = std::clamp(v, 0, ceiling_count());
    else return;
    dress(sess_.lv);  // the maze you are in changes at once
    persist();
    request_frame();
}

// Easy, Medium or Hard: each keeps its own place, and a switch goes straight to it.
void MazeView::set_difficulty(int d) {
    if (save_.difficulty == d) return;
    save_.difficulty = d;
    open(Panel::none);
    start_level(level_reached());
}

// What a maze holds: the cheese first (on Easy), then every other reward in an order shuffled
// for the run, so none comes round again until all have.
int MazeView::reward_for(int level) const {
    const Difficulty difficulty = static_cast<Difficulty>(save_.difficulty);
    if (level == 1 && difficulty == Difficulty::easy) return 0;
    const int others = reward_count() - 1;
    if (others <= 0) return 0;
    std::vector<int> order(static_cast<std::size_t>(others));
    for (int i = 0; i < others; ++i) order[static_cast<std::size_t>(i)] = i + 1;
    std::uint64_t h = stir(save_.run_seed ^ 0xC0FFEEULL ^ static_cast<std::uint64_t>(save_.difficulty) << 40);
    for (int i = others - 1; i > 0; --i) {
        h = stir(h + 0x9E3779B97F4A7C15ULL);
        const int j = static_cast<int>(h % static_cast<std::uint64_t>(i + 1));
        std::swap(order[static_cast<std::size_t>(i)], order[static_cast<std::size_t>(j)]);
    }
    const int round = (level - 1) % others;
    return order[static_cast<std::size_t>(round)];
}

void MazeView::start_level(int n) {
    awarded_ = false;
    level_ = std::max(1, n);
    level_reached() = level_;
    const Difficulty difficulty = static_cast<Difficulty>(save_.difficulty);
    // Easy's seeds are as they always were, so a saved maze is the same maze.
    const std::uint64_t seed = save_.run_seed * 0x9E3779B97F4A7C15ULL + static_cast<std::uint64_t>(level_) * 7919ULL +
                               static_cast<std::uint64_t>(save_.difficulty) * 0x5BD1E995ULL;
    Level lv = generate(params_for(difficulty, level_, seed));
    level_seed_ = seed;
    dress(lv);
    reward_ = reward_for(level_);
    sess_.reward_tex = &reward_tex(reward_);
    sess_.reward_name = reward_name(reward_);
    sess_.cast.clear();
    for (const Talker& t : visitors()) {  // who comes by: the Shiba, Apu, the Doomer (residents.hpp)
        VisitorDef v;
        v.name = t.name;
        v.tex = resident_tex(t.look);
        if (!v.tex) continue;  // without their pictures, they stay home
        for (const std::string& l : t.lines) v.lines.push_back(replace_all(l, "{R}", reward_the(reward_)));
        v.w = t.w;
        v.h = t.h;
        v.lift = t.lift;
        sess_.cast.push_back(v);
    }
    sess_.start(lv, seed ^ 0xABCDEFULL);
    autowalk_ = 0;
    wanderer_.reset(seed ^ 0x57A11ULL);
    pause_t_ = 1.2;   // a moment to take in the maze before setting off
    heard_ = sess_.speech_revision;
    // No briefing: the maze says what it holds as it begins, and whatever it has that the last
    // did not (Easy, the course), or, at Medium and Hard, whatever is met here for the first time.
    sess_.announce(maze_name() + ". Somewhere in here: " + reward_a(reward_) + ".");
    const LevelParams& p = lv.params;
    const LevelParams prev = difficulty == Difficulty::easy ? params_for(level_ - 1, seed) : LevelParams{};
    const bool has[] = {p.doors > 0, p.portals > 0 || p.sealed_portal, p.marble, p.bulbs > 0, p.snail,
                        p.floors > 1, p.ceiling_doors > 0};
    const bool had[] = {prev.doors > 0 && level_ != 2, (prev.portals > 0 || prev.sealed_portal) && level_ != 4,
                        prev.marble, prev.bulbs > 0, prev.snail, prev.floors > 1, prev.ceiling_doors > 0};
    const char* tell[] = {"Locked doors. Step on the pad of the same colour to open one.",
                          "Portals. Nothing marks them but the carpet, which is a little... off.",
                          "A giant marble rolls these halls. It will block you, never crush you.",
                          "Floating light bulbs. Touch one and the lights go out. Touch another to bring them back.",
                          "A paint snail. It redecorates. You'll see.",
                          "An elevator tile. There's another maze up there.",
                          "A flip stone, and a pad on the ceiling. Touch the stone and walk upside down."};
    for (int t = 0; t < 7; ++t) {
        const bool fresh = difficulty == Difficulty::easy ? !had[t] : !(save_.met >> t & 1);
        if (has[t] && fresh) sess_.announce(tell[t]);
        if (has[t]) save_.met |= 1ULL << t;
    }
    heard_ = sess_.speech_revision;   // its own announcements are not encounters
    play("mz_start", .8f);
    dirty_ = true;
    persist();
}

// The reward room admired: the tallies kept, and on to the next maze. There is no end.
void MazeView::advance() {
    if (awarded_) return;
    awarded_ = true;
    if (reward_ < 64) save_.collected |= 1ULL << reward_;
    ++save_.cleared;
    save_.steps += sess_.play.steps;
    if (sess_.play.steps <= sess_.lv.optimal_steps) ++save_.perfect;
    start_level(level_ + 1);
}

// You play: the passive player stands aside until you have left the maze alone a while.
void MazeView::take_over() {
    passive_ = false;
    idle_t_ = 0;
}

// The passive player's turn: it stands for anyone who speaks (a fixed beat), dawdles as it
// likes, waits for whoever is in its way, and otherwise turns and walks as the wanderer says.
void MazeView::walk_passively(double dt) {
    if (sess_.speech_revision != heard_) {
        heard_ = sess_.speech_revision;
        pause_t_ = std::max(pause_t_, kEncounterSeconds);
    }
    if (pause_t_ > 0) {
        pause_t_ -= dt;
        return;
    }
    if (sess_.busy() || sess_.won_t >= 0) return;
    const Wanderer::Move m = wanderer_.next(sess_.lv, sess_.play);
    if (m.pause > 0) {
        pause_t_ = m.pause;
        return;
    }
    if (m.dir < 0) return;
    const int me = sess_.play.dir;
    if (m.dir != me) {
        sess_.command(m.dir == (me + 3) % 4 ? Cmd::left : Cmd::right);
        return;
    }
    if (!m.walk) return;
    const Pos next{sess_.play.at.f, sess_.play.at.x + kDX[m.dir], sess_.play.at.y + kDY[m.dir]};
    const bool in_way = (sess_.world.marble.alive && (sess_.world.marble.at == next || sess_.world.marble.to == next)) ||
                        (sess_.world.visitor.alive && sess_.world.visitor.at == next);
    if (!in_way) sess_.command(Cmd::forward);
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
    else if (code == "ok") open(Panel::none);
    else if (code == "take") take_over();
    else if (code == "watch") passive_ = true;
    else if (code == "help") open(Panel::help);
    else if (code == "credits" || code == "more") action(code);
    else if (code == "next") start_level(level_ + 1);
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
    if (timer_ && visible() && cab_front_) {
        if (!(*timer_).enabled()) last_ = std::chrono::steady_clock::now();
        (*timer_).set_interval(std::chrono::milliseconds(33));
        if (!(*timer_).enabled()) (*timer_).start();
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
            {"easy","Easy",true,save_.difficulty == 0,false,true},
            {"medium","Medium",true,save_.difficulty == 1,false,true},
            {"hard","Hard",true,save_.difficulty == 2,false,true},
            {"help","Help",true,panel_ == Panel::help},
            {"credits","Who's who",true,panel_ == Panel::credits}};
}
void MazeView::run_command(std::string_view id) {
    if (id == "help" && games::route_help(*this)) return;
    if (id == "help" && panel_ == Panel::help) open(Panel::none);
    else action(std::string(id));
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
    const double elapsed = std::max(0.0,std::chrono::duration<double>(now-last_).count());
    // A quiet scene sleeps to its next event. Consume that elapsed quiet time,
    // while preserving the existing bounded step for active simulation.
    const double dt = sess_.simulation_animating() ? std::min(elapsed,.25) : elapsed;
    last_ = now;
    const bool was_animating = sess_.camera_animating() || visible_world_animation(r_,sess_.lv,sess_.world);
    const std::uint64_t old_speech = sess_.speech_revision;
    const bool was_live = panel_ == Panel::none;
    t_ += dt;
    run_script();
    if (panel_ == Panel::none && autowalk_ == 0 && !passive_) {
        idle_t_ += dt;
        if (idle_t_ > kIdleSeconds) { passive_ = true; pause_t_ = .8; heard_ = sess_.speech_revision; }
    }
    if (walking_passively()) walk_passively(dt);
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
    const bool simulation_live = panel_ == Panel::none;
    if (simulation_live) sess_.update(was_live ? dt : 0);
    for (const Event& ev : sess_.events) {
        if (ev.kind == Event::sound) play(ev.text, ev.gain, ev.rate);
        else if (ev.kind == Event::won) { /* the next maze begins once the room has been admired */ }
    }
    sess_.events.clear();
    if (sess_.won_t > kAdmireSeconds && panel_ == Panel::none) advance();
    {
        const bool on = (opt_.hosted || save_.music) && cab_music_ && cab_front_;
        // the band plays one of its three styles for each level, chosen by the level's seed
        const std::uint64_t pick = stir(level_seed_ ^ 0x5C0BEULL);
        const int style = static_cast<int>(pick % static_cast<std::uint64_t>(Style::count));
        const std::uint32_t tunes = static_cast<std::uint32_t>(pick >> 32);
#ifdef GUI_FORMS_AUDIO_GENERATOR
        if (!band_) {
            band_ = std::make_shared<LiveBand>(style, tunes);
            band_style_ = style;
            band_tunes_ = tunes;
        }
        if (style != band_style_ || tunes != band_tunes_) {
            band_style_ = style;
            band_tunes_ = tunes;
            (*band_).band().play(static_cast<Style>(style), tunes);
        }
        (*band_).band().muffle(sess_.play.blackout);
        (*band_).band().backwards(sess_.play.flipped);
        audio_live_music(band_, on && visible());
#else
        static_cast<void>(on);
        static_cast<void>(style);
        static_cast<void>(tunes);
#endif
    }
    audio_tick(dt);
    save_t_ += dt;
    if (dirty_ && save_t_ > 2) { save_t_ = 0; persist(); }
    gf::Window* win = attached_window();
    const bool hidden = win && (*win).occluded();
    const bool visible_motion = sess_.camera_animating() || visible_world_animation(r_,sess_.lv,sess_.world);
    bool changed = simulation_live && (was_animating || visible_motion || old_speech!=sess_.speech_revision);
    if (simulation_live) for (const Pos& cell:sess_.repainted) {
        if (cell.f==sess_.world.floor && visible_box(r_,{double(cell.x),double(cell.y),double(cell.f)},
                                                       {cell.x+1.0,cell.y+1.0,cell.f+1.0})) changed=true;
    }
    if (changed) render_dirty_=true;
    if (!hidden && !frame_.px.empty() && render_dirty_) {
        compose(); publish();
        render_dirty_ = pixels_.owes();  // a frame no buffer was free for is drawn next tick
    }
    double delay=std::numeric_limits<double>::infinity();
    if (panel_==Panel::none) {
        delay=sess_.next_update_delay();
        if (visible_world_animation(r_,sess_.lv,sess_.world) || autowalk_>0 || walking_passively() || sess_.won_t>=0) delay=.033;
        else if (!passive_) delay=std::min(delay,std::max(1.0,kIdleSeconds-idle_t_));   // one wake, to hand the maze back when you leave it
    }
    if (!script_.empty()) delay=std::min(delay,std::max(.001,script_.front().first-t_));
    if (audio_needs_tick()) delay=std::min(delay,.033);
    if (dirty_) delay=std::min(delay,std::max(.001,2-save_t_));
    if (timer_) {
        if (!std::isfinite(delay)) (*timer_).stop();
        else (*timer_).set_interval(std::chrono::milliseconds(std::max(1,static_cast<int>(std::ceil((hidden ? std::max(.25,delay) : delay)*1000)))));
    }
}

void MazeView::publish() {
    if (!pixels_.configure(phys_w_, phys_h_)) return;
    if (!pixels_attached_ && attached_window()) {
        static_cast<void>(pixels_.attach(*attached_window(), shared_from_this()));
        pixels_attached_ = true;
        invalidate(gf::Dirty::paint);  // the paint now shows the live surface
    }
    // Only the window pixels under what changed are enlarged again; an unchanged
    // frame with the same texts publishes nothing.
    const double k = pixel_ * bs_;
    const std::optional<render::Surface::Frame> frame = pixels_.begin(frame_, k, text_marks(k));
    if (!frame) return;
    blit_texts((*frame).target, (*frame).repair, k);
    pixels_.publish();
    ++published_frames_;
    text_cache_trim();
}

// ------------------------------------------------------------------ input
void MazeView::on_pointer(gf::PointerEvent& e) {
    const gf::Point local = point_from_window(e.position);
    mouse_x_ = local.x / pixel_;
    mouse_y_ = local.y / pixel_;
    std::string hit;
    for (const Button& button : buttons_)
        if (mouse_x_ >= button.x && mouse_x_ < button.x + button.w &&
            mouse_y_ >= button.y && mouse_y_ < button.y + button.h) { hit = button.id; break; }
    if (e.action == gf::PointerAction::move) {
        if (hover_ != hit) request_frame();
        hover_ = hit;
        set_cursor(!hover_.empty() ? gf::CursorKind::hand : gf::CursorKind::arrow);
    }
    if (e.action == gf::PointerAction::down || e.action == gf::PointerAction::up) request_frame();
    if (e.action == gf::PointerAction::down && e.button == gf::PointerButton::primary) {
        activate();
        const std::string h = hit;
        if (!h.empty()) { pressed_ = h; e.handled = true; return; }
        // a click on the line showing puts it away
        if (panel_ == Panel::none && speech_w_ > 0 && mouse_x_ >= speech_x_ && mouse_x_ < speech_x_ + speech_w_ &&
            mouse_y_ >= speech_y_ && mouse_y_ < speech_y_ + speech_h_) {
            sess_.dismiss_speech();
            e.handled = true;
            return;
        }
        if (panel_ == Panel::none) {
            take_over();
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
        if (k == K::enter || k == K::space || k == K::escape) open(Panel::none);
        else if (k == K::f1 && panel_ == Panel::help) open(Panel::none);
        e.handled = true;
        return;
    }
    const bool walking = k == K::up || k == K::w || k == K::down || k == K::s || k == K::left || k == K::a || k == K::right || k == K::d;
    if (walking) take_over();
    if (k == K::up || k == K::w) sess_.command(Cmd::forward);
    else if (k == K::down || k == K::s) sess_.command(Cmd::back);
    else if (k == K::left || k == K::a) sess_.command(Cmd::left);
    else if (k == K::right || k == K::d) sess_.command(Cmd::right);
    else if (k == K::f1 || k == K::h) open(Panel::help);
    else if (k == K::m && !opt_.hosted) action("music");
    else return;
    e.handled = true;
}

void MazeView::action(const std::string& id) {
    play("mz_click", .4f);
    if (id == "ok" || id == "close") open(Panel::none);
    else if (id == "help") { if (!games::route_help(*this)) open(Panel::help); }
    else if (id == "music") { save_.music = !save_.music; dirty_ = true; layout_buttons(); }
    else if (id == "sound") { save_.sound = !save_.sound; dirty_ = true; layout_buttons(); }
    else if (id == "restart") { open(Panel::none); start_level(level_); }
    else if (id == "easy" || id == "medium" || id == "hard") set_difficulty(id == "easy" ? 0 : id == "medium" ? 1 : 2);
    else if (id == "credits") { credits_page_ = 0; open(Panel::credits); }
    else if (id == "more") { credits_page_ = (credits_page_ + 1) % ((static_cast<int>(resident_credits().size()) + 7) / 8 + 1); layout_buttons(); }
    if (dirty_) persist();
    request_frame();
}

void MazeView::open(Panel p) {
    if (panel_ != p) last_ = std::chrono::steady_clock::now();
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
    field(2, 56, maze_name());
    field(60, pw_ - 60 - 74, "Seeking: " + reward_the(reward_));
    field(pw_ - 72, 70, "Steps: " + std::to_string(sess_.play.steps));
}

void MazeView::draw_speech() {
    speech_w_ = speech_h_ = 0;
    if (sess_.speech.empty() || panel_ != Panel::none) return;
    const Speech& sp = sess_.speech.front();
    // A resident speaks from where it stands: a bubble at it, while it shows in the view.
    if (sp.at.f >= 0 && sp.at.f == sess_.play.at.f) {
        const V3 at = cell_center(sp.at, sp.z);
        double sx = 0, sy = 0, depth = 0;
        if (r_.project(at, sx, sy, depth)) {
            sx /= kSamples;
            sy /= kSamples;
            if (sx >= 0 && sx < pw_ && sy >= 0 && sy < ph_ - 15) {
                draw_bubble(sp, sx, sy);
                return;
            }
        }
        // out of the picture: at the edge on its side, pointing that way
        const bool right = dot(at - r_.eye, r_.right()) > 0;
        draw_bubble(sp, right ? pw_ - 4.0 : 4.0, ph_ * .55);
        return;
    }
    const int w = std::min(pw_ - 20, 300), x = (pw_ - w) / 2;
    const int wrap = w - (sp.who.empty() ? 14 : 52);
    const int th = text_h(sp.text, 13.1, 0, wrap);
    const int h = std::max(sp.who.empty() ? 24 : 54, th + 22), y = ph_ - 16 - h;
    window(x, y, w, h, sp.who.empty() ? "Maze" : sp.who);
    speech_x_ = x;
    speech_y_ = y;
    speech_w_ = w;
    speech_h_ = h;
    int tx = x + 7;
    if (!sp.who.empty()) {
        // the speaker's portrait, framed
        if (const Tex32* face = portrait_of(sp.who)) {
            bevel(x + 5, y + 15, 38, 36, true);
            frame_.fill_rect(x + 6, y + 16, 36, 34, hex(0xF0E8F8));
            blit(*face, x + 6, y + 15, 36, 36);
        }
        tx = x + 47;
    }
    text(sp.text, tx, y + 16, kInk, 13.1, 0, wrap);
}

void MazeView::draw_bubble(const Speech& sp, double sx, double sy) {
    const int w = std::min(pw_ - 12, 176), wrap = w - 12;
    const int th = text_h(sp.text, 12.5, 0, wrap), nh = sp.who.empty() ? 0 : 13;
    const int h = th + nh + 9;
    const int x = std::clamp(static_cast<int>(sx) - w / 2, 4, std::max(4, pw_ - w - 4));
    // above its speaker, or below when there is no room above
    const bool above = sy - h - 10 >= 4;
    const int y = above ? static_cast<int>(sy) - h - 10 : std::min(static_cast<int>(sy) + 10, ph_ - 15 - h - 2);
    const double tail_x = std::clamp(sx, x + 10.0, x + w - 10.0);
    const double base_y = above ? y + h - .5 : y + .5;
    const double tip_y = above ? std::min(sy - 2, base_y + 9.0) : std::max(sy + 2, base_y - 9.0);
    frame_.begin();
    frame_.rrect(x + 2, y + 2, w, h, 6);
    frame_.fill(hex(0x000000, .3f));
    frame_.begin();
    frame_.rrect(x, y, w, h, 6);
    frame_.fill(hex(0xFFFDF2));
    frame_.begin();
    frame_.rrect(x + .5, y + .5, w - 1, h - 1, 6);
    frame_.stroke(kInk, 1);
    // the tail, pointing at whoever speaks
    frame_.begin();
    frame_.move(tail_x - 5, base_y);
    frame_.line(sx, tip_y);
    frame_.line(tail_x + 5, base_y);
    frame_.close();
    frame_.fill(hex(0xFFFDF2));
    frame_.stroke_line(tail_x - 5, base_y, sx, tip_y, kInk, 1);
    frame_.stroke_line(sx, tip_y, tail_x + 5, base_y, kInk, 1);
    if (!sp.who.empty()) text(sp.who, x + 6, y + 3, hex(0x9A2A7A), 11.5, 1);
    text(sp.text, x + 6, y + 3 + nh, kInk, 12.5, 0, wrap);
    speech_x_ = x;
    speech_y_ = y;
    speech_w_ = w;
    speech_h_ = h;
}

void MazeView::draw_panel() {
    if (panel_ == Panel::none) return;
    frame_.fill_rect(0, 0, pw_, ph_, hex(0x000000, .35f));
    switch (panel_) {
        case Panel::help: {
            const int w = std::min(pw_ - 20, 290), h = 150, x = (pw_ - w) / 2, y = ph_ / 2 + 80 - h;
            window(x, y, w, h, "Help");
            const std::string s =
                "Each maze holds something, named as it begins, in a room at the far end along with plenty more of it. Then the next maze; there is no end.\n"
                "Left alone, the maze walks itself. Up / W walks forward, Down / S steps back, Left and Right turn, or click: the sides of the view turn, the middle walks. Leave it a while and it walks on.\n"
                "Pads open doors of their colour. Pads on the ceiling need you upside down: find the spinning stone.  M: music.  F1: this help.";
            text(s, x + 8, y + 17, kInk, 13.5, 0, w - 16);
            break;
        }
        case Panel::credits: {
            const int w = std::min(pw_ - 20, 300), h = 168, x = (pw_ - w) / 2, y = ph_ / 2 + 88 - h;
            window(x, y, w, h, "Who's who");
            int ly = y + 17;
            if (credits_page_ == 0) {
                const std::string s =
                    "Who you meet in the maze: the internet's own residents, after their memes. The Shiba, Apu and Monke are drawn for PlaySuite in "
                    "crayon; the Wojaks are drawn to match; the rest are modelled here. Keyboard Cat plays a tune of our own.\n\nNo trademarks: "
                    "the rewards carry no brand marks, and the only operating system on the walls is our own Rainstar 95.";
                text(s, x + 8, ly, kInk, 12.5, 0, w - 16);
            } else {
                const std::vector<Credit>& who = resident_credits();
                const int first = (credits_page_ - 1) * 8;
                for (int i = first; i < std::min(static_cast<int>(who.size()), first + 8); ++i) {
                    if (const Tex32* t = resident_tex(who[static_cast<size_t>(i)].look)) blit(*t, x + 6, ly - 2, 16, 16);
                    text(who[static_cast<size_t>(i)].name, x + 25, ly, kInk, 11, 1);
                    text(who[static_cast<size_t>(i)].note, x + 25, ly + 8, hex(0x404040), 9.5, 0, w - 34);
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
    resolve_frame();
    // a portal's white blink
    if (sess_.flash() > 0) frame_.fill_rect(0, 0, pw_, ph_, hex(0xFFFFFF, static_cast<float>(sess_.flash())));
    draw_speech();
    draw_status();
    draw_panel();
}

std::vector<render::PixelSurface::Text> MazeView::text_marks(double k) const {
    const int sc = std::max(1, static_cast<int>(std::lround(bs_)));
    std::vector<render::PixelSurface::Text> marks;
    marks.reserve(texts_.size());
    for (const HiText& h : texts_) {
        const Mask& m = tmask(h.s, h.font, h.size, h.wrap);
        std::uint64_t key = std::hash<std::string>{}(h.s);
        const float parts[] = {static_cast<float>(h.font), static_cast<float>(h.size), static_cast<float>(h.wrap),
                               h.c.r, h.c.g, h.c.b, h.c.a};
        for (float part : parts) key = (key ^ std::hash<float>{}(part)) * 1099511628211ULL;
        const int x = static_cast<int>(h.x * k), y = static_cast<int>(h.y * k);
        marks.push_back({key, render::Rect{x, y, x + m.w * sc, y + m.h * sc}});
    }
    return marks;
}

// The frame is drawn at kSamples x kSamples the size the game shows and averaged down:
// edges come out clean while the texels keep the game's pixel size (the owner chose this
// from labelled candidates, 2026-10-12). Channels are averaged in pairs of lanes (red
// with blue, green alone); the sums stay well inside each lane.
void MazeView::resolve_frame() {
    const int w = r_.W, h = r_.H;
    std::uint32_t* out = reinterpret_cast<std::uint32_t*>(frame_.px.data());
    const std::uint32_t* source = r_.color.data();
    constexpr std::uint32_t count = kSamples * kSamples;
    for (int y = 0; y < ph_; ++y) {
        for (int x = 0; x < pw_; ++x) {
            std::uint32_t red_blue = 0, green = 0;
            for (int sy = 0; sy < kSamples; ++sy) {
                const std::uint32_t* row = source + static_cast<size_t>(std::min(h - 1, y * kSamples + sy)) * w;
                for (int sx = 0; sx < kSamples; ++sx) {
                    const std::uint32_t p = row[std::min(w - 1, x * kSamples + sx)];
                    red_blue += p & 0x00FF00FFu;
                    green += p & 0x0000FF00u;
                }
            }
            red_blue = ((red_blue + (count / 2) * 0x00010001u) / count) & 0x00FF00FFu;
            green = ((green + (count / 2) * 0x00000100u) / count) & 0x0000FF00u;
            out[static_cast<size_t>(y) * pw_ + x] = 0xFF000000u | red_blue | green;
        }
    }
}

void MazeView::blit_texts(const render::Target& target, render::Rect clip, double k) const {
    const int sc = std::max(1, static_cast<int>(std::lround(bs_)));
    for (const HiText& h : texts_) {
        const Mask& m = tmask(h.s, h.font, h.size, h.wrap);
        render::r2d::blit_text(target, clip, m, static_cast<int>(h.x * k), static_cast<int>(h.y * k), sc, h.c);
    }
}

}  // namespace mz
