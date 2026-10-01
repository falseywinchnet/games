#include "eggy_view.hpp"

#include "audio.hpp"
#include "text.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace eggy {

namespace {
const Col kInk = hex(0x101010), kFace = hex(0xC6C3BB), kLight = hex(0xFFFFFF), kShadow = hex(0x7B7870), kDark = hex(0x1A1A1A);
const Col kTitle0 = hex(0x0A246A), kTitle1 = hex(0x3A8CC8), kGoldT = hex(0xFFD23F);

std::string with_commas(double v) {
    std::string s = std::to_string(static_cast<long long>(std::floor(std::max(0.0, v))));
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
    return s;
}
const char* music_for(Biome b) {
    switch (b) {
        case Biome::meadow: return "eggy_meadow";
        case Biome::pond: return "eggy_pond";
        case Biome::forest: case Biome::autumn: return "eggy_forest";
        case Biome::ravine: return "eggy_ravine";
        case Biome::alpine: case Biome::ridge: return "eggy_highland";
        case Biome::snow: case Biome::ice: return "eggy_monastery";
        case Biome::summit: return "eggy_summit";
        default: return "eggy_meadow";
    }
}
const char* biome_lines(Biome b) {
    switch (b) {
        case Biome::meadow: return "meadow";
        case Biome::forest: case Biome::autumn: return "forest";
        case Biome::pond: return "pond_brook";
        case Biome::ravine: case Biome::ridge: return "ravine_rock";
        case Biome::alpine: return "meadow";
        case Biome::snow: case Biome::summit: return "snow";
        case Biome::ice: return "ice";
        default: return "general_walking";
    }
}
}  // namespace

EggyView::EggyView(gf::StableId id, Options opt) : Control(std::move(id)), opt_(opt) {
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    {   // we cover every pixel ourselves: no themed background fill underneath
        gf::SurfaceMaterial none;
        none.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(0, 0, 0))};
        set_authored_surface_material(none);
    }
    set_accessible_name("Eggy and the Very, Very Tall Mountain. Arrow keys or W A S D steer Eggy uphill, Space hops, "
                        "click or hold the mouse on the ground to guide him. When you stop, Eggy climbs on by himself. "
                        "F1 help, T top scores, M music, N sound, plus and minus zoom.");
    lines_.load(asset_dir() + "/lines");
    const auto path = save_path(opt_.dev);
    const bool loaded = load_save(path, save_);
    climb_started_ = loaded || std::getenv("EGGY_SKIP_TITLE");
    if (!loaded || save_.seed == 0) {
        save_ = SaveData{};
        save_.seed = static_cast<std::uint64_t>(wall_clock() * 1000) ^ 0xE66E5EEDULL;
        save_.start_wall = save_.last_wall = wall_clock();
    }
    sim_ = std::make_unique<Sim>(save_.seed);
    if (loaded) restore(*sim_, save_);
    const double now = wall_clock();
    if (loaded && save_.settings.offline_climbing && !save_.finished && save_.last_wall > 0 && now - save_.last_wall > 60) {
        const double before = sim_->world.altitude_m(sim_->d.v);
        double rows = 0;
        sim_->advance_offline(now - save_.last_wall, rows);
        away_s_ = now - save_.last_wall;
        away_m_ = sim_->world.altitude_m(sim_->d.v) - before;
    }
    if (opt_.summit) sim_->place_at(static_cast<double>(sim_->world.length()) - 40);
    else if (opt_.warp_v > 0) sim_->place_at(opt_.warp_v);
    scene_.zoom = save_.settings.zoom;
    scene_.reduced_motion = save_.settings.reduced_motion;
    name_entry_ = save_.settings.player_name;
    music_biome_ = sim_->world.row(static_cast<std::int64_t>(sim_->d.v)).biome;
    panel_ = std::getenv("EGGY_SKIP_TITLE") ? Panel::none : Panel::title;
    intro_pending_ = !save_.intro_done && sim_->d.v < kCampEnd;
    if (opt_.storm) sim_->next_storm = 3;
}

void EggyView::on_attached_to_window() {
    audio_start(asset_dir());
    subs_.push_back(visible_changed().subscribe(*this, gf::Delegate<bool>::bind<EggyView, &EggyView::cabinet_visibility>(*this)));
    cabinet_visibility(visible());
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(66));
    subs_.push_back((*timer_).tick().subscribe(*this, gf::Delegate<>::bind<EggyView, &EggyView::tick>(*this)));
    last_ = std::chrono::steady_clock::now();
    (*timer_).start();
}

void EggyView::on_detaching_from_window(gf::Window&) noexcept {
    try { persist(); } catch (...) {}
    if (timer_) (*timer_).stop();
    timer_.reset();
    audio_stop();
}

// Hand the surface to the native host's direct presentation path: new frames
// are composited by the display pipeline without repainting the control tree.
void EggyView::register_surface() {
    if (!surface_ || !attached_window()) return;
    direct_ = (*attached_window()).queue_live_surface_presentation(shared_from_this(), surface_);
}

void EggyView::cabinet_visibility(bool shown) {
    audio_cabinet(shown, cabinet_music_, cabinet_sound_);
    if (!shown) {
        for (auto& key : keys_) key = false;
        mouse_down_ = hop_edge_ = false;
        set_pointer_capture(false);
        persist();
    }
    if(timer_) (*timer_).set_interval(std::chrono::milliseconds(shown ? 66 : 1000));
}
void EggyView::set_cabinet_preferences(bool music, bool sound, bool reduced) {
    cabinet_music_=music; cabinet_sound_=sound; cabinet_reduced_=reduced;
    scene_.reduced_motion=save_.settings.reduced_motion || reduced;
    audio_cabinet(visible(),music,sound);
}
void EggyView::activate() {
    cabinet_visibility(visible());
    if (attached_window()) static_cast<void>((*attached_window()).request_focus(shared_from_this()));
}

void EggyView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    // Everything (world, HUD, dialogs) renders into one small framebuffer of
    // "game pixels"; the compositor scales it up. Few pixels = little CPU.
    const double px = save_.settings.pixel;
    pw_ = std::max(64, static_cast<int>(std::ceil(bounds.width / px)));
    ph_ = std::max(64, static_cast<int>(std::ceil(bounds.height / px)));
    frame_.resize(pw_, ph_);
    scene_.resize(pw_, ph_);
    // The published surface matches the display's physical pixels so the
    // compositor copies it 1:1; we do the (cheap, crisp) pixel-art upscale.
    bs_ = attached_window() != nullptr ? (*attached_window()).scale() : 1.0;
    phys_w_ = std::max(1, static_cast<int>(std::lround(bounds.width * bs_)));
    phys_h_ = std::max(1, static_cast<int>(std::lround(bounds.height * bs_)));
    if (surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        static_cast<void>(surface_->reconfigure(d));
    }
    layout_buttons();
}

