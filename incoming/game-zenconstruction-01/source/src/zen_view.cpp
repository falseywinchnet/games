#include "zen_view.hpp"

#include "platform/audio.hpp"
#include "platform/text.hpp"

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

namespace zc {

namespace {

constexpr double kStep = 1.0 / 60.0;

// USB HID usage IDs (gui_forms::PhysicalKey's vocabulary)
constexpr std::uint32_t kKeyB = 0x05, kKeyC = 0x06, kKeyE = 0x08, kKeyF = 0x09, kKeyH = 0x0B, kKeyM = 0x10;
constexpr std::uint32_t kKeyQ = 0x14, kKeyR = 0x15, kKeyS = 0x16, kKeyW = 0x1A, kKeyZ = 0x1D;
constexpr std::uint32_t kKeyEnter = 0x28, kKeyEscape = 0x29, kKeyBackspace = 0x2A, kKeySpace = 0x2C;
constexpr std::uint32_t kKeyF1 = 0x3A, kKeyRight = 0x4F, kKeyLeft = 0x50, kKeyDown = 0x51, kKeyUp = 0x52;
constexpr std::uint32_t kKeyPageUp = 0x4B, kKeyPageDown = 0x4E;
constexpr std::uint32_t kKeyBracketLeft = 0x2F, kKeyBracketRight = 0x30;
constexpr std::uint32_t kKeyShiftLeft = 0xE1, kKeyShiftRight = 0xE5;

const Col kInk = hex(0x2A241E);
const Col kPaper = hex(0xFBF6EA);
const Col kTimber = hex(0x8A6440);
const Col kYellow = hex(0xF7BC1E);
const Col kShade = hex(0x000000, .35f);

std::string save_path(bool dev) {
    const char* name = dev ? "zen_construction-dev-v1.txt" : "zen_construction-v1.txt";
    if (const char* dir = std::getenv("GAMES_STATE_DIR")) {
        return (std::filesystem::path(dir) / name).string();
    }
    const char* home = std::getenv("HOME");
    return (std::filesystem::path(home != nullptr ? home : ".") / "Library/Application Support/Rainstar/Games" / name).string();
}

std::uint64_t fnv(const std::string& text) {
    std::uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < text.size(); i += 1) {
        h = h ^ static_cast<unsigned char>(text[i]);
        h = h * 1099511628211ull;
    }
    return h;
}

std::uint32_t fresh_seed() {
    const double now = wall_clock();
    const std::uint64_t bits = static_cast<std::uint64_t>(now * 1000.0);
    std::uint32_t h = static_cast<std::uint32_t>(bits ^ (bits >> 32));
    h = (h ^ (h >> 16)) * 0x45d9f3bu;
    h = h ^ (h >> 16);
    return (h % 900000u) + 100000u;
}

// a new site, made on a worker thread (the bowl is poured by simulation)
std::unique_ptr<Run> make_site(std::uint32_t seed, std::string company, double best) {
    std::unique_ptr<Run> run = std::make_unique<Run>();
    (*run).begin(seed, company);
    if (best > (*run).best_height()) {
        (*run).set_best(best);
    }
    return run;
}

std::string centimetres(double metres) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f cm", metres * 100);
    return std::string(buffer);
}

const char* const kGrabbed[] = {"Got it.", "Up she comes.", "Hooked!", "Nice one."};
const char* const kNewBest[] = {"New best!", "Higher than ever!", "That's a record!"};
const char* const kCollapse[] = {"Whoops! Back to the bowl with those.", "Oh no... tidying up.", "Down they come. Again!"};
const char* const kStuck[] = {"That one's wedged in. Try another.", "It won't budge. Another?"};
const char* const kCovered[] = {"Something's sitting on that one.", "Take the one on top first."};
const char* const kBack[] = {"Back it goes.", "Into the bowl."};
const char* const kJoined[] = {"Nice and steady.", "It's holding.", "Lovely."};

}  // namespace

ZenView::ZenView(gf::StableId id, Options options) : Control(std::move(id)), options_(options) {
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    {
        gf::SurfaceMaterial none;
        none.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(0, 0, 0))};
        set_authored_surface_material(none);
    }
    set_accessible_name("Zen Construction. Stack rocks from the bowl with a little crane.");
    camera_goal_ = camera_.target;
    if (!load()) {
        seed_entry_ = fresh_seed();
        name_entry_ = last_company_;
        panel_ = Panel::new_site;
    }
    if (const char* script = std::getenv("ZC_SCRIPT"); script != nullptr && options_.dev) {
        const std::string all = script;
        size_t pos = 0;
        while (pos < all.size()) {
            size_t end = all.find(',', pos);
            if (end == std::string::npos) {
                end = all.size();
            }
            const std::string item = all.substr(pos, end - pos);
            const size_t colon = item.find(':');
            if (colon != std::string::npos) {
                script_.push_back(std::pair<double, std::string>(std::atof(item.c_str()), item.substr(colon + 1)));
            }
            pos = end + 1;
        }
    }
}

void ZenView::on_attached_to_window() {
    audio_start(asset_dir());
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(16));
    subs_.push_back((*timer_).tick().subscribe(*this, gf::Delegate<>::bind<ZenView, &ZenView::tick>(*this)));
    last_ = std::chrono::steady_clock::now();
    (*timer_).start();
}

void ZenView::on_detaching_from_window(gf::Window&) noexcept {
    try {
        persist();
    } catch (...) {
    }
    if (timer_) {
        (*timer_).stop();
    }
    timer_.reset();
    presenter_.detach();
    audio_stop();
}

void ZenView::activate() {
    if (attached_window() != nullptr) {
        static_cast<void>((*attached_window()).request_focus(shared_from_this()));
    }
}

void ZenView::set_cabinet(bool foreground, bool music, bool sound) {
    cab_front_ = foreground;
    cab_music_ = music;
    cab_sound_ = sound;
}

void ZenView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    W_ = bounds.width;
    H_ = bounds.height;
    bs_ = backing_scale();
    phys_w_ = std::max(1, static_cast<int>(std::lround(W_ * bs_)));
    phys_h_ = std::max(1, static_cast<int>(std::lround(H_ * bs_)));
    frame_.resize(phys_w_, phys_h_);
    // the scene at 1.5 points a pixel: on a 2x display, three screen pixels to one
    scene_pixel_ = compact() ? 1.25 : 1.5;
    const int sw = std::max(64, static_cast<int>(std::lround(W_ / scene_pixel_)));
    const int sh = std::max(48, static_cast<int>(std::lround(H_ / scene_pixel_)));
    site_.resize(sw, sh);
    site_.set_camera(camera_);
    xmap_.clear();
    ymap_.clear();
    if (surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        static_cast<void>(surface_->reconfigure(d));
    }
    layout_buttons();
    want_render_ = true;
}

void ZenView::on_paint(gf::Painter& painter, gf::Rect) {
    const gf::Rect b = client_rectangle();
    if (surface_ && !direct_) {
        painter.draw_live_surface(surface_, b);
    } else {
        painter.fill_rect(b, gf::Color::rgba(200, 215, 225));
    }
}

// ---------------------------------------------------------------- sites and saving

void ZenView::start_new_site(const std::string& company, std::uint32_t seed) {
    store_current();
    last_company_ = company.empty() ? std::string("Pebble & Sons") : company;
    pending_ = std::async(std::launch::async, make_site, seed, last_company_, 0.0);
    pending_new_ = true;
    play("zc_reset", .8f);
    panel_ = Panel::none;
    layout_buttons();
}

void ZenView::start_over() {
    if (!run_ || pending_.valid()) {
        return;
    }
    pending_ = std::async(std::launch::async, make_site, (*run_).seed(), (*run_).company(), (*run_).best_height());
    pending_new_ = false;
    run_.reset();
    play("zc_reset", .8f);
    layout_buttons();
}