void EggyView::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    // With direct presentation the host composites the surface itself; drawing
    // it here as well would blit every frame twice.
    if (surface_ && !direct_) p.draw_live_surface(surface_, b);
    else p.fill_rect(b, gf::Color::rgba(20, 30, 50));
}

// ------------------------------------------------------------------ speech
void EggyView::say_text(const std::string& txt, bool officer, double dur) {
    if (txt.empty()) return;
    Bubble b;
    b.text = txt;
    b.officer = officer;
    b.dur = dur > 0 ? dur : std::clamp(1.8 + txt.size() * .07, 3.0, 7.0);
    if (officer || bubble_.text.empty() || bubble_.age > bubble_.dur) bubble_ = b;
    else if (queue_.size() < 2) queue_.push_back(b);
    speech_cool_ = 7 + b.dur;
}

void EggyView::say(const std::string& cat, double chance) {
    if (speech_cool_ > 0 && cat != "drink" && cat != "star" && cat != "milestone") return;
    if (static_cast<double>(std::rand()) / RAND_MAX > chance) return;
    say_text(lines_.pick(cat));
}

// What is Eggy next to right now? Returns a ctx_ line category, or "".
std::string EggyView::surroundings() {
    const Sim& s = *sim_;
    const World& w = s.world;
    const Tile& here = w.tile(std::clamp(static_cast<int>(s.d.u), 0, kWidth - 1), static_cast<std::int64_t>(s.d.v));
    if (s.d.swim) return "ctx_water";
    if (here.surface == Surface::path && std::rand() % 4 == 0) return "ctx_path";
    std::string best;
    double bestd = 1.6;
    for (int ou = -2; ou <= 2; ++ou)
        for (int ov = -1; ov <= 3; ++ov) {
            const int tu = static_cast<int>(s.d.u) + ou;
            const std::int64_t tv = static_cast<std::int64_t>(s.d.v) + ov;
            if (tu < 0 || tu >= kWidth || tv < 0) continue;
            const Tile& t = w.tile(tu, tv);
            const char* c = nullptr;
            switch (t.feature) {
                case Feature::mushroom: c = "ctx_mushroom"; break;
                case Feature::fern: c = "ctx_fern"; break;
                case Feature::log: c = "ctx_log"; break;
                case Feature::tree: c = t.biome == Biome::autumn ? "ctx_autumn_tree" : "ctx_tree"; break;
                case Feature::pine: c = "ctx_pine"; break;
                case Feature::stump: c = "ctx_stump"; break;
                case Feature::boulder: c = "ctx_boulder"; break;
                case Feature::rock: c = "ctx_rock"; break;
                case Feature::pebble: c = "ctx_pebble"; break;
                case Feature::flower: c = "ctx_flower"; break;
                case Feature::clover: c = "ctx_clover"; break;
                case Feature::tuft: c = "ctx_tuft"; break;
                case Feature::lichen: c = "ctx_lichen"; break;
                case Feature::lily: c = "ctx_lily"; break;
                case Feature::stone: c = "ctx_stone"; break;
                case Feature::puddle: c = "ctx_puddle"; break;
                case Feature::snowdrift: c = "ctx_snowdrift"; break;
                case Feature::crystal: c = "ctx_crystal"; break;
                case Feature::ledge: c = "ctx_ledge"; break;
                case Feature::cairn: c = "ctx_cairn"; break;
                case Feature::flags: c = "ctx_flags"; break;
                case Feature::signpost: c = "ctx_signpost"; break;
                case Feature::bush: c = "ctx_tree"; break;
                default: break;
            }
            if (!c) continue;
            const double d = std::hypot(tu + t.fx - s.d.u, tv + t.fy - s.d.v);
            const double since = t_ - noticed_[c];
            if (d < bestd && since > 75) { bestd = d; best = c; }
        }
    return best;
}

std::string EggyView::self_category() {
    const Sim& s = *sim_;
    if (s.t_since_swim() < 6 && !s.d.swim) return "ctx_self_soaked";
    if (s.d.breath < .3) return "ctx_self_tired";
    const int r = std::rand() % 4;
    if (r == 0) return "ctx_self_small";
    if (r == 1) return "ctx_self_helmet";
    if (r == 2 && !s.player_mode) return "ctx_self_alone_helped";
    return "";
}

std::string EggyView::context_category() {
    const Sim& s = *sim_;
    const SegmentInfo seg = s.world.segment_at(s.d.v);
    const double r = static_cast<double>(std::rand()) / RAND_MAX;
    // mostly about where he is and what he is passing; sometimes about himself
    if (r < .35) { const std::string c = surroundings(); if (!c.empty()) { noticed_[c] = t_; return c; } }
    if (r < .47) { const std::string c = self_category(); if (!c.empty()) return c; }
    if (r < .62) return biome_lines(s.world.row(static_cast<std::int64_t>(s.d.v)).biome);
    if (r < .74) return "general_walking";
    if (r < .84) return "determination";
    if (r < .92) return "misunderstandings";
    if (s.sun() < .2) return "night_sky";
    if (seg.rain > 0) return "rain_mist";
    if (s.d.heat > .4) return "sun_heat";
    if (s.d.cold > .5) return "cold";
    if (s.wind_vis > .35) return "wind";
    return "general_walking";
}