void ZenView::adopt_pending() {
    if (!pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    run_ = pending_.get();
    SavedSite saved;
    saved.company = (*run_).company();
    saved.seed = (*run_).seed();
    saved.best = (*run_).best_height();
    saved.text = (*run_).save();
    if (pending_new_ || current_ < 0) {
        sites_.push_back(saved);
        current_ = static_cast<int>(sites_.size()) - 1;
    } else {
        sites_[static_cast<size_t>(current_)] = saved;
    }
    paint_sign();
    camera_ = OrbitCamera();
    camera_goal_ = camera_.target;
    camera_dirty_ = true;
    site_.invalidate();
    say(told_controls_ ? std::string("Fresh rocks in the bowl. Pick one!") : std::string("Morning! Click a rock in the bowl and I'll fetch it."));
    set_mood(OperatorMood::cheering, 1.5);
    dirty_save_ = true;
    persist();
    layout_buttons();
}

void ZenView::switch_site(int index) {
    if (index < 0 || index >= static_cast<int>(sites_.size()) || pending_.valid()) {
        return;
    }
    if (run_ && (*run_).busy()) {
        return;
    }
    store_current();
    std::unique_ptr<Run> run = std::make_unique<Run>();
    if (!(*run).load(sites_[static_cast<size_t>(index)].text)) {
        return;
    }
    run_ = std::move(run);
    current_ = index;
    paint_sign();
    camera_dirty_ = true;
    site_.invalidate();
    hover_rock_ = -1;
    dirty_save_ = true;
    play("zc_click", .5f);
    layout_buttons();
}

// The current site's latest quiet arrangement into its record.
void ZenView::store_current() {
    if (!run_ || current_ < 0 || current_ >= static_cast<int>(sites_.size())) {
        return;
    }
    SavedSite& saved = sites_[static_cast<size_t>(current_)];
    saved.text = (*run_).save();
    saved.company = (*run_).company();
    saved.seed = (*run_).seed();
    saved.best = (*run_).best_height();
}

void ZenView::persist() {
    store_current();
    std::ostringstream body;
    body << "current=" << current_ << "\n";
    body << "music=" << (music_ ? 1 : 0) << "\n";
    body << "sound=" << (sound_ ? 1 : 0) << "\n";
    body << "company=" << last_company_ << "\n";
    for (size_t i = 0; i < sites_.size(); i += 1) {
        body << "site\n" << sites_[i].text << "end\n";
    }
    const std::string text = body.str();
    const std::string all = "ZENC1\n" + text + "check=" + std::to_string(fnv(text)) + "\n";
    const std::string path = save_path(options_.dev);
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    {
        std::ofstream file(path + ".tmp", std::ios::binary | std::ios::trunc);
        if (!file) {
            return;
        }
        file << all;
    }
    std::filesystem::rename(path + ".tmp", path, error);
    dirty_save_ = false;
}

bool ZenView::load() {
    std::ifstream file(save_path(options_.dev), std::ios::binary);
    if (!file) {
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string all = buffer.str();
    if (all.rfind("ZENC1\n", 0) != 0) {
        return false;
    }
    const size_t check = all.rfind("check=");
    if (check == std::string::npos || check < 6) {
        return false;
    }
    const std::string body = all.substr(6, check - 6);
    if (std::to_string(fnv(body)) + "\n" != all.substr(check + 6)) {
        return false;
    }
    std::istringstream in(body);
    std::string line;
    int current = -1;
    std::vector<SavedSite> sites;
    bool inside = false;
    std::string block;
    while (std::getline(in, line)) {
        if (inside) {
            if (line == "end") {
                SavedSite saved;
                saved.text = block;
                Run probe;
                if (!probe.load(block)) {
                    return false;
                }
                saved.company = probe.company();
                saved.seed = probe.seed();
                saved.best = probe.best_height();
                sites.push_back(saved);
                inside = false;
                block.clear();
            } else {
                block += line + "\n";
            }
            continue;
        }
        if (line == "site") {
            inside = true;
        } else if (line.rfind("current=", 0) == 0) {
            current = std::atoi(line.c_str() + 8);
        } else if (line == "music=0") {
            music_ = false;
        } else if (line == "sound=0") {
            sound_ = false;
        } else if (line.rfind("company=", 0) == 0) {
            last_company_ = line.substr(8);
        }
    }
    if (sites.empty()) {
        return false;
    }
    sites_ = sites;
    current_ = std::clamp(current, 0, static_cast<int>(sites_.size()) - 1);
    run_ = std::make_unique<Run>();
    if (!(*run_).load(sites_[static_cast<size_t>(current_)].text)) {
        run_.reset();
        return false;
    }
    told_controls_ = true;
    paint_sign();
    return true;
}

// ---------------------------------------------------------------- the operator

void ZenView::say(const std::string& line) {
    say_.text = line;
    say_.age = 0;
    say_.life = 2.4 + 0.045 * static_cast<double>(line.size());
    say_cooldown_ = 1.2;
}

void ZenView::say_one(const char* const* lines, int count) {
    lines_said_ += 1;
    const std::uint32_t pick = (lines_said_ * 2654435761u) >> 16;
    say(std::string(lines[pick % static_cast<std::uint32_t>(count)]));
}

void ZenView::set_mood(OperatorMood mood, double seconds) {
    mood_ = mood;
    mood_time_ = seconds;
}

void ZenView::react(const RunEvents& events) {
    if (events.grabbed) {
        play("zc_clip", .8f);
        if (!told_controls_) {
            say("Up and down arrows reach out and in, left and right swing. W and S raise and lower, Q and E turn it. Space lets go.");
            told_controls_ = true;
        } else {
            say_one(kGrabbed, 4);
        }
    }
    if (events.released) {
        play("zc_unclip", .8f);
    }
    if (events.stuck) {
        say_one(kStuck, 2);
        set_mood(OperatorMood::worried, 2.0);
        play("zc_clunk", .7f);
    }
    if (events.knocks > 0 && t_ - last_knock_ > 0.06) {
        last_knock_ = t_;
        const float gain = static_cast<float>(std::min(1.0, 0.18 + events.hardest_impact * 3));
        play("zc_knock_" + std::to_string(1 + knock_variant_ % 3), gain, static_cast<float>(0.88 + 0.24 * std::fmod(t_ * 7.3, 1.0)));
        knock_variant_ += 1;
    }
    if (events.collapsed) {
        say_one(kCollapse, 3);
        set_mood(OperatorMood::oops, 2.6);
        play("zc_tumble", .9f);
    }
    if (events.tidied) {
        play("zc_whoosh", .5f);
    }
    if (events.landed) {
        play("zc_land", .55f);
    }
    if (events.new_best) {
        say(std::string(kNewBest[lines_said_ % 3]) + "  " + centimetres((*run_).height()));
        lines_said_ += 1;
        set_mood(OperatorMood::cheering, 2.8);
        play("zc_best", .8f);
        audio_duck_music(.6f);
        // and the duck, hopping in the cab
        Later squeak;
        squeak.at = t_ + 0.45;
        squeak.name = "zc_squeak";
        squeak.gain = .5f;
        later_.push_back(squeak);
    } else if (events.settled && !events.collapsed && (*run_).stack_count() > 1 && say_cooldown_ <= 0 && say_.age > say_.life) {
        say_one(kJoined, 3);
    }
    if (events.settled) {
        dirty_save_ = true;
        save_t_ = 0;
    }
}

// ---------------------------------------------------------------- play

CraneInput ZenView::gather_input() const {
    CraneInput input;
    if (panel_ != Panel::none) {
        return input;
    }
    input.right = (keys_[kKeyRight] ? 1.0 : 0.0) - (keys_[kKeyLeft] ? 1.0 : 0.0);
    input.forward = (keys_[kKeyUp] ? 1.0 : 0.0) - (keys_[kKeyDown] ? 1.0 : 0.0);
    input.up = ((keys_[kKeyW] || keys_[kKeyPageUp]) ? 1.0 : 0.0) - ((keys_[kKeyS] || keys_[kKeyPageDown]) ? 1.0 : 0.0);
    input.yaw = (keys_[kKeyQ] ? 1.0 : 0.0) - (keys_[kKeyE] ? 1.0 : 0.0);     // turn, about the vertical
    input.pitch = (keys_[kKeyR] ? 1.0 : 0.0) - (keys_[kKeyF] ? 1.0 : 0.0);   // tip, toward or away from the crane
    input.roll = (keys_[kKeyZ] ? 1.0 : 0.0) - (keys_[kKeyC] ? 1.0 : 0.0);
    input.fine = keys_[kKeyShiftLeft] || keys_[kKeyShiftRight];
    return input;
}

void ZenView::step_play(double dt) {
    if (!run_) {
        return;
    }
    step_accumulator_ = std::min(step_accumulator_ + dt, 4 * kStep);
    int steps = 0;
    while (step_accumulator_ >= kStep && steps < 3) {
        const CraneInput input = gather_input();
        (*run_).step(input, camera_.yaw, camera_.pitch);
        react((*run_).events());
        step_accumulator_ -= kStep;
        steps += 1;
        want_render_ = true;
    }
    // the near meshes of everything out of the bowl (built once, on first need)
    for (size_t i = 0; i < (*run_).rocks.size(); i += 1) {
        RockState& state = (*run_).rocks[i];
        if (state.place != Place::bowl && state.rock.near_mesh.triangles.empty()) {
            ensure_near_mesh(state.rock);
        }
    }
    // Mina's mood follows the work unless something just happened
    if (mood_time_ > 0) {
        mood_time_ -= dt;
    } else {
        const Crane& crane = (*run_).crane;
        OperatorMood mood = OperatorMood::idle;
        if (crane.mode == CraneMode::steering) {
            const double tension = (*run_).world().hold_state().tension;
            mood = tension < 0.6 ? OperatorMood::worried : (gather_input().fine ? OperatorMood::focused : OperatorMood::watching);
            if (tension < 0.1 && !told_slack_ && say_.age > say_.life) {
                say("It's resting on something. Space lets go.");
                told_slack_ = true;
            }
        } else if (crane.mode != CraneMode::parked) {
            mood = OperatorMood::watching;
        }
        mood_ = mood;
    }
}

void ZenView::update_camera(double dt) {
    if (!run_) {
        return;
    }
    // keep the whole scene composed (bowl, crane, pile); rise with the stack and
    // lean a little toward a rock in the hooks
    const OrbitCamera composed;
    phys::Vec3 goal = composed.target;
    goal.z = std::max(composed.target.z, (*run_).stack_top() * 0.55);
    const Crane& crane = (*run_).crane;
    if (crane.attached && crane.rock >= 0) {
        const phys::Pose held = (*run_).rock_pose(crane.rock);
        goal.x = goal.x + 0.3 * (held.p.x - goal.x);
        goal.y = goal.y + 0.3 * (held.p.y - goal.y);
        goal.z = std::max(goal.z, held.p.z * 0.6);
    }
    camera_goal_ = goal;
    const double k = 1 - std::exp(-dt * 2.5);
    phys::Vec3& target = camera_.target;
    const phys::Vec3 gap = camera_goal_ - target;
    if (phys::length(gap) > 0.0005) {
        target = target + gap * k;
        camera_dirty_ = true;
    }
    if (camera_dirty_) {
        site_.set_camera(camera_);
        camera_dirty_ = false;
        want_render_ = true;
    }
}

void ZenView::update_hover() {
    if (!run_ || panel_ != Panel::none || dragging_) {
        hover_rock_ = -1;
        return;
    }
    phys::Vec3 origin;
    phys::Vec3 direction;
    site_.ray(mx_ / scene_pixel_, my_ / scene_pixel_, origin, direction);
    const int rock = (*run_).rock_at_ray(origin, direction);
    if (rock != hover_rock_) {
        want_render_ = true;
    }
    hover_rock_ = rock;
    hover_ok_ = rock >= 0 && (*run_).can_fetch(rock);
}

void ZenView::play(const std::string& name, float gain, float rate, float pan) {
    audio_sfx(name, gain, rate, sound_ && cab_sound_ && cab_front_ && visible(), pan);
}

// The beds and the odd sounds: the brook always; the motor whining up with
// the crane's speed (the hook's travel and the slings reeling), humming low
// while it holds a rock, spinning down when it stops; sounds due now; a
// bird in the trees every half minute or so.
void ZenView::step_sound(double dt) {
    const bool sound_on = sound_ && cab_sound_ && cab_front_ && visible();
    // the creek: its rush, the babble of bubbles over it, wind in the reeds
    // (loops of 37, 40 and 53 seconds, so together they never repeat)
    audio_bed("zc_rush", .13f, 1.f, sound_on);
    audio_bed("zc_brook", .17f, 1.f, sound_on);
    audio_bed("zc_reeds", .06f, 1.f, sound_on);
    double want = 0;
    bool running = false;
    double strain = 0;
    if (run_) {
        const Crane& crane = (*run_).crane;
        running = crane.mode != CraneMode::parked;
        const double travel = phys::length(crane.hook - last_hook_);
        const double speed = dt > 0 && travel < 0.3 ? travel / dt : 0.0;
        const double reel = dt > 0 ? std::abs(crane.wires - last_wires_) / dt : 0.0;
        last_hook_ = crane.hook;
        last_wires_ = crane.wires;
        if (running) {
            want = std::min(1.0, speed / 0.35 + reel * 0.4);
        }
        if (crane.attached) {
            strain = std::min(1.0, std::max(0.0, (*run_).world().hold_state().tension));
        }
    }
    motor_level_ += (want - motor_level_) * std::min(1.0, dt / 0.12);
    const float gain = running ? static_cast<float>(0.014 + 0.075 * motor_level_) : 0.f;
    const float rate = static_cast<float>((0.78 + 0.5 * motor_level_) * (1 - 0.06 * strain));
    audio_bed("zc_motor", gain, rate, sound_on);
    for (size_t i = 0; i < later_.size();) {
        if (later_[i].at <= t_) {
            play(later_[i].name, later_[i].gain);
            later_.erase(later_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            i += 1;
        }
    }
    // and what happens in it now and then: water slapping a stone, pebbles
    // knocking along the bed, a frog (sometimes answered)
    splash_timer_ -= dt;
    if (splash_timer_ <= 0) {
        sound_seed_ = sound_seed_ * 1664525u + 1013904223u;
        const int kind = static_cast<int>((sound_seed_ >> 16) % 4u);
        const float pan = static_cast<float>(((sound_seed_ >> 8) % 1000u) / 1000.0 * 1.6 - 0.8);
        const float loud = static_cast<float>(0.04 + ((sound_seed_ >> 4) % 100u) / 100.0 * 0.08);
        const float pitch = static_cast<float>(0.85 + ((sound_seed_ >> 20) % 100u) / 100.0 * 0.3);
        play("zc_splash_" + std::to_string(kind + 1), loud, pitch, pan);
        splash_timer_ = 0.5 + static_cast<double>((sound_seed_ >> 12) % 250u) / 100.0;
    }
    pebble_timer_ -= dt;
    if (pebble_timer_ <= 0) {
        sound_seed_ = sound_seed_ * 1664525u + 1013904223u;
        const int kind = static_cast<int>((sound_seed_ >> 16) % 3u);
        const float pan = static_cast<float>(((sound_seed_ >> 8) % 1000u) / 1000.0 * 1.4 - 0.7);
        play("zc_bed_" + std::to_string(kind + 1), static_cast<float>(0.05 + ((sound_seed_ >> 4) % 100u) / 100.0 * 0.05), 1.f, pan);
        pebble_timer_ = 3 + static_cast<double>((sound_seed_ >> 12) % 700u) / 100.0;
    }
    frog_timer_ -= dt;
    if (frog_timer_ <= 0) {
        sound_seed_ = sound_seed_ * 1664525u + 1013904223u;
        const int kind = static_cast<int>((sound_seed_ >> 16) % 3u);
        const float pan = static_cast<float>(((sound_seed_ >> 8) % 1000u) / 1000.0 * 1.4 - 0.7);
        const float loud = static_cast<float>(0.07 + ((sound_seed_ >> 4) % 100u) / 100.0 * 0.06);
        play("zc_frog_" + std::to_string(kind + 1), loud, 1.f, pan);
        // a reply from along the bank, now and then
        if ((sound_seed_ >> 24) % 3u == 0u) {
            Later reply;
            reply.at = t_ + 0.8 + static_cast<double>((sound_seed_ >> 2) % 120u) / 100.0;
            reply.name = "zc_frog_" + std::to_string(kind + 1);
            reply.gain = loud * 0.7f;
            later_.push_back(reply);
        }
        frog_timer_ = 25 + static_cast<double>((sound_seed_ >> 12) % 4500u) / 100.0;
    }
    bird_timer_ -= dt;
    if (bird_timer_ <= 0) {
        sound_seed_ = sound_seed_ * 1664525u + 1013904223u;
        const int kind = static_cast<int>((sound_seed_ >> 16) % 3u);
        const float pan = static_cast<float>(((sound_seed_ >> 8) % 1000u) / 1000.0 * 1.4 - 0.7);
        const float loud = static_cast<float>(0.05 + ((sound_seed_ >> 4) % 100u) / 100.0 * 0.05);
        play("zc_bird_" + std::to_string(kind + 1), loud, 1.f, pan);
        bird_timer_ = 18 + static_cast<double>((sound_seed_ >> 12) % 3200u) / 100.0;
    }
}

void ZenView::tick() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - last_).count(), 0.0, 0.1);
    last_ = now;
    t_ += dt;
    run_script();
    adopt_pending();
    const bool front = cab_front_ && visible();
    if (front) {
        step_play(dt);
        update_camera(dt);
    }
    if (say_.age < say_.life) {
        say_.age += dt;
        want_render_ = true;
    }
    say_cooldown_ = std::max(0.0, say_cooldown_ - dt);
    blink_ += dt;
    if (!pressed_.empty() && pressed_ == "reset" && hover_button_ == "reset") {
        hold_reset_ += dt;
        want_render_ = true;
        if (hold_reset_ >= 1.6) {
            pressed_.clear();
            hold_reset_ = 0;
            start_over();
        }
    } else {
        hold_reset_ = 0;
    }
    audio_music(front ? "zc_music" : "", music_ && cab_music_ && cab_front_);
    step_sound(dt);
    audio_tick(dt);
    save_t_ += dt;
    if (dirty_save_ && save_t_ > 1.0 && run_ && !(*run_).busy()) {
        persist();
    }
    gf::Window* window = attached_window();
    static const bool capturing = options_.dev && std::getenv("ZC_PRINT_WID") != nullptr;
    const bool hidden = window != nullptr && (*window).occluded() && !capturing;
    const bool active = window == nullptr || (*window).active();
    const bool shown = visible();
    // the brook keeps flowing at a gentle rate when nothing else moves
    since_render_ += dt;
    const bool busy = pending_.valid() || (run_ && (!(*run_).quiet() || (*run_).busy()));
    const double interval = busy || want_render_ ? 0.0 : 0.1;
    const std::chrono::milliseconds pace(hidden || !shown || !cab_front_ ? 500 : (active ? 16 : 100));
    if (timer_ && (*timer_).interval() != pace) {
        (*timer_).set_interval(pace);
    }
    presenter_.set_visible(shown);
    if (hidden || !shown || frame_.px.empty() || since_render_ < interval) {
        return;
    }
    since_render_ = 0;
    want_render_ = false;
    compose();
    publish();
}

void ZenView::publish() {
    if (!presenter_.attached() && std::getenv("ZC_NO_NATIVE") == nullptr) {
        presenter_.attach();
    }
    if (presenter_.attached()) {
        const gf::Rect ab = absolute_bounds();
        presenter_.frame(frame_, ab.x, ab.y, ab.width, ab.height, W_, H_);
        presenter_.texts({});
        text_cache_trim();
        return;
    }
    if (!surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        surface_ = gf::LiveSurface::create(d);
        if (surface_ && attached_window() != nullptr) {
            direct_ = (*attached_window()).queue_live_surface_presentation(shared_from_this(), surface_);
        }
    }
    if (!surface_) {
        return;
    }
    gf::LiveSurfaceWriteLease lease = surface_->try_acquire_write();
    if (lease && static_cast<int>(lease.width()) == phys_w_ && static_cast<int>(lease.height()) == phys_h_) {
        std::span<std::byte> destination = lease.pixels();
        const size_t row_bytes = lease.row_bytes();
        for (int y = 0; y < phys_h_; y += 1) {
            std::memcpy(destination.data() + static_cast<size_t>(y) * row_bytes, frame_.px.data() + static_cast<size_t>(y) * static_cast<size_t>(phys_w_) * 4,
                        static_cast<size_t>(phys_w_) * 4);
        }
        static_cast<void>(lease.publish());
    }
    if (!direct_) {
        invalidate(gf::Dirty::paint);
    }
    text_cache_trim();
}