void EggyView::handle(const Event& e) {
    Sim& s = *sim_;
    const bool snd = save_.settings.sound;
    auto rnd = [](float a, float b) { return a + (b - a) * static_cast<float>(std::rand()) / RAND_MAX; };
    scene_.on_event(e, s);
    switch (e.type) {
        case Ev::step: {
            const Surface sf = static_cast<Surface>(e.a);
            const char* n = sf == Surface::snow ? "eggy_step_snow" : (sf == Surface::rock || sf == Surface::gravel) ? "eggy_step_rock"
                          : sf == Surface::ice ? "eggy_step_ice" : sf == Surface::water ? "eggy_step_water" : "eggy_step_grass";
            audio_sfx(n, .32f, rnd(.9f, 1.15f), snd);
            break;
        }
        case Ev::swim_stroke: audio_sfx("eggy_step_water", .35f, rnd(.8f, 1.f), snd); break;
        case Ev::hop: audio_sfx("eggy_hop", .55f, rnd(.92f, 1.12f), snd); say("hop_obstacle", .06); break;
        case Ev::breath_low: say("ctx_self_tired", .35); break;
        case Ev::land: audio_sfx("eggy_land", .45f, rnd(.9f, 1.1f), snd); break;
        case Ev::splash: audio_sfx("eggy_splash", .6f, rnd(.9f, 1.1f), snd); say(std::rand() % 2 ? "ctx_water" : "pond_brook", .3); break;
        case Ev::drink: audio_sfx("eggy_drink", .6f, 1, snd); break;
        case Ev::refreshing: {
            audio_sfx("eggy_refresh", .7f, 1, snd);
            const double r = static_cast<double>(std::rand()) / RAND_MAX;
            speech_cool_ = 0;
            say_text(r < .55 ? "Refreshing!" : lines_.pick("drink"));
            break;
        }
        case Ev::breathless: audio_sfx("eggy_pant", .5f, 1, snd); say("rest_breath", .75); break;
        case Ev::rested: say("determination", .35); break;
        case Ev::knocked: audio_sfx("eggy_bonk", .7f, rnd(.95f, 1.05f), snd); break;
        case Ev::got_up: speech_cool_ = 0; say("leaf_knock", .9); break;
        case Ev::star: audio_sfx("eggy_star", .8f, 1, snd); speech_cool_ = 0; say("star"); break;
        case Ev::gust: audio_sfx("eggy_gust", std::min(1.f, .4f + static_cast<float>(e.x) * .5f), rnd(.9f, 1.1f), snd); say("wind", .35); break;
        case Ev::slide: audio_sfx("eggy_slide", .5f, 1, snd); say("ice", .5); break;
        case Ev::ledge_found: say("ctx_ledge", .5); break;
        case Ev::preen: audio_sfx("eggy_preen", .45f, rnd(.9f, 1.1f), snd); say("idle_preen_flap", .3); break;
        case Ev::flap: audio_sfx("eggy_flap", .5f, rnd(.9f, 1.15f), snd); say("idle_preen_flap", .25); break;
        case Ev::chirp: audio_sfx("eggy_chirp_0" + std::to_string(1 + std::rand() % 6), .45f, rnd(.95f, 1.25f), snd); break;
        case Ev::shiver: say("cold", .6); break;
        case Ev::fan: say("sun_heat", .6); break;
        case Ev::look: say("misunderstandings", .3); break;
        case Ev::milestone: audio_sfx("eggy_milestone", .6f, 1, snd); speech_cool_ = 0; say("milestone", .8); break;
        case Ev::biome:
            banner_ = std::string("~ ") + biome_name(static_cast<Biome>(e.a)) + " ~";
            banner_t_ = 5;
            audio_sfx("eggy_biome", .45f, 1, snd);
            say(biome_lines(static_cast<Biome>(e.a)), .7);
            break;
        case Ev::helped: say("player_help", .55); break;
        case Ev::storm_begin: banner_ = "~ A storm rolls in ~"; banner_t_ = 5; speech_cool_ = 0; say("storm", .9); break;
        case Ev::storm_end: say("determination", .6); break;
        case Ev::thunder: {
            const bool near = e.x < .4;
            audio_sfx(near ? "eggy_thunder_near" : "eggy_thunder_far", static_cast<float>(1.0 - .5 * e.x), .9f + .2f * static_cast<float>(std::rand() % 10) / 10, snd);
            audio_duck_music(.35f);
            if (near) say("storm", .35);
            break;
        }
        case Ev::auto_on: say("determination", .4); break;
        case Ev::summit_seen: speech_cool_ = 0; say_text("Is that... a TENT? Up here?", false, 3.5); audio_duck_music(.6f); break;
        case Ev::tent_open: audio_sfx("eggy_tent", .7f, 1, snd); break;
        case Ev::general_out: say_text("Private Eggy! You made it!", true, 3.6); break;
        case Ev::salute: audio_sfx("eggy_salute", .7f, 1, snd); say_text("On behalf of the Very, Very Tall Mountain...", true, 3.5); break;
        case Ev::return_salute: say_text("Reporting from the top, sir! Refreshing!", false, 3.0); break;
        case Ev::medal: audio_sfx("eggy_medal", .8f, 1, snd); say_text("...for courage above, and above, and above...", true, 3.2); break;
        case Ev::title:
            audio_sfx("eggy_fanfare", .9f, 1, snd);
            audio_duck_music(1.f);
            say_text("I name you ELITE SPECIAL SOLDIER, FIRST CLASS!", true, 4.5);
            title_card_ = 1e9;
            break;
        case Ev::ceremony_done:
            save_.finished = true;
            save_.finish_seconds = wall_clock() - save_.start_wall;
            persist();
            open(Panel::finale);
            break;
        default: break;
    }
}

// ------------------------------------------------------------------ loop
void EggyView::tick() {
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - last_).count(), 0.0, 2.0);
    last_ = now;
    t_ += dt;
    Sim& s = *sim_;
    if (!visible()) {
        s.in=Input{};
        if(panel_ != Panel::title && panel_ != Panel::finale)
            for(double left=dt;left>1e-6;left-=.25)s.step(std::min(left,.25));
        s.events.clear();
        save_t_+=dt;
        if(save_t_>15){save_t_=0;persist();}
        return;
    }
    // input
    const bool playing = panel_ == Panel::none;
    s.in = Input{};
    if (intro_ >= 0 && (keys_[0] || keys_[1] || keys_[2] || keys_[3] || hop_edge_ || mouse_down_)) { skip_intro(); hop_edge_ = false; mouse_down_ = false; }
    if (playing && intro_ < 0) {
        s.in.iv = (keys_[0] ? 1 : 0) - (keys_[1] ? 1 : 0);
        s.in.iu = (keys_[3] ? 1 : 0) - (keys_[2] ? 1 : 0);
        s.in.hop = hop_edge_;
        if (mouse_down_) {
            double u, v;
            if (scene_.screen_to_ground(s, mouse_x_, mouse_y_, u, v)) {
                s.in.has_target = true;
                s.in.tu = std::clamp(u, .3, kWidth - .3);
                s.in.tv = std::max(1.0, v);
            }
        }
    }
    hop_edge_ = false;
    intro_tick(dt);
    if (panel_ != Panel::finale && panel_ != Panel::title)
        for (double left = dt; left > 1e-6; left -= .25) s.step(std::min(left, .25));
    else if (panel_ == Panel::title) { s.events.clear(); }
    for (const auto& [name, gain] : scene_.sounds) audio_sfx(name, gain, 1, save_.settings.sound);
    scene_.sounds.clear();
    for (const Event& e : s.events) handle(e);
    s.events.clear();
    // speech pacing
    speech_cool_ = std::max(0.0, speech_cool_ - dt);
    if (!bubble_.text.empty()) {
        const int before = bubble_.shown;
        bubble_.age += dt;
        bubble_.shown = std::min(static_cast<int>(bubble_.text.size()), static_cast<int>(bubble_.age * 34));
        if (bubble_.shown / 3 != before / 3 && bubble_.shown < static_cast<int>(bubble_.text.size()) && bubble_.text[static_cast<size_t>(bubble_.shown)] != ' ')
            audio_sfx("eggy_chirp_0" + std::to_string(1 + std::rand() % 6), bubble_.officer ? .2f : .28f,
                      (bubble_.officer ? .75f : 1.05f) + .3f * static_cast<float>(std::rand()) / RAND_MAX, save_.settings.sound);
        if (bubble_.age > bubble_.dur) {
            bubble_ = Bubble{};
            if (!queue_.empty()) { bubble_ = queue_.front(); queue_.erase(queue_.begin()); }
        }
    }
    notice_t_ -= dt;
    if (notice_t_ <= 0 && playing && s.ceremony_t < 0 && speech_cool_ <= 0 && bubble_.text.empty()) {
        notice_t_ = 4;
        if (std::rand() % 5 == 0) {
            const std::string c = surroundings();
            if (!c.empty() && lines_.has(c)) { noticed_[c] = t_; say(c, 1.0); chatter_t_ = std::max(chatter_t_, 14.0); }
        }
    }
    chatter_t_ -= dt;
    if (chatter_t_ <= 0 && playing && s.ceremony_t < 0) {
        chatter_t_ = 22 + std::rand() % 40;
        say(context_category(), .85);
    }
    banner_t_ = std::max(0.0, banner_t_ - dt);
    // music follows the biome, with a little patience
    const Biome b = s.world.row(static_cast<std::int64_t>(s.d.v)).biome;
    if (b != music_biome_) { music_hold_ += dt; if (music_hold_ > 6) { music_biome_ = b; music_hold_ = 0; } }
    else music_hold_ = 0;
    std::string want = panel_ == Panel::title ? "eggy_title" : music_for(music_biome_);
    if (s.ceremony_t >= 0 || s.finished) want = "eggy_summit";
    audio_music(want, save_.settings.music);
    {
        const SegmentInfo seg = s.world.segment_at(s.d.v);
        float water = 0;
        for (int ou = -2; ou <= 2; ++ou)
            for (int ov = -2; ov <= 3; ++ov) {
                const int tu = static_cast<int>(s.d.u) + ou;
                const std::int64_t tv = static_cast<std::int64_t>(s.d.v) + ov;
                if (tu >= 0 && tu < kWidth && tv >= 0 && s.world.tile(tu, tv).surface == Surface::water) water = .55f;
            }
        const bool woods = b == Biome::forest || b == Biome::autumn;
        water = std::max(water, static_cast<float>(scene_.near_waterfall * .8));
        audio_ambience(static_cast<float>(.08 + .55 * s.wind_vis + .15 * seg.wind), water, woods ? .3f : .05f,
                       static_cast<float>(std::max(s.storm * .9, seg.rain * .35)), static_cast<float>(scene_.near_fire * .7), save_.settings.sound);
    }
    audio_tick(dt);
    // Frame pacing: a calm 15 fps while you watch, 6 fps in the background,
    // and no drawing at all while the window is hidden (Eggy still climbs).
    gf::Window* win = attached_window();
    const bool hidden = win && (*win).occluded();
    const bool front = !win || (*win).active();
    const auto pace = std::chrono::milliseconds(hidden ? 1000 : !front ? 200 : panel_ != Panel::none ? 100 : s.player_mode ? 50 : 83);
    const auto paced = std::getenv("EGGY_PACE") ? std::chrono::milliseconds(std::atoi(std::getenv("EGGY_PACE"))) : pace;
    if (timer_ && (*timer_).interval() != paced) (*timer_).set_interval(paced);
    save_t_ += dt;
    if (save_t_ > 15) { save_t_ = 0; persist(); }
    if (std::getenv("EGGY_FPS")) {
        static int frames = 0;
        static double acc = 0;
        ++frames; acc += dt;
        if (acc > 5) { std::fprintf(stderr, "ticks/s %.1f pace %lld ms hidden %d front %d\n", frames / acc, static_cast<long long>(pace.count()), hidden, front); frames = 0; acc = 0; }
    }
    // render (only once the window has given us a size)
    const bool shown = visible();
    if (!shown) audio_music("", false);  // another tab is showing: stay quiet, keep climbing
    if (hidden || !shown || scene_.r.rgb.empty() || frame_.px.empty()) return;
    static const bool no_render = std::getenv("EGGY_NO_RENDER"), no_publish = std::getenv("EGGY_NO_PUBLISH");
    if (!no_render) { scene_.render(s, t_, dt); compose(); }
    if (no_publish) return;
    // All platforms publish through the toolkit-owned CPU surface.
    if (!surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        surface_ = gf::LiveSurface::create(d);
        register_surface();
    }
    if (surface_) {
        gf::LiveSurfaceWriteLease lease = surface_->try_acquire_write();
        if (lease && static_cast<int>(lease.width()) == phys_w_ && static_cast<int>(lease.height()) == phys_h_) {
            // nearest-neighbour upscale: expand one row, then duplicate it
            std::span<std::byte> dst = lease.pixels();
            const size_t rb = lease.row_bytes();
            const double k = save_.settings.pixel * bs_;
            if (xmap_.size() != static_cast<size_t>(phys_w_)) {
                xmap_.resize(static_cast<size_t>(phys_w_));
                for (int x = 0; x < phys_w_; ++x) xmap_[static_cast<size_t>(x)] = std::min(frame_.w - 1, static_cast<int>(x / k));
            }
            const std::uint32_t* src = reinterpret_cast<const std::uint32_t*>(frame_.px.data());
            int last_src = -1;
            std::byte* last_row = nullptr;
            for (int y = 0; y < phys_h_; ++y) {
                const int sy = std::min(frame_.h - 1, static_cast<int>(y / k));
                std::byte* row = dst.data() + static_cast<size_t>(y) * rb;
                if (sy == last_src) {
                    std::memcpy(row, last_row, static_cast<size_t>(phys_w_) * 4);
                } else {
                    std::uint32_t* o = reinterpret_cast<std::uint32_t*>(row);
                    const std::uint32_t* sr = src + static_cast<size_t>(sy) * frame_.w;
                    for (int x = 0; x < phys_w_; ++x) o[x] = sr[xmap_[static_cast<size_t>(x)]];
                    last_src = sy;
                    last_row = row;
                }
            }
            blit_texts(reinterpret_cast<std::uint32_t*>(dst.data()), rb / 4, k);
            // debugging aid: EGGY_SNAPSHOT=/path.ppm EGGY_SNAPSHOT_AT=seconds writes one published frame
            if (const char* snap = std::getenv("EGGY_SNAPSHOT")) {
                static bool done = false;
                const char* at = std::getenv("EGGY_SNAPSHOT_AT");
                if (!done && t_ >= (at ? std::atof(at) : 5.0)) {
                    done = true;
                    if (FILE* f = std::fopen(snap, "wb")) {
                        std::fprintf(f, "P6 %d %d 255\n", phys_w_, phys_h_);
                        for (int y = 0; y < phys_h_; ++y)
                            for (int x = 0; x < phys_w_; ++x) {
                                const std::uint32_t p = reinterpret_cast<const std::uint32_t*>(dst.data() + static_cast<size_t>(y) * rb)[x];
                                const unsigned char q[3] = {static_cast<unsigned char>(p >> 16), static_cast<unsigned char>(p >> 8), static_cast<unsigned char>(p)};
                                std::fwrite(q, 1, 3, f);
                            }
                        std::fclose(f);
                    }
                }
            }
            if (const char* td = std::getenv("EGGY_TEST_DAMAGE")) static_cast<void>(lease.publish(gf::Rect{0, 0, std::atof(td), std::atof(td)}));
            else static_cast<void>(lease.publish());
        }
    }
    if (!direct_ || std::getenv("EGGY_NO_DIRECT")) invalidate(gf::Dirty::paint);
    text_cache_trim();
}