// ---------------------------------------------------------------- input

void ZenView::action(const std::string& id) {
    play("zc_click", .5f);
    if (id == "help") {
        panel_ = panel_ == Panel::help ? Panel::none : Panel::help;
    } else if (id == "sites") {
        panel_ = panel_ == Panel::sites ? Panel::none : Panel::sites;
    } else if (id == "new") {
        name_entry_ = last_company_;
        seed_entry_ = fresh_seed();
        panel_ = Panel::new_site;
    } else if (id == "close") {
        panel_ = Panel::none;
    } else if (id == "music") {
        music_ = !music_;
        dirty_save_ = true;
    } else if (id == "start") {
        start_new_site(name_entry_, seed_entry_);
    } else if (id == "reroll") {
        seed_entry_ = fresh_seed();
    } else if (id == "release") {
        if (run_) {
            (*run_).release();
        }
    } else if (id == "throw") {
        if (run_) {
            (*run_).throw_back();
            say_one(kBack, 2);
        }
    } else if (id.rfind("site", 0) == 0) {
        switch_site(std::atoi(id.c_str() + 4));
        panel_ = Panel::none;
    } else if (id == "prev") {
        switch_site(current_ - 1);
    } else if (id == "next") {
        switch_site(current_ + 1);
    }
    layout_buttons();
    want_render_ = true;
}

void ZenView::on_pointer(gf::PointerEvent& e) {
    mx_ = e.position.x;
    my_ = e.position.y;
    std::string hit;
    for (size_t i = 0; i < buttons_.size(); i += 1) {
        const Button& b = buttons_[i];
        if (b.enabled && mx_ >= b.x && mx_ < b.x + b.w && my_ >= b.y && my_ < b.y + b.h) {
            hit = b.id;
        }
    }
    if (e.action == gf::PointerAction::wheel) {
        if (panel_ == Panel::none) {
            camera_.distance = std::clamp(camera_.distance * std::exp(-e.wheel_delta.y * 0.06), 0.7, 4.5);
            camera_.yaw = camera_.yaw + e.wheel_delta.x * 0.01;
            camera_dirty_ = true;
        }
        e.handled = true;
        return;
    }
    if (e.action == gf::PointerAction::move) {
        if (hit != hover_button_) {
            hover_button_ = hit;
            want_render_ = true;
        }
        if (dragging_) {
            const double dx = mx_ - drag_x_;
            const double dy = my_ - drag_y_;
            if (std::abs(dx) + std::abs(dy) > 2) {
                drag_moved_ = true;
            }
            camera_.yaw = camera_.yaw - dx * 0.008;
            camera_.pitch = std::clamp(camera_.pitch + dy * 0.006, 0.18, 1.35);
            drag_x_ = mx_;
            drag_y_ = my_;
            camera_dirty_ = true;
        } else {
            update_hover();
        }
        set_cursor(!hit.empty() || hover_ok_ ? gf::CursorKind::hand : gf::CursorKind::arrow);
        return;
    }
    if (e.action == gf::PointerAction::down) {
        activate();
        if (!hit.empty()) {
            pressed_ = hit;
            hover_button_ = hit;
            e.handled = true;
            return;
        }
        if (panel_ != Panel::none) {
            e.handled = true;
            return;
        }
        dragging_ = true;
        drag_moved_ = false;
        drag_x_ = mx_;
        drag_y_ = my_;
        e.handled = true;
        return;
    }
    if (e.action == gf::PointerAction::up) {
        if (!pressed_.empty()) {
            const Button* pressed_button = nullptr;
            for (size_t i = 0; i < buttons_.size(); i += 1) {
                if (buttons_[i].id == pressed_) {
                    pressed_button = &buttons_[i];
                }
            }
            if (hit == pressed_ && pressed_button != nullptr && (*pressed_button).style != 2) {
                action(pressed_);
            }
            pressed_.clear();
        } else if (dragging_) {
            dragging_ = false;
            // a click (not a drag) on a rock sends the crane for it
            if (!drag_moved_ && e.button == gf::PointerButton::primary && run_) {
                update_hover();
                if (hover_rock_ >= 0) {
                    if ((*run_).fetch(hover_rock_)) {
                        play("zc_relay", .6f);
                    } else if ((*run_).crane.mode == CraneMode::parked) {
                        say_one(kCovered, 2);
                    }
                }
            }
        }
        e.handled = true;
        want_render_ = true;
    }
}

void ZenView::on_key(gf::KeyEvent& e) {
    if (e.handled) {
        return;
    }
    const std::uint32_t k = e.physical_key;
    const bool down = e.action == gf::KeyAction::down;
    if (k < 256) {
        keys_[k] = down;
    }
    if (!down) {
        e.handled = true;
        return;
    }
    if (panel_ == Panel::new_site) {
        if (k == kKeyBackspace && !name_entry_.empty()) {
            // remove one UTF-8 character
            size_t cut = name_entry_.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(name_entry_[cut]) & 0xC0u) == 0x80u) {
                cut -= 1;
            }
            name_entry_.erase(cut);
            play("ui_name_backspace", .6f);
        } else if (k == kKeyEnter) {
            action("start");
        } else if (k == kKeyEscape && run_) {
            action("close");
        }
        want_render_ = true;
        e.handled = true;
        return;
    }
    if (panel_ != Panel::none) {
        if (k == kKeyEscape || k == kKeyEnter || k == kKeySpace || k == kKeyF1) {
            action("close");
        }
        e.handled = true;
        return;
    }
    if (k == kKeySpace) {
        action("release");
    } else if (k == kKeyB || k == kKeyBackspace) {
        action("throw");
    } else if (k == kKeyEscape && run_) {
        (*run_).cancel();
    } else if (k == kKeyH || k == kKeyF1) {
        action("help");
    } else if (k == kKeyM) {
        action("music");
    } else if (k == kKeyBracketLeft) {
        action("prev");
    } else if (k == kKeyBracketRight) {
        action("next");
    }
    e.handled = true;
}