// ------------------------------------------------------------------ base camp
static const char* kIntro[] = {
    "Base camp. Day one. Helmet: secured.",
    "They say this mountain is so very, very tall that nobody has ever seen the top.",
    "I am a duck. A small duck. My legs are... compact.",
    "But I have a helmet, a heart, and two very determined feet.",
    "I am not here to fight anyone. Only the part of me that wants to stop.",
    "And that part is not in charge today.",
    "Somewhere up there, something is waiting for me. I can feel it in my feathers.",
    "Eggy, reporting for climbing. Hup two!",
};
constexpr int kIntroLines = 8;

void EggyView::intro_tick(double dt) {
    Sim& s = *sim_;
    if (intro_pending_ && panel_ == Panel::none) {
        intro_pending_ = false;
        intro_ = 0;
        intro_t_ = 0;
        s.hold = true;
        s.d.heading = .35;  // face the fire, and us
        say_text(kIntro[0], false, 3.6);
    }
    if (intro_ < 0) return;
    intro_t_ += dt;
    s.d.heading = .35;
    if (bubble_.text.empty() || bubble_.age > bubble_.dur) {
        ++intro_;
        if (intro_ < kIntroLines) {
            bubble_ = Bubble{};
            say_text(kIntro[intro_], false, std::clamp(1.6 + std::strlen(kIntro[intro_]) * .06, 3.0, 5.5));
            if (intro_ == kIntroLines - 1) { scene_.salute = 2.0; audio_sfx("eggy_salute", .7f, 1, save_.settings.sound); }
        } else {
            intro_ = -1;
            s.hold = false;
            save_.intro_done = true;
            persist();
        }
    }
}

void EggyView::skip_intro() {
    if (intro_ < 0 || intro_ >= kIntroLines - 1) return;
    intro_ = kIntroLines - 2;
    bubble_ = Bubble{};
}

void EggyView::persist() {
    if (!climb_started_) return;
    capture(*sim_, save_);
    save_.last_wall = wall_clock();
    save_.settings.zoom = scene_.zoom;
    save_.settings.player_name = name_entry_.empty() ? save_.settings.player_name : name_entry_;
    static_cast<void>(write_save(save_path(opt_.dev), save_));
}

void EggyView::new_climb() {
    const auto scores = save_.scores;
    const Settings settings = save_.settings;
    save_ = SaveData{};
    save_.scores = scores;
    save_.settings = settings;
    save_.seed = static_cast<std::uint64_t>(wall_clock() * 1000) ^ 0xE66E5EEDULL;
    save_.start_wall = save_.last_wall = wall_clock();
    sim_ = std::make_unique<Sim>(save_.seed);
    scene_.cam_ready = false;
    scene_.parts.clear();
    bubble_ = Bubble{};
    queue_.clear();
    title_card_ = 0;
    score_saved_ = false;
    persist();
    open(Panel::none);
    say_text("A new mountain! Hup two, hup two!");
}

// ------------------------------------------------------------------ UI
void EggyView::open(Panel p) {
    panel_ = p;
    pressed_.clear();
    layout_buttons();
}

void EggyView::layout_buttons() {
    buttons_.clear();
    const int bh = 8, y = ph_ - bh - 1;
    int x = pw_ - 2;
    auto add_right = [&](const std::string& id, const std::string& label) {
        const int w = text_w(label, 10, true) + 6;
        x -= w;
        buttons_.push_back({id, label, x, y, w, bh});
        x -= 2;
    };
    if (panel_ == Panel::none) {
        add_right("zoom_in", " + ");
        add_right("zoom_out", " - ");
        add_right("music", save_.settings.music ? "MUSIC ON" : "MUSIC OFF");
        add_right("sound", save_.settings.sound ? "SOUND ON" : "SOUND OFF");
        add_right("scores", "TOP SCORES");
        add_right("help", "HELP");
        add_right("new", "NEW CLIMB");
        return;
    }
    const int ww = std::min(pw_ - 12, 200), wh = std::min(ph_ - 12, 140);
    const int wx = (pw_ - ww) / 2, wy = (ph_ - wh) / 2;
    auto add = [&](const std::string& id, const std::string& label, bool right) {
        const int w = text_w(label, 10, true) + 10;
        buttons_.push_back({id, label, right ? wx + ww - w - 6 : wx + 6, wy + wh - 14, w, 9});
    };
    switch (panel_) {
        case Panel::title: buttons_.push_back({"start", "BEGIN", pw_ / 2 - 20, ph_ - 30, 40, 9}); break;
        case Panel::help: case Panel::scores: add("close", "CLOSE", true); break;
        case Panel::confirm: add("keep", "KEEP CLIMBING", false); add("restart", "START OVER", true); break;
        case Panel::away: add("close", "ONWARD!", true); break;
        case Panel::finale:
            if (!score_saved_) add("save_name", "SAVE NAME", false);
            add("restart", "NEW CLIMB", true);
            break;
        default: break;
    }
}

void EggyView::action(const std::string& id) {
    audio_sfx("eggy_ui_click", .5f, 1, save_.settings.sound);
    if (id == "start") {
        if (!climb_started_) {
            climb_started_ = true;
            save_.start_wall = save_.last_wall = wall_clock();
        }
        open(away_m_ > 1 ? Panel::away : Panel::none);
        persist();
    }
    else if (id == "close" || id == "keep") open(Panel::none);
    else if (id == "help") open(Panel::help);
    else if (id == "scores") open(Panel::scores);
    else if (id == "new") open(Panel::confirm);
    else if (id == "restart") new_climb();
    else if (id == "sound") { save_.settings.sound = !save_.settings.sound; layout_buttons(); }
    else if (id == "music") { save_.settings.music = !save_.settings.music; layout_buttons(); }
    else if (id == "zoom_in") scene_.zoom = std::min(1.6, scene_.zoom + .1);
    else if (id == "zoom_out") scene_.zoom = std::max(.6, scene_.zoom - .1);
    else if (id == "save_name" && !score_saved_) {
        TopScore t;
        t.name = name_entry_.empty() ? "EGGY FAN" : name_entry_;
        t.seconds = save_.finish_seconds;
        t.stars = sim_->stars_collected;
        t.stars_total = static_cast<int>(sim_->world.stars().size());
        add_score(save_.scores, t);
        save_.recorded = true;
        score_saved_ = true;
        save_.settings.player_name = t.name;
        persist();
        layout_buttons();
    }
    if (id == "away") open(Panel::none);
    if (panel_ == Panel::away && id == "close") open(Panel::none);
}