void ZenView::on_text_input(gf::TextInputEvent& e) {
    if (panel_ != Panel::new_site) {
        return;
    }
    const std::string& typed = e.text_utf8;
    for (size_t i = 0; i < typed.size(); i += 1) {
        const unsigned char c = static_cast<unsigned char>(typed[i]);
        if (c < 0x20 || name_entry_.size() >= 28) {
            continue;
        }
        name_entry_ += static_cast<char>(c);
    }
    play("ui_name_key_0" + std::to_string(1 + (static_cast<int>(name_entry_.size()) % 3)), .55f);
    want_render_ = true;
}

namespace {
bool script_before(const std::pair<double, std::string>& a, const std::pair<double, std::string>& b) {
    return a.first < b.first;
}
}  // namespace

void ZenView::run_script() {
    while (!script_.empty() && script_.front().first <= t_) {
        const std::string c = script_.front().second;
        script_.erase(script_.begin());
        if (c.rfind("size", 0) == 0) {
            int w = 0;
            int h = 0;
            if (std::sscanf(c.c_str() + 4, "%dx%d", &w, &h) == 2) {
                dev_resize_window(w, h);
            }
        } else if (c.rfind("new=", 0) == 0) {
            start_new_site(c.substr(4), 7);
        } else if (c == "fetch" && run_) {
            // the highest rock in the bowl the crane can lift
            int best = -1;
            double best_z = -1;
            for (size_t i = 0; i < (*run_).rocks.size(); i += 1) {
                if ((*run_).rocks[i].place == Place::bowl && (*run_).can_fetch(static_cast<int>(i))) {
                    const double z = (*run_).rock_pose(static_cast<int>(i)).p.z;
                    if (z > best_z) {
                        best_z = z;
                        best = static_cast<int>(i);
                    }
                }
            }
            if (best >= 0) {
                (*run_).fetch(best);
            }
        } else if (c == "over" && run_ && (*run_).crane.attached) {
            const int base = (*run_).base_rock();
            phys::Vec3 spot = site_layout().stack_centre;
            if (base >= 0) {
                spot = (*run_).rock_pose(base).p;
            }
            (*run_).crane.target.p.x = spot.x;
            (*run_).crane.target.p.y = spot.y;
        } else if (c == "lower" && run_ && (*run_).crane.attached) {
            (*run_).crane.target.p.z = (*run_).crane.target.p.z - 0.08;
        } else if (c == "drop") {
            action("release");
        } else if (c == "back") {
            action("throw");
        } else if (c.rfind("yaw=", 0) == 0) {
            camera_.yaw = std::atof(c.c_str() + 4);
            camera_dirty_ = true;
        } else if (c.rfind("pitch=", 0) == 0) {
            camera_.pitch = std::atof(c.c_str() + 6);
            camera_dirty_ = true;
        } else if (c.rfind("dist=", 0) == 0) {
            camera_.distance = std::atof(c.c_str() + 5);
            camera_dirty_ = true;
        } else if (c == "help" || c == "sites" || c == "close" || c == "new" || c == "next" || c == "prev") {
            action(c);
        } else if (c == "startover") {
            start_over();
        } else if (c.rfind("type=", 0) == 0) {
            name_entry_ = c.substr(5);
        } else if (c.rfind("hold=", 0) == 0) {
            // hold a key for a while: hold=up,1.5 (up down left right w s q e r f z c)
            const std::string spec = c.substr(5);
            const size_t comma = spec.find(',');
            const std::string name = spec.substr(0, comma);
            const double seconds = comma == std::string::npos ? 1.0 : std::atof(spec.c_str() + comma + 1);
            const char* names[12] = {"up", "down", "left", "right", "w", "s", "q", "e", "r", "f", "z", "c"};
            const std::uint32_t codes[12] = {kKeyUp, kKeyDown, kKeyLeft, kKeyRight, kKeyW, kKeyS, kKeyQ, kKeyE, kKeyR, kKeyF, kKeyZ, kKeyC};
            for (int k = 0; k < 12; k += 1) {
                if (name == names[k]) {
                    keys_[codes[k]] = true;
                    script_.push_back(std::pair<double, std::string>(t_ + seconds, "free=" + std::to_string(codes[k])));
                    std::stable_sort(script_.begin(), script_.end(), script_before);
                }
            }
        } else if (c.rfind("free=", 0) == 0) {
            keys_[std::atoi(c.c_str() + 5) & 255] = false;
        }
    }
}

// ---------------------------------------------------------------- drawing helpers

void ZenView::text(const std::string& s, double x, double y, Col c, double size, int font, double wrap, int align) {
    if (s.empty()) {
        return;
    }
    const Font f = font == 1 ? Font::speech_bold : font == 2 ? Font::title : font == 3 ? Font::ui : Font::speech;
    const Mask& m = text_mask(s, f, size * bs_, wrap > 0 ? wrap * bs_ : 0);
    double px = x * bs_;
    if (align == 1) {
        px -= m.w / 2.0;
    } else if (align == 2) {
        px -= m.w;
    }
    frame_.draw_mask(m, static_cast<int>(std::lround(px)), static_cast<int>(std::lround(y * bs_)), c, 1);
}

double ZenView::text_w(const std::string& s, double size, int font) const {
    const Font f = font == 1 ? Font::speech_bold : font == 2 ? Font::title : font == 3 ? Font::ui : Font::speech;
    return text_mask(s, f, size * bs_, 0).w / bs_;
}

double ZenView::text_h(const std::string& s, double size, int font, double wrap) const {
    const Font f = font == 1 ? Font::speech_bold : font == 2 ? Font::title : font == 3 ? Font::ui : Font::speech;
    return text_mask(s, f, size * bs_, wrap > 0 ? wrap * bs_ : 0).h / bs_;
}

void ZenView::rrect(double x, double y, double w, double h, double r, Col fill, Col line, double width) {
    frame_.begin();
    frame_.rrect(x * bs_, y * bs_, w * bs_, h * bs_, r * bs_);
    frame_.fill(fill);
    if (width > 0) {
        frame_.begin();
        frame_.rrect((x + width * 0.5) * bs_, (y + width * 0.5) * bs_, (w - width) * bs_, (h - width) * bs_, r * bs_);
        frame_.stroke(line, width * bs_);
    }
}

// The company's name, painted on the sign's board.
void ZenView::paint_sign() {
    if (!run_) {
        return;
    }
    Canvas board;
    board.resize(256, 128);
    board.clear(hex(0xF4ECD6));
    board.begin();
    board.rect(6, 6, 244, 116);
    board.stroke(hex(0x6A4A2A), 6);
    const std::string& name = (*run_).company();
    double size = 44;
    const Mask* mask = &text_mask(name, Font::speech_bold, size, 228);
    while ((mask->h > 104 || mask->w > 228) && size > 14) {
        size -= 3;
        mask = &text_mask(name, Font::speech_bold, size, 228);
    }
    board.draw_mask(*mask, (256 - mask->w) / 2, (128 - mask->h) / 2, hex(0x2E2014));
    Tex tex;
    tex.make(256, 128);
    for (int y = 0; y < 128; y += 1) {
        for (int x = 0; x < 256; x += 1) {
            const std::uint8_t* p = board.px.data() + (static_cast<size_t>(y) * 256 + static_cast<size_t>(x)) * 4;
            tex.at(x, y) = 0xFF000000u | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | static_cast<std::uint32_t>(p[0]);
        }
    }
    site_.set_sign(tex);
}

// ---------------------------------------------------------------- layout