void EggyView::on_pointer(gf::PointerEvent& e) {
    const gf::Point local = point_from_window(e.position);
    mouse_x_ = local.x / save_.settings.pixel;
    mouse_y_ = local.y / save_.settings.pixel;
    auto hit = [&]() -> std::string {
        for (const Button& b : buttons_)
            if (mouse_x_ >= b.x && mouse_x_ < b.x + b.w && mouse_y_ >= b.y && mouse_y_ < b.y + b.h) return b.id;
        return {};
    };
    if (e.action == gf::PointerAction::wheel) {
        scene_.zoom = std::clamp(scene_.zoom + (e.wheel_delta.y > 0 ? .05 : -.05), .6, 1.6);
        e.handled = true;
        return;
    }
    if (e.action == gf::PointerAction::down && e.button == gf::PointerButton::primary) {
        activate();
        const std::string h = hit();
        if (!h.empty()) { pressed_ = h; e.handled = true; return; }
        if (panel_ == Panel::title) { action("start"); e.handled = true; return; }
        if (panel_ == Panel::none) { mouse_down_ = true; set_pointer_capture(true); }
        e.handled = true;
    } else if (e.action == gf::PointerAction::up) {
        if (!pressed_.empty() && hit() == pressed_) action(pressed_);
        pressed_.clear();
        if (mouse_down_) set_pointer_capture(false);
        mouse_down_ = false;
        e.handled = true;
    }
}

void EggyView::on_key(gf::KeyEvent& e) {
    if (e.handled) return;
    using K = gf::PhysicalKey;
    const bool down = e.action == gf::KeyAction::down;
    const std::uint32_t k = e.physical_key;
    int idx = -1;
    if (k == K::up || k == K::w) idx = 0;
    else if (k == K::down || k == K::s) idx = 1;
    else if (k == K::left || k == K::a) idx = 2;
    else if (k == K::right || k == K::d) idx = 3;
    if (panel_ == Panel::finale && !score_saved_) {
        if (down && k == K::backspace && !name_entry_.empty()) { name_entry_.pop_back(); audio_sfx("eggy_ui_key", .4f, .8f, save_.settings.sound); }
        if (down && k == K::enter) action("save_name");
        e.handled = true;
        return;
    }
    if (idx >= 0) {
        keys_[idx] = down && panel_ == Panel::none;
        e.handled = true;
        return;
    }
    if (!down) return;
    if (panel_ == Panel::title) { action("start"); e.handled = true; return; }
    if (k == K::escape) { if (panel_ != Panel::none) open(Panel::none); e.handled = true; return; }
    if (k == K::space) { if (!e.repeat) hop_edge_ = true; e.handled = true; return; }
    if (k == K::f1) { open(panel_ == Panel::help ? Panel::none : Panel::help); e.handled = true; return; }
    if (k == K::t) { open(panel_ == Panel::scores ? Panel::none : Panel::scores); e.handled = true; return; }
    if (k == K::m) { action("music"); e.handled = true; return; }
    if (k == K::n) { action("sound"); e.handled = true; return; }
    if (k == K::equal) { action("zoom_in"); e.handled = true; return; }
    if (k == K::minus) { action("zoom_out"); e.handled = true; return; }
    if (k == K::enter && panel_ != Panel::none && panel_ != Panel::confirm) { action(panel_ == Panel::finale ? "save_name" : "close"); e.handled = true; }
}

void EggyView::on_text_input(gf::TextInputEvent& e) {
    if (panel_ != Panel::finale || score_saved_) return;
    for (char c : e.text_utf8) {
        if (name_entry_.size() >= 12) break;
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '.') {
            name_entry_ += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            audio_sfx("eggy_ui_key", .4f, 1.f + .1f * (std::rand() % 4), save_.settings.sound);
        }
    }
    e.handled = true;
}

// ------------------------------------------------------------------ drawing
// Shapes are drawn into the small game-pixel framebuffer; text is queued and
// drawn crisply at display resolution after the pixel-art upscale.
const Mask& EggyView::tmask(const std::string& s, bool bold, double size, int wrap_game) const {
    const double wrap_font = wrap_game > 0 ? wrap_game * save_.settings.pixel : 0;
    return text_mask(s, bold ? Font::pixel_bold : Font::pixel, size, wrap_font);
}

int EggyView::text(const std::string& s, int x, int y, Col c, double size, bool bold, int wrap, int big) {
    const Mask& m = tmask(s, bold, size, wrap > 0 ? wrap / big : 0);
    texts_.push_back({s, bold, size, wrap > 0 ? wrap / big : 0, x, y, c, big});
    return static_cast<int>(std::ceil(m.w * big / save_.settings.pixel));
}

int EggyView::text_w(const std::string& s, double size, bool bold, int big) const {
    return static_cast<int>(std::ceil(tmask(s, bold, size, 0).w * big / save_.settings.pixel));
}
int EggyView::text_h(const std::string& s, double size, bool bold, int wrap, int big) const {
    return static_cast<int>(std::ceil(tmask(s, bold, size, wrap > 0 ? wrap / big : 0).h * big / save_.settings.pixel));
}

void EggyView::draw_window(int x, int y, int w, int h, const std::string& title) {
    frame_.fill_rect(x + 2, y + 2, w, h, hex(0x000000, .35f));
    frame_.fill_rect(x, y, w, h, kDark);
    frame_.fill_rect(x, y, w - 1, h - 1, kLight);
    frame_.fill_rect(x + 1, y + 1, w - 2, h - 2, kShadow);
    frame_.fill_rect(x + 1, y + 1, w - 3, h - 3, kFace);
    frame_.begin();
    frame_.rect(x + 2, y + 2, w - 4, 8);
    frame_.fill(Paint::lin(x, 0, x + w, 0, {{0, kTitle0}, {1, kTitle1}}));
    text(title, x + 4, y + 3, kLight, 11, true);
}

void EggyView::draw_button(const Button& b) {
    const bool down = pressed_ == b.id;
    frame_.fill_rect(b.x, b.y, b.w, b.h, kDark);
    frame_.fill_rect(b.x, b.y, b.w - 1, b.h - 1, down ? kShadow : kLight);
    frame_.fill_rect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, down ? kLight : kShadow);
    frame_.fill_rect(b.x + 1, b.y + 1, b.w - 3, b.h - 3, kFace);
    const int tw = text_w(b.label, 10, true), th = text_h(b.label, 10, true);
    text(b.label, b.x + (b.w - tw) / 2 + (down ? 1 : 0), b.y + (b.h - th) / 2 + (down ? 1 : 0), kInk, 10, true);
}