void ZenView::layout_buttons() {
    buttons_.clear();
    const bool small = compact();
    if (panel_ == Panel::new_site) {
        const double w = std::min(W_ - 24, 440.0);
        const double h = 230;
        const double x = (W_ - w) / 2;
        const double y = std::max(10.0, (H_ - h) / 2);
        Button start{"start", "Start the site", x + w - 170, y + h - 46, 150, 32, !name_entry_.empty() || true, 1};
        buttons_.push_back(start);
        Button reroll{"reroll", "Other rocks", x + 20, y + h - 46, 120, 32, true, 0};
        buttons_.push_back(reroll);
        if (run_) {
            Button cancel{"close", "Cancel", x + w - 170 - 100, y + h - 46, 88, 32, true, 0};
            buttons_.push_back(cancel);
        }
        return;
    }
    if (panel_ == Panel::sites) {
        const double w = std::min(W_ - 24, 520.0);
        const double rows = std::max(1.0, std::min(static_cast<double>(sites_.size()), std::floor((H_ - 140) / 34)));
        const double h = 74 + rows * 34 + 44;
        const double x = (W_ - w) / 2;
        const double y = std::max(10.0, (H_ - h) / 2);
        int first = 0;
        if (static_cast<int>(sites_.size()) > static_cast<int>(rows)) {
            first = std::clamp(current_ - static_cast<int>(rows) / 2, 0, static_cast<int>(sites_.size()) - static_cast<int>(rows));
        }
        for (int i = 0; i < static_cast<int>(rows); i += 1) {
            const int index = first + i;
            if (index >= static_cast<int>(sites_.size())) {
                break;
            }
            const SavedSite& saved = sites_[static_cast<size_t>(index)];
            char label[160];
            std::snprintf(label, sizeof label, "%s   (rocks no. %u)   best %.1f cm", saved.company.c_str(), saved.seed, saved.best * 100);
            Button row{"site" + std::to_string(index), label, x + 16, y + 60 + i * 34, w - 32, 30, true, index == current_ ? 1 : 0};
            buttons_.push_back(row);
        }
        Button close{"close", "Close", x + w / 2 - 50, y + h - 40, 100, 30, true, 0};
        buttons_.push_back(close);
        return;
    }
    if (panel_ == Panel::help) {
        const double h = std::min(H_ - 60, 420.0);
        const double y = std::max(10.0, (H_ - h) / 2);
        Button close{"close", "Close", W_ / 2 - 50, y + h - 42, 100, 30, true, 0};
        buttons_.push_back(close);
        return;
    }
    // the top-right row: sized to their labels, from the right
    double x = W_ - 10;
    const double y = 10;
    const double bh = small ? 24 : 28;
    const double size = small ? 11.5 : 12.5;
    const char* labels[5][2] = {{"music", music_ ? "Music" : "Quiet"}, {"help", "Help"}, {"reset", "Start over"}, {"new", "New site"}, {"sites", "Sites"}};
    for (int i = 0; i < 5; i += 1) {
        const std::string label = labels[i][1];
        const double w = text_w(label, size, 0) + (small ? 16 : 22);
        x -= w;
        Button b{labels[i][0], label, x, y, w, bh, true, i == 2 ? 2 : 0};
        if (i == 2) {
            b.enabled = run_ != nullptr && !pending_.valid() && !(*run_).busy();
        }
        if (i == 4) {
            b.enabled = sites_.size() > 0;
        }
        buttons_.push_back(b);
        x -= 6;
    }
    // while the crane holds a rock: let go, back to the bowl
    if (run_ && (*run_).crane.attached && (*run_).crane.mode == CraneMode::steering) {
        const double bw = small ? 86 : 104;
        Button let_go{"release", "Let go", W_ - 10 - bw, H_ - 10 - bh, bw, bh, true, 1};
        Button back{"throw", "Back to bowl", W_ - 10 - bw - 8 - (bw + 14), H_ - 10 - bh, bw + 14, bh, true, 0};
        buttons_.push_back(let_go);
        buttons_.push_back(back);
    }
}

// ---------------------------------------------------------------- compose

// The low-resolution scene, upscaled nearest-neighbour with a 4 x 4 ordered
// dither into the physical frame.
void ZenView::present_scene() {
    const R3D& r = site_.r;
    if (static_cast<int>(xmap_.size()) != phys_w_) {
        xmap_.resize(static_cast<size_t>(phys_w_));
        for (int x = 0; x < phys_w_; x += 1) {
            xmap_[static_cast<size_t>(x)] = std::min(r.W - 1, static_cast<int>(static_cast<double>(x) * r.W / phys_w_));
        }
    }
    if (static_cast<int>(ymap_.size()) != phys_h_) {
        ymap_.resize(static_cast<size_t>(phys_h_));
        for (int y = 0; y < phys_h_; y += 1) {
            ymap_[static_cast<size_t>(y)] = std::min(r.H - 1, static_cast<int>(static_cast<double>(y) * r.H / phys_h_));
        }
    }
    static const int bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    std::vector<std::uint32_t> row(static_cast<size_t>(r.W));
    int last_source = -1;
    for (int y = 0; y < phys_h_; y += 1) {
        const int sy = ymap_[static_cast<size_t>(y)];
        if (sy != last_source) {
            const float* c = r.rgb.data() + static_cast<size_t>(sy) * static_cast<size_t>(r.W) * 3;
            for (int x = 0; x < r.W; x += 1) {
                const float threshold = (static_cast<float>(bayer[(sy & 3) * 4 + (x & 3)]) + 0.5f) / 16.f - 0.5f;
                const float rf = std::clamp(c[x * 3] * 255.f + threshold * 6.f, 0.f, 255.f);
                const float gf_ = std::clamp(c[x * 3 + 1] * 255.f + threshold * 6.f, 0.f, 255.f);
                const float bf = std::clamp(c[x * 3 + 2] * 255.f + threshold * 6.f, 0.f, 255.f);
                row[static_cast<size_t>(x)] = 0xFF000000u | (static_cast<std::uint32_t>(rf) << 16) | (static_cast<std::uint32_t>(gf_) << 8) |
                                              static_cast<std::uint32_t>(bf);
            }
            last_source = sy;
        }
        std::uint32_t* out = reinterpret_cast<std::uint32_t*>(frame_.px.data() + static_cast<size_t>(y) * static_cast<size_t>(phys_w_) * 4);
        for (int x = 0; x < phys_w_; x += 1) {
            out[x] = row[static_cast<size_t>(xmap_[static_cast<size_t>(x)])];
        }
    }
}

void ZenView::compose() {
    if (run_) {
        SceneState state;
        state.run = run_.get();
        state.time = t_;
        state.hover_rock = (*run_).crane.mode == CraneMode::parked ? hover_rock_ : -1;
        state.hover_ok = hover_ok_;
        state.mood = mood_;
        site_.render(state, (*run_).quiet() && !(*run_).busy());
        present_scene();
        draw_height_marks();
        draw_hud();
    } else {
        frame_.clear(hex(0xC8D7DF));
        draw_loading();
    }
    layout_buttons();
    draw_panel();
    draw_buttons();
}

void ZenView::draw_loading() {
    const double w = std::min(W_ - 40, 360.0);
    const double h = 96;
    const double x = (W_ - w) / 2;
    const double y = (H_ - h) / 2;
    if (!pending_.valid()) {
        return;
    }
    rrect(x, y, w, h, 12, kPaper, kTimber, 2);
    text("Filling the bowl...", x + w / 2, y + 22, kInk, 18, 2, 0, 1);
    const int dots = static_cast<int>(t_ * 3) % 4;
    text(std::string(static_cast<size_t>(dots), '.') + " Mina's tipping in fresh rocks " + std::string(static_cast<size_t>(dots), '.'), x + w / 2, y + 56,
         hex(0x6A5A44), 12, 0, 0, 1);
}

// The stack's height and the best, marked beside the stack.
void ZenView::draw_height_marks() {
    if (!run_ || (*run_).stack_count() == 0) {
        return;
    }
    const int base = (*run_).base_rock();
    if (base < 0) {
        return;
    }
    const phys::Pose foot = (*run_).rock_pose(base);
    const double side = 0.16;
    const phys::Vec3 right{std::cos(camera_.yaw), std::sin(camera_.yaw), 0};
    double sx = 0;
    double sy = 0;
    double gx = 0;
    double gy = 0;
    const phys::Vec3 ground = foot.p + right * side;
    const phys::Vec3 top{ground.x, ground.y, (*run_).height()};
    if (!site_.project(phys::Vec3{ground.x, ground.y, 0}, gx, gy) || !site_.project(top, sx, sy)) {
        return;
    }
    const double k = scene_pixel_;
    // a slim measuring staff from the sand to the top
    frame_.stroke_line(gx * k * bs_, gy * k * bs_, sx * k * bs_, sy * k * bs_, hex(0xFFFFFF, .55f), 1.2 * bs_);
    frame_.stroke_line((sx * k - 9) * bs_, sy * k * bs_, (sx * k + 9) * bs_, sy * k * bs_, hex(0xFFFFFF, .9f), 2 * bs_);
    const std::string label = centimetres((*run_).height());
    const double lw = text_w(label, 12, 1) + 12;
    rrect(sx * k + 12, sy * k - 10, lw, 20, 10, hex(0x2A241E, .72f));
    text(label, sx * k + 18, sy * k - 7, hex(0xFFF8E6), 12, 1);
    const double best = (*run_).best_height();
    if (best > (*run_).height() + 0.002) {
        double bx = 0;
        double by = 0;
        if (site_.project(phys::Vec3{ground.x, ground.y, best}, bx, by)) {
            for (int d = -3; d <= 3; d += 2) {
                frame_.stroke_line((bx * k + d * 4) * bs_, by * k * bs_, (bx * k + d * 4 + 4) * bs_, by * k * bs_, hex(0xF7BC1E, .9f), 1.5 * bs_);
            }
            text("best " + centimetres(best), bx * k + 18, by * k - 7, hex(0xF7BC1E), 11, 1);
        }
    }
}