void EggyView::draw_hud() {
    const Sim& s = *sim_;
    // status panel: only what Eggy has achieved, never what remains
    const int x = 3, y = 3, w = 64, h = 25;
    frame_.fill_rect(x + 1, y + 1, w, h, hex(0x000000, .35f));
    frame_.fill_rect(x, y, w, h, hex(0x10182A, .8f));
    frame_.begin(); frame_.rect(x + .5, y + .5, w - 1, h - 1); frame_.stroke(hex(0xC9A227), 1);
    int ly = y + 2;
    auto row = [&](const std::string& k, const std::string& v, Col vc) {
        text(k, x + 3, ly, hex(0x9FB4D8), 10, false);
        text(v, x + 23, ly, vc, 10, true);
        ly += 5;
    };
    row("ALTITUDE", with_commas(s.world.altitude_m(s.d.v)) + " m", hex(0xFFFFFF));
    row("STARS", std::to_string(s.stars_collected), kGoldT);
    row("CLIMBING", format_duration(std::max(0.0, (s.finished ? save_.finish_seconds : wall_clock() - save_.start_wall))), hex(0xFFFFFF));
    text("BREATH", x + 3, ly, hex(0x9FB4D8), 10, false);
    const int bx = x + 23, by = ly + 1, bw = 38, bh = 3;
    frame_.fill_rect(bx, by, bw, bh, hex(0x000000, .6f));
    const int segs = 12;
    for (int i = 0; i < segs; ++i) {
        if ((i + .5) / segs > s.d.breath) break;
        const Col c = s.d.breath < .25 ? hex(0xFF6A4A) : s.d.refreshed > 0 ? hex(0x7FE3FF) : hex(0x7BE07A);
        frame_.fill_rect(bx + 1 + i * 3, by + 1, 2, 1, c);
    }
    // mode badge
    {
        const bool helping = s.player_mode;
        const std::string m = helping ? "YOU ARE HELPING!" : "AUTOPILOT - EGGY CLIMBS ON";
        const int mw = text_w(m, 10, true) + 6, mx = pw_ - mw - 3, my = 3;
        frame_.fill_rect(mx, my, mw, 7, helping ? hex(0x1F7A3A, .88f) : hex(0x8A5A10, .85f));
        frame_.begin(); frame_.rect(mx + .5, my + .5, mw - 1, 6); frame_.stroke(hex(0xFFE9A0, .9f), 1);
        const float pulse = helping ? 1.f : static_cast<float>(.8 + .2 * std::sin(t_ * 3));
        text(m, mx + 3, my + 1, hex(0xFFFFFF, pulse), 10, true);
        if (!helping) {
            const std::string hint = "hold a key or the mouse to help";
            text(hint, pw_ - 3 - text_w(hint, 10, false), my + 9, hex(0xFFFFFF, .8f), 10, false);
        }
    }
    if (banner_t_ > 0) {
        const float a = static_cast<float>(std::min(1.0, std::min(banner_t_, 5 - banner_t_) * 1.5));
        const int bw2 = text_w(banner_, 12, true, 2);
        text(banner_, (pw_ - bw2) / 2, 24, hex(0xFFF2C0, a), 12, true, 0, 2);
    }
    frame_.fill_rect(0, ph_ - 11, pw_, 11, hex(0x10182A, .8f));
    text(std::string(biome_name(s.world.row(static_cast<std::int64_t>(s.d.v)).biome)) + (s.sun() < .2 ? "  -  night" : s.sun() < .6 ? "  -  twilight" : "  -  day"),
         3, ph_ - 8, hex(0xD8E4FF), 10, false);
    for (const Button& b : buttons_) draw_button(b);
}

void EggyView::draw_bubble() {
    if (bubble_.text.empty()) return;
    const Sim& s = *sim_;
    double hx, hy;
    if (bubble_.officer) {
        double z;
        scene_.r.project({scene_.officer_u, scene_.officer_v, s.world.ground(scene_.officer_u, scene_.officer_v) + .5 / scene_.r.height_scale}, hx, hy, z);
    } else {
        scene_.duck_screen(s, hx, hy);
    }
    const int wrap = 66;
    const std::string shown = bubble_.text.substr(0, static_cast<size_t>(std::max(1, bubble_.shown)));
    const int tw = std::min(wrap, text_w(bubble_.text, 11, false)), th = text_h(bubble_.text, 11, false, wrap);
    const int bw = tw + 5, bh = th + 3;
    const double pop = std::min(1.0, bubble_.age * 8);
    int bx = static_cast<int>(hx - bw * .3), by = static_cast<int>(hy - bh - 6 - (1 - pop) * 3);
    bx = std::clamp(bx, 3, pw_ - bw - 3);
    by = std::clamp(by, 31, ph_ - bh - 14);
    const Col bg = bubble_.officer ? hex(0xE9F2D0) : hex(0xFFFFFF);
    frame_.fill_rect(bx + 1, by + 1, bw, bh, hex(0x000000, .3f));
    frame_.fill_rect(bx - 1, by - 1, bw + 2, bh + 2, kInk);
    frame_.fill_rect(bx, by, bw, bh, bg);
    const int tx = static_cast<int>(std::clamp(hx, bx + 4.0, bx + bw - 4.0));
    for (int i = 0; i < 4; ++i) {
        frame_.fill_rect(tx - (4 - i) / 2 - 1, by + bh + i - 1, (4 - i) + 2, 1, kInk);
        frame_.fill_rect(tx - (4 - i) / 2, by + bh + i - 1, (4 - i), 1, bg);
    }
    text(shown, bx + 2, by + 1, bubble_.officer ? hex(0x2A3A10) : kInk, 11, false, wrap);
}