void ZenView::draw_hud() {
    const bool small = compact();
    // the site's card, top left
    {
        const std::string company = (*run_).company();
        char line[160];
        std::snprintf(line, sizeof line, "Height %s   Best %s   Stack %d", centimetres((*run_).height()).c_str(), centimetres((*run_).best_height()).c_str(),
                      (*run_).stack_count());
        const double title = small ? 14 : 17;
        const double w = std::max(text_w(company, title, 2), text_w(line, small ? 11 : 12, 0)) + 28;
        const double h = small ? 46 : 56;
        rrect(10, 10, w, h, 10, hex(0xFBF6EA, .9f), kTimber, 1.5);
        text(company, 24, small ? 15 : 17, kInk, title, 2);
        text(line, 24, small ? 34 : 40, hex(0x5A4A36), small ? 11 : 12, 0);
    }
    // Mina's portrait and what she says, bottom left
    const double radius = small ? 30 : 42;
    const double cx = 12 + radius;
    const double cy = H_ - 12 - radius;
    draw_portrait(cx, cy, radius);
    if (say_.age < say_.life && !say_.text.empty()) {
        const double fade = std::min(1.0, std::min(say_.age * 6, (say_.life - say_.age) * 3));
        const double wrap = std::min(260.0, W_ - (cx + radius) - 140);
        const double size = small ? 11.5 : 13;
        const double tw = std::min(wrap, text_w(say_.text, size, 0));
        const double th = text_h(say_.text, size, 0, wrap);
        const double bx = cx + radius + 12;
        const double by = cy - th / 2 - 8;
        const float a = static_cast<float>(fade);
        rrect(bx, by, tw + 22, th + 16, 10, hex(0xFFFCF3, .95f * a), hex(0x8A6440, a), 1.2);
        frame_.begin();
        frame_.move((bx + 1) * bs_, (cy - 5) * bs_);
        frame_.line((bx - 8) * bs_, cy * bs_);
        frame_.line((bx + 1) * bs_, (cy + 5) * bs_);
        frame_.close();
        frame_.fill(hex(0xFFFCF3, .95f * a));
        text(say_.text, bx + 11, by + 8, Col{kInk.r, kInk.g, kInk.b, a}, size, 0, wrap);
    }
    // what the keys do now, and the slings' tension
    const Crane& crane = (*run_).crane;
    std::string hint;
    if (crane.mode == CraneMode::parked) {
        hint = "Click a rock to fetch it   ·   drag to look around   ·   scroll to zoom";
    } else if (crane.mode == CraneMode::steering) {
        hint = small ? "↑↓ reach · ←→ swing · W/S line · Q/E turn · R/F tip · Z/C roll · Space let go"
                     : "↑ ↓ reach  ·  ← → swing  ·  W / S raise, lower  ·  Q / E turn  ·  R / F tip  ·  Z / C roll  ·  Shift: fine  ·  Space: let go";
    } else if (crane.mode == CraneMode::fetching || crane.mode == CraneMode::attaching) {
        hint = "Off to fetch it   ·   Esc to call the crane back";
    } else if (crane.mode == CraneMode::lifting) {
        hint = "Lifting...";
    }
    const double hint_size = small ? 10.5 : 11.5;
    const double hint_x = cx + radius + 14;
    const double hint_wrap = W_ - hint_x - (crane.mode == CraneMode::steering ? 240 : 20);
    if (!hint.empty() && !(say_.age < say_.life)) {
        const double th = text_h(hint, hint_size, 0, hint_wrap);
        text(hint, hint_x, H_ - 14 - th, hex(0xFFFFFF, .92f), hint_size, 0, hint_wrap);
    }
    if (crane.attached && crane.mode == CraneMode::steering) {
        const double tension = std::clamp((*run_).world().hold_state().tension, 0.0, 1.2);
        const double bw = small ? 90 : 130;
        const double bx = W_ - 10 - bw;
        const double by = H_ - 10 - (small ? 24 : 28) - 26;
        rrect(bx, by, bw, 14, 7, hex(0x000000, .35f));
        rrect(bx + 2, by + 2, (bw - 4) * std::min(1.0, tension), 10, 5, tension < 0.1 ? hex(0x9AD48A) : hex(0xF7BC1E));
        text(tension < 0.1 ? "slings slack: resting" : "slings taut", bx, by - 15, hex(0xFFFFFF, .9f), 10.5, 0);
    }
}

// Mina: a round portrait in the corner. Hard hat, dark bob, big eyes; her
// face follows her mood.
void ZenView::draw_portrait(double cx, double cy, double radius) {
    frame_.save();
    frame_.scale(bs_, bs_);
    const double r = radius;
    // the frame and the sky behind her
    frame_.fill_circle(cx, cy, r + 3, kTimber);
    frame_.fill_circle(cx, cy, r, hex(0xBFDCEB));
    const double bob = mood_ == OperatorMood::cheering ? std::abs(std::sin(t_ * 9)) * r * 0.05 : 0.0;
    const double fx = cx;
    const double fy = cy + r * 0.12 - bob;
    // shoulders in an orange hi-vis vest
    frame_.begin();
    frame_.ellipse(fx, cy + r * 0.95, r * 0.7, r * 0.42);
    frame_.fill(hex(0xF08A24));
    frame_.fill_rect(fx - r * 0.5, cy + r * 0.72, r, r * 0.07, hex(0xF2F0E0));
    // hair behind
    frame_.begin();
    frame_.ellipse(fx, fy + r * 0.05, r * 0.5, r * 0.52);
    frame_.fill(hex(0x4A2E2A));
    // face
    frame_.begin();
    frame_.ellipse(fx, fy + r * 0.08, r * 0.42, r * 0.44);
    frame_.fill(hex(0xFBDCC6));
    // bangs
    frame_.begin();
    frame_.move(fx - r * 0.44, fy - r * 0.02);
    frame_.quad(fx - r * 0.36, fy - r * 0.36, fx, fy - r * 0.34);
    frame_.quad(fx + r * 0.36, fy - r * 0.36, fx + r * 0.44, fy - r * 0.02);
    frame_.line(fx + r * 0.3, fy - r * 0.12);
    frame_.line(fx + r * 0.16, fy - r * 0.02);
    frame_.line(fx + r * 0.05, fy - r * 0.14);
    frame_.line(fx - r * 0.1, fy - r * 0.02);
    frame_.line(fx - r * 0.25, fy - r * 0.14);
    frame_.line(fx - r * 0.34, fy - r * 0.02);
    frame_.close();
    frame_.fill(hex(0x4A2E2A));
    // the hard hat
    frame_.begin();
    frame_.move(fx - r * 0.48, fy - r * 0.18);
    frame_.cubic(fx - r * 0.46, fy - r * 0.62, fx + r * 0.46, fy - r * 0.62, fx + r * 0.48, fy - r * 0.18);
    frame_.close();
    frame_.fill(kYellow);
    frame_.fill_rect(fx - r * 0.58, fy - r * 0.2, r * 1.16, r * 0.07, hex(0xE0A410));
    frame_.fill_rect(fx - r * 0.05, fy - r * 0.58, r * 0.1, r * 0.4, hex(0xFFD24A));
    // eyes: big and dark with a glint; they blink, and narrow when she concentrates
    const double eye_y = fy + r * 0.1;
    const bool blinking = std::fmod(blink_, 4.3) < 0.12;
    const bool happy = mood_ == OperatorMood::cheering;
    for (int side = -1; side <= 1; side += 2) {
        const double ex = fx + side * r * 0.17;
        if (blinking || happy) {
            frame_.begin();
            frame_.move(ex - r * 0.08, eye_y + (happy ? r * 0.02 : 0));
            frame_.quad(ex, eye_y - (happy ? r * 0.08 : -r * 0.02), ex + r * 0.08, eye_y + (happy ? r * 0.02 : 0));
            frame_.stroke(hex(0x3A2420), r * 0.035);
        } else {
            const double squint = mood_ == OperatorMood::focused ? 0.6 : 1.0;
            frame_.begin();
            frame_.ellipse(ex, eye_y, r * 0.075, r * 0.1 * squint);
            frame_.fill(hex(0x3A2420));
            frame_.fill_circle(ex - r * 0.025, eye_y - r * 0.035 * squint, r * 0.025, hex(0xFFFFFF));
        }
    }
    // blush
    frame_.fill_ellipse(fx - r * 0.27, fy + r * 0.24, r * 0.07, r * 0.035, hex(0xF29A9A, .6f));
    frame_.fill_ellipse(fx + r * 0.27, fy + r * 0.24, r * 0.07, r * 0.035, hex(0xF29A9A, .6f));
    // mouth
    const double my = fy + r * 0.32;
    frame_.begin();
    if (mood_ == OperatorMood::cheering) {
        frame_.move(fx - r * 0.1, my - r * 0.02);
        frame_.quad(fx, my + r * 0.14, fx + r * 0.1, my - r * 0.02);
        frame_.close();
        frame_.fill(hex(0xB04040));
    } else if (mood_ == OperatorMood::oops) {
        frame_.ellipse(fx, my + r * 0.02, r * 0.05, r * 0.06);
        frame_.fill(hex(0x8A3030));
    } else if (mood_ == OperatorMood::worried) {
        frame_.move(fx - r * 0.08, my + r * 0.02);
        frame_.quad(fx - r * 0.04, my - r * 0.03, fx, my + r * 0.02);
        frame_.quad(fx + r * 0.04, my + r * 0.06, fx + r * 0.08, my + r * 0.01);
        frame_.stroke(hex(0x8A3030), r * 0.03);
    } else if (mood_ == OperatorMood::focused) {
        frame_.move(fx - r * 0.06, my + r * 0.01);
        frame_.line(fx + r * 0.06, my + r * 0.01);
        frame_.stroke(hex(0x8A3030), r * 0.03);
        frame_.fill_circle(fx + r * 0.07, my + r * 0.02, r * 0.025, hex(0xE07070));   // the tip of her tongue
    } else {
        frame_.move(fx - r * 0.08, my);
        frame_.quad(fx, my + r * 0.07, fx + r * 0.08, my);
        frame_.stroke(hex(0x8A3030), r * 0.03);
    }
    // a bead of sweat when she's worried; sparkles when she cheers
    if (mood_ == OperatorMood::worried || mood_ == OperatorMood::oops) {
        frame_.begin();
        frame_.move(fx + r * 0.42, fy - r * 0.12);
        frame_.quad(fx + r * 0.36, fy, fx + r * 0.42, fy + r * 0.02);
        frame_.quad(fx + r * 0.48, fy, fx + r * 0.42, fy - r * 0.12);
        frame_.fill(hex(0x9ED4F2));
    }
    if (mood_ == OperatorMood::cheering) {
        for (int k = 0; k < 3; k += 1) {
            const double a = t_ * 2 + k * 2.1;
            const double sxp = cx + std::cos(a) * r * 0.82;
            const double syp = cy + std::sin(a) * r * 0.82;
            frame_.stroke_line(sxp - r * 0.07, syp, sxp + r * 0.07, syp, hex(0xFFE070), r * 0.03);
            frame_.stroke_line(sxp, syp - r * 0.07, sxp, syp + r * 0.07, hex(0xFFE070), r * 0.03);
        }
    }
    frame_.restore();
    text("Mina", cx, cy + radius - 4, hex(0xFFFFFF), compact() ? 9.5 : 10.5, 1, 0, 1);
}

void ZenView::draw_panel() {
    if (panel_ == Panel::none) {
        return;
    }
    frame_.fill_rect(0, 0, phys_w_, phys_h_, hex(0x000000, .45f));
    if (panel_ == Panel::new_site) {
        const double w = std::min(W_ - 24, 440.0);
        const double h = 230;
        const double x = (W_ - w) / 2;
        const double y = std::max(10.0, (H_ - h) / 2);
        rrect(x, y, w, h, 14, kPaper, kTimber, 2);
        text("A new worksite", x + w / 2, y + 16, kInk, 20, 2, 0, 1);
        text("Your company's name goes on the sign:", x + 22, y + 56, hex(0x5A4A36), 12.5, 0);
        rrect(x + 20, y + 78, w - 40, 38, 8, hex(0xFFFFFF), kTimber, 1.2);
        const std::string shown = name_entry_ + (std::fmod(t_, 1.0) < 0.55 ? "|" : " ");
        text(shown, x + 32, y + 87, kInk, 16, 1);
        char seed[96];
        std::snprintf(seed, sizeof seed, "Rocks no. %u: fifty stones in the bowl, the same for this number every time.", seed_entry_);
        text(seed, x + 22, y + 128, hex(0x6A5A44), 11, 0, w - 44);
        want_render_ = true;
        return;
    }
    if (panel_ == Panel::sites) {
        const double w = std::min(W_ - 24, 520.0);
        const double rows = std::max(1.0, std::min(static_cast<double>(sites_.size()), std::floor((H_ - 140) / 34)));
        const double h = 74 + rows * 34 + 44;
        const double x = (W_ - w) / 2;
        const double y = std::max(10.0, (H_ - h) / 2);
        rrect(x, y, w, h, 14, kPaper, kTimber, 2);
        text("Your worksites", x + w / 2, y + 14, kInk, 19, 2, 0, 1);
        text("Pick one to work on.  [ and ] flip between them.", x + w / 2, y + 40, hex(0x6A5A44), 11.5, 0, 0, 1);
        return;
    }
    // help
    const double w = std::min(W_ - 24, 560.0);
    const double h = std::min(H_ - 60, 420.0);
    const double x = (W_ - w) / 2;
    const double y = std::max(10.0, (H_ - h) / 2);
    rrect(x, y, w, h, 14, kPaper, kTimber, 2);
    text("Zen Construction", x + w / 2, y + 14, kInk, 20, 2, 0, 1);
    const char* lines[] = {
        "Stack the rocks as tall as they'll stand. Your height is the top of the stack: the first rock you set down, and every rock resting on the ones below it.",
        "Click a rock in the bowl and Mina's crane fetches it. The up and down arrows telescope the boom out and in, left and right swing it round; W and S pay the line out and in. Turn the rock with Q and E, tip it toward or away from the crane with R and F, roll it with Z and C. Hold Shift for fine work.",
        "Lower it slowly onto the stack: as it settles, the slings go slack. When they're slack and nothing moves, press Space to let go. B carries it back to the bowl.",
        "To take the stack apart, click its top rock. Rocks that fall are tidied back into the bowl. Drag to look around; scroll to zoom.",
        "Every site is saved. Sites lets you go back to one; Start over (hold it) puts every rock back in the bowl."};
    double ly = y + 50;
    const double size = h < 360 ? 11 : 12.5;
    for (int i = 0; i < 5; i += 1) {
        text(lines[i], x + 24, ly, kInk, size, 0, w - 48);
        ly += text_h(lines[i], size, 0, w - 48) + 8;
    }
}

void ZenView::draw_buttons() {
    for (size_t i = 0; i < buttons_.size(); i += 1) {
        const Button& b = buttons_[i];
        const bool over = hover_button_ == b.id && b.enabled;
        const bool down = pressed_ == b.id;
        Col face = !b.enabled ? hex(0xCFC8B8, .85f) : over ? hex(0xFFFBF0) : hex(0xF4EDDC, .95f);
        Col ink = b.enabled ? kInk : hex(0x8A8070);
        if (b.style == 1 && b.enabled) {
            face = over ? hex(0xFFCC3A) : kYellow;
        }
        rrect(b.x + 1, b.y + 2, b.w, b.h, 7, kShade);
        rrect(b.x, b.y + (down ? 1 : 0), b.w, b.h, 7, face, kTimber, 1.1);
        if (b.style == 2 && down && hold_reset_ > 0) {
            // the hold-to-confirm fill
            rrect(b.x + 2, b.y + 2, (b.w - 4) * std::min(1.0, hold_reset_ / 1.6), b.h - 4, 6, hex(0xE8826A));
        }
        const double size = compact() ? 11.5 : 12.5;
        std::string label = b.label;
        if (b.style == 2 && down) {
            label = "Keep holding...";
        }
        text(label, b.x + b.w / 2, b.y + b.h / 2 - size * 0.62 + (down ? 1 : 0), ink, size, b.style == 1 ? 1 : 0, 0, 1);
    }
}

}  // namespace zc