void EggyView::draw_panel() {
    if (panel_ == Panel::none) return;
    const Sim& s = *sim_;
    frame_.fill_rect(0, 0, pw_, ph_, hex(0x000010, panel_ == Panel::title ? .25f : .45f));
    const int ww = std::min(pw_ - 12, 200), wh = std::min(ph_ - 12, 140);
    const int wx = (pw_ - ww) / 2, wy = (ph_ - wh) / 2;
    int ly = wy + 13;
    auto line = [&](const std::string& t, Col c = kInk, bool bold = false) {
        text(t, wx + 6, ly, c, 11, bold, ww - 12);
        ly += text_h(t, 11, bold, ww - 12) + 2;
    };
    switch (panel_) {
        case Panel::title: {
            const int sc = 4;
            const Mask& t1 = text_mask("EGGY", Font::pixel_bold, 14);
            const int tx = (pw_ - t1.w * sc) / 2, ty = ph_ / 2 - 70;
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy) frame_.draw_mask(t1, tx + dx * 2, ty + dy * 2, hex(0x3A1E08), sc);
            frame_.draw_mask(t1, tx + sc, ty + sc, hex(0x000000, .5f), sc);
            frame_.draw_mask(t1, tx, ty, kGoldT, sc);
            int yy = ty + t1.h * sc + 4;
            auto centre = [&](const std::string& t, Col c, double size, bool bold, int big) {
                text(t, (pw_ - text_w(t, size, bold, big)) / 2, yy, c, size, bold, 0, big);
                yy += text_h(t, size, bold, 0, big) + 3;
            };
            centre("and the Very, Very Tall Mountain", hex(0xFFFFFF), 13, true, 2);
            centre(away_m_ > 1 ? "Eggy kept climbing while you were away." : "A tiny duck. A very, very tall mountain.", hex(0xFFF2C0), 12, false, 1);
            yy += 4;
            centre("CLICK OR PRESS ANY KEY", hex(0xFFFFFF, std::sin(t_ * 4) > 0 ? 1.f : .45f), 12, true, 1);
            buttons_.clear();
            return;
        }
        case Panel::help:
            draw_window(wx, wy, ww, wh, "HOW TO HELP EGGY");
            line("Eggy is climbing the Very, Very Tall Mountain. It is very, very tall.");
            line("HELP HIM: arrow keys or W A S D steer (UP is uphill), SPACE hops, or hold the mouse on the ground.");
            line("Stop helping and he climbs on by himself, a little slower. He never gives up. He keeps climbing even while the game is closed.");
            line("Ice is slippery: look for stone ledges. Hop logs and rocks. Leaves can bonk him. Wind slows him. Water refreshes him.");
            line("Floating stars are only caught by a helping hand. The autopilot will not chase them.");
            line("F1 help   T top scores   M music   N sound   +/- zoom   ESC close", hex(0x0A246A), true);
            line("Why can't Eggy be your screensaver? Because the ducks always find their way out of the game and onto people's desks.", hex(0x6A1A1A));
            break;
        case Panel::scores: case Panel::finale: {
            const bool fin = panel_ == Panel::finale;
            draw_window(wx, wy, ww, wh, fin ? "ELITE SPECIAL SOLDIER, FIRST CLASS" : "TOP SCORES");
            if (fin) {
                line("Eggy reached the summit! Time to complete: " + format_duration(save_.finish_seconds), kInk, true);
                line("Stars collected: " + std::to_string(s.stars_collected), kInk);
                if (!score_saved_) line("ENTER YOUR NAME:  " + name_entry_ + (std::sin(t_ * 6) > 0 ? "_" : " "), hex(0x0A246A), true);
                else line("Your name is in the book of summits.", hex(0x1F6A2A), true);
                ly += 2;
            }
            if (save_.scores.empty()) line("No summits yet. The mountain is very, very tall.", kShadow);
            for (size_t i = 0; i < save_.scores.size() && ly < wy + wh - 18; ++i) {
                const TopScore& t = save_.scores[i];
                std::ostringstream o;
                o << (i + 1) << ". " << std::left << std::setw(13) << t.name << " " << format_duration(t.seconds) << "   stars " << t.stars;
                line(o.str(), i == 0 ? hex(0x8A5A00) : kInk, i == 0);
            }
            break;
        }
        case Panel::confirm:
            draw_window(wx, wy, ww, wh, "START A NEW CLIMB?");
            line("Eggy has climbed " + with_commas(s.world.altitude_m(s.d.v)) + " m of this mountain.", kInk, true);
            line("Starting over abandons this climb. He will be fine. He will start climbing again immediately. He is like that.");
            break;
        case Panel::away:
            draw_window(wx, wy, ww, wh, "WHILE YOU WERE AWAY");
            line("You were gone for " + format_duration(away_s_) + ".");
            line("Eggy climbed " + with_commas(away_m_) + " m on his own.", kInk, true);
            line("He chirped the whole way. He did not stop. He never stops.");
            line("(Stars wait for a helping hand.)", kShadow);
            break;
        default: break;
    }
    for (const Button& b : buttons_) draw_button(b);
}

void EggyView::compose() {
    texts_.clear();
    scene_.r.present(frame_, 1, 0, 0, true);
    if (title_card_ > 0 && panel_ == Panel::none) {
        const std::string t = "ELITE SPECIAL SOLDIER";
        text(t, (pw_ - text_w(t, 14, true, 3)) / 2, 30, kGoldT, 14, true, 0, 3);
    }
    if (panel_ == Panel::title) layout_buttons();
    if (panel_ != Panel::title) draw_hud();
    if (panel_ == Panel::none) draw_bubble();
    draw_panel();
}

void EggyView::blit_texts(std::uint32_t* dst, size_t stride_px, double k) {
    const int scale_unit = std::max(1, static_cast<int>(std::lround(bs_)));
    for (const HiText& h : texts_) {
        const Mask& m = tmask(h.s, h.bold, h.size, h.wrap);
        const int sc = scale_unit * h.big;
        const int ox = static_cast<int>(h.x * k), oy = static_cast<int>(h.y * k);
        const bool shadow = h.c.r + h.c.g + h.c.b > 1.5f;
        for (int pass = shadow ? 0 : 1; pass < 2; ++pass) {
            const Col c = pass == 0 ? hex(0x000000, .7f * h.c.a) : h.c;
            const int off = pass == 0 ? sc : 0;
            const float pr = c.r * c.a, pg = c.g * c.a, pb = c.b * c.a;
            for (int my = 0; my < m.h * sc; ++my) {
                const int dy = oy + my + off;
                if (dy < 0 || dy >= phys_h_) continue;
                const std::uint8_t* srow = m.a.data() + static_cast<size_t>(my / sc) * m.w;
                std::uint32_t* drow = dst + static_cast<size_t>(dy) * stride_px;
                for (int mx = 0; mx < m.w * sc; ++mx) {
                    const int dx = ox + mx + off;
                    if (dx < 0 || dx >= phys_w_) continue;
                    const float cov = srow[mx / sc] * (1.f / 255.f);
                    if (cov <= 0) continue;
                    const std::uint32_t d = drow[dx];
                    const float a = c.a * cov, kk = 1 - a;
                    const auto ch = [&](int sh, float src) {
                        return static_cast<std::uint32_t>(std::min(255.f, src * cov * 255 + static_cast<float>((d >> sh) & 255) * kk + .5f)) << sh;
                    };
                    drow[dx] = ch(0, pb) | ch(8, pg) | ch(16, pr) | (0xFFu << 24);
                }
            }
        }
    }
}

}  // namespace eggy
