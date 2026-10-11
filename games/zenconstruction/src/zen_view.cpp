#include "help_route.hpp"
#include "zen_view.hpp"
#include "runtime_paths.hpp"

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

std::string save_path(bool dev) {
    const char* name = dev ? "zen_construction-dev-v1.txt" : "zen_construction-v1.txt";
    return (games::state_directory() / name).string();
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
    set_accessible_name("Rock Stack. Stack rocks from the bowl with a little crane.");
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
    hud_panel_ = -1;  // a panel already open takes the keyboard now there is a window
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
    audio_stop();
}

void ZenView::activate() {
    if (attached_window() != nullptr) {
        static_cast<void>((*attached_window()).request_focus(shared_from_this()));
    }
}

void ZenView::set_cabinet(bool foreground, bool music, bool sound, bool reduced) {
    cab_reduced_ = reduced;
    if (timer_) {
        if (foreground) { last_ = std::chrono::steady_clock::now(); (*timer_).start(); }
        else { (*timer_).stop(); audio_stop(); on_focus_changed(false); }
    }
    cab_front_ = foreground;
    cab_music_ = music;
    cab_sound_ = sound;
}

void ZenView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    const double scale = attached_window() ? (*attached_window()).scale() : 1.0;
    // The controls ask for layout as they change; the scene is resized only with the window.
    if (sized_ && bounds.width == W_ && bounds.height == H_ && scale == bs_) {
        layout_hud();
        return;
    }
    sized_ = true;
    W_ = bounds.width;
    H_ = bounds.height;
    bs_ = scale;
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
        d.opaque = true;  // every pixel is drawn opaque: the window copies, never blends
        static_cast<void>(surface_->reconfigure(d));
    }
    hud_key_.clear();
    update_hud();
    layout_hud();
    want_render_ = true;
}

void ZenView::on_paint(gf::Painter& painter, gf::Rect) {
    const gf::Rect b = client_rectangle();
    if (surface_) {
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
}

void ZenView::start_over() {
    if (!run_ || pending_.valid()) {
        return;
    }
    pending_ = std::async(std::launch::async, make_site, (*run_).seed(), (*run_).company(), (*run_).best_height());
    pending_new_ = false;
    run_.reset();
    play("zc_reset", .8f);
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
        want_render_ = want_render_ || !(*run_).quiet() || (*run_).busy() ||
                       input.right != 0 || input.forward != 0 || input.up != 0 ||
                       input.yaw != 0 || input.pitch != 0 || input.roll != 0;
    }
    // the near meshes of everything out of the bowl (built once, on first need)
    for (size_t i = 0; i < (*run_).rocks.size(); i += 1) {
        RockState& state = (*run_).rocks[i];
        if (state.place != Place::bowl && state.rock.near_mesh.triangles.empty()) {
            ensure_near_mesh(state.rock);
        }
    }
    // The duck's mood follows the work unless something just happened
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
    // Hosted, PlaySuite's Sound and Music masters are the only switches.
    audio_sfx(name, gain, rate, (options_.hosted || sound_) && cab_sound_ && cab_front_ && visible(), pan);
}

// The beds and the odd sounds: the brook always; the motor whining up with
// the crane's speed (the hook's travel and the slings reeling), humming low
// while it holds a rock, spinning down when it stops; sounds due now; a
// bird in the trees every half minute or so.
void ZenView::step_sound(double dt) {
    const bool sound_on = (options_.hosted || sound_) && cab_sound_ && cab_front_ && visible();
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
    if (!cab_front_ || !effectively_visible()) return;
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
    }
    say_cooldown_ = std::max(0.0, say_cooldown_ - dt);
    if (hud_.reset && (*hud_.reset).holding() && !hold_spent_) {
        hold_reset_ += dt;
        if (hold_reset_ >= 1.6) {
            hold_reset_ = 0;
            hold_spent_ = true;
            start_over();
        }
    } else {
        hold_reset_ = 0;
        if (!hud_.reset || !(*hud_.reset).pressed_visual()) {
            hold_spent_ = false;
        }
    }
    update_hud();
    audio_music(front ? "zc_music" : "", (options_.hosted || music_) && cab_music_ && cab_front_);
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
    if (hidden || !shown || frame_.px.empty() || since_render_ < interval) {
        return;
    }
    since_render_ = 0;
    want_render_ = false;
    compose();
    publish();
}

void ZenView::publish() {
    if (!surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        d.opaque = true;  // every pixel is drawn opaque: the window copies, never blends
        surface_ = gf::LiveSurface::create(d);
        if (surface_ && attached_window() != nullptr) {
            direct_ = (*attached_window()).queue_live_surface_presentation(shared_from_this(), surface_);
            invalidate(gf::Dirty::paint);  // the paint now shows the live surface
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
    if (id == "help" && games::route_help(*this, id))
        return;
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
        // Enter in the name field and the button may both ask
        if (panel_ == Panel::new_site) {
            start_new_site(name_entry_, seed_entry_);
        }
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
    update_hud();
    want_render_ = true;
}

void ZenView::clicked(int index) {
    const std::string& id = button_ids_[static_cast<size_t>(index)];
    if (id == "reset") {
        return;  // it acts when held, not when clicked
    }
    if (id == "cancel") {
        action("close");
    } else if (id.rfind("row", 0) == 0) {
        action("site" + std::to_string(hud_.first_row + std::atoi(id.c_str() + 3)));
    } else {
        action(id);
    }
    // the keys steer the crane again
    if (panel_ == Panel::none) {
        activate();
    }
}

void ZenView::name_changed(const std::string& text) {
    if (text.size() > name_entry_.size()) {
        play("ui_name_key_0" + std::to_string(1 + (static_cast<int>(text.size()) % 3)), .55f);
    } else if (text.size() < name_entry_.size()) {
        play("ui_name_backspace", .6f);
    }
    name_entry_ = text;
}

void ZenView::name_committed(const std::string&) { action("start"); }

void ZenView::name_cancelled() {
    if (run_) {
        action("close");
    }
}

void ZenView::on_pointer(gf::PointerEvent& e) {
    const gf::Point local = point_from_window(e.position);
    mx_ = local.x;
    my_ = local.y;
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
        set_cursor(hover_ok_ ? gf::CursorKind::hand : gf::CursorKind::arrow);
        return;
    }
    if (e.action == gf::PointerAction::down) {
        if (panel_ != Panel::new_site) {
            activate();
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
        if (dragging_) {
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
        if (k == kKeyEnter) {
            action("start");
        } else if (k == kKeyEscape && run_) {
            action("close");
        }
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
            if (hud_.name) {
                (*hud_.name).set_text(name_entry_);
            }
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

// ---------------------------------------------------------------- the controls

namespace {
gf::Color ink(Col c, double alpha = 1) {
    return gf::Color::rgba(static_cast<std::uint8_t>(std::lround(c.r * 255)), static_cast<std::uint8_t>(std::lround(c.g * 255)),
                           static_cast<std::uint8_t>(std::lround(c.b * 255)), static_cast<std::uint8_t>(std::lround(c.a * alpha * 255)));
}
gf::Color paper(double alpha = 1) { return gf::Color::rgba(0xFB, 0xF6, 0xEA, static_cast<std::uint8_t>(std::lround(255 * alpha))); }

const char* const kHelp[] = {
    "Stack the rocks as tall as they'll stand. Your height is the top of the stack: the first rock you set down, and every rock resting on the ones below it.",
    "Click a rock in the bowl and the crane fetches it. The up and down arrows telescope the boom out and in, left and right swing it round; W and S pay the line out and in. Turn the rock with Q and E, tip it toward or away from the crane with R and F, roll it with Z and C. Hold Shift for fine work.",
    "Lower it slowly onto the stack: as it settles, the slings go slack. When they're slack and nothing moves, press Space to let go. B carries it back to the bowl.",
    "To take the stack apart, click its top rock. Rocks that fall are tidied back into the bowl. Drag to look around; scroll to zoom.",
    "Every site is saved. Sites lets you go back to one; Start over (hold it) puts every rock back in the bowl."};

// A font only when it differs, so laying out never asks for another layout.
void font(gf::Label& label, double size, int weight = 400, bool italic = false) {
    const gf::FontSpec f{gf::FontRole::content, size, static_cast<std::uint16_t>(weight), italic};
    if (!label.has_font_override() || !(label.font() == f)) {
        label.set_font(f);
    }
}
void set_words(gf::Label& label, const std::string& text) {
    if (label.text() != text) {
        label.set_text(text);
    }
}
void show(gf::Control& control, bool shown) {
    if (control.visible() != shown) {
        control.set_visible(shown);
    }
}
void enable(gf::Control& control, bool enabled) {
    if (control.enabled() != enabled) {
        control.set_enabled(enabled);
    }
}
gf::Size size_of(gf::Label& label, double wrap) { return label.measure({wrap > 0 ? wrap : 1e6, 1e6}); }
}  // namespace

std::shared_ptr<games::SuiteButton> ZenView::make_button(const std::string& id, const std::string& label, games::GlossTone tone) {
    std::shared_ptr<games::SuiteButton> b = gf::make_control<games::SuiteButton>(gf::StableId("zc." + id), label, tone);
    (*b).set_radius(7);
    (*b).set_accessible_name(label);
    (*b).set_paint_plane(gf::PaintPlane::overlay);
    (*b).set_visible(false);
    add_child(b);
    gf::on((*b).clicked(), *this, &ZenView::clicked, static_cast<int>(button_ids_.size()));
    button_ids_.push_back(id);
    return b;
}

std::shared_ptr<gf::Label> ZenView::make_label(const std::string& id, double size, int weight, bool italic, gf::Color colour, bool wrap) {
    std::shared_ptr<gf::Label> l = gf::make_control<Words>(gf::StableId("zc." + id));
    font(*l, size, weight, italic);
    (*l).set_foreground(colour);
    (*l).set_use_mnemonic(false);
    if (wrap) {
        (*l).set_text_wrapping(gf::TextWrapping::word);
    }
    (*l).set_paint_plane(gf::PaintPlane::overlay);
    (*l).set_visible(false);
    add_child(l);
    return l;
}

void ZenView::initialize_control_tree() {
    const gf::Color brown = ink(hex(0x5A4A36));
    const gf::Color muted = ink(hex(0x6A5A44));
    // the site's card, top left
    hud_.card = gf::make_control<Paper>(gf::StableId("zc.card"), 10, paper(.9), 1.5);
    (*hud_.card).set_visible(false);
    add_child(hud_.card);
    hud_.company = make_label("company", 17, 700, true, ink(kInk));
    hud_.line = make_label("line", 12, 400, false, brown);
    // a line from the worksite, bottom left
    hud_.say = gf::make_control<Paper>(gf::StableId("zc.say"), 10, gf::Color::rgba(0xFF, 0xFC, 0xF3, 242), 1.2, true);
    (*hud_.say).set_visible(false);
    add_child(hud_.say);
    hud_.say_text = make_label("say.text", 13, 400, false, ink(kInk), true);
    // what the keys do now
    hud_.hint = gf::make_control<Words>(gf::StableId("zc.hint"));
    (*hud_.hint).set_text_wrapping(gf::TextWrapping::word);
    (*hud_.hint).set_use_mnemonic(false);
    (*hud_.hint).set_foreground(gf::Color::rgba(255, 255, 255, 235));
    (*hud_.hint).set_paint_plane(gf::PaintPlane::overlay);
    (*hud_.hint).set_visible(false);
    add_child(hud_.hint);
    hud_.slings = gf::make_control<SlingMeter>(gf::StableId("zc.slings"));
    (*hud_.slings).set_visible(false);
    add_child(hud_.slings);
    // the top-right row; hosted, the capsule has Music, Help, New site and Sites
    hud_.music = make_button("music", music_ ? "Music" : "Quiet", games::GlossTone::chrome);
    hud_.help = make_button("help", "Help", games::GlossTone::chrome);
    hud_.reset = gf::make_control<HoldButton>(gf::StableId("zc.reset"), "Start over");
    (*hud_.reset).set_radius(7);
    (*hud_.reset).set_accessible_name("Start over (hold)");
    (*hud_.reset).set_paint_plane(gf::PaintPlane::overlay);
    (*hud_.reset).set_visible(false);
    add_child(hud_.reset);
    button_ids_.push_back("reset");
    hud_.new_site = make_button("new", "New site", games::GlossTone::chrome);
    hud_.sites = make_button("sites", "Sites", games::GlossTone::chrome);
    // while the crane holds a rock
    hud_.back = make_button("throw", "Back to bowl", games::GlossTone::chrome);
    hud_.let_go = make_button("release", "Let go", games::GlossTone::gold);
    // while fresh rocks tip into the bowl
    hud_.loading = gf::make_control<Paper>(gf::StableId("zc.loading"), 12, paper(), 2);
    (*hud_.loading).set_visible(false);
    add_child(hud_.loading);
    hud_.loading_title = make_label("loading.title", 18, 700, true, ink(kInk));
    hud_.loading_dots = make_label("loading.dots", 12, 400, false, muted);
    (*hud_.loading_title).set_alignment(gf::HorizontalAlignment::center);
    (*hud_.loading_dots).set_alignment(gf::HorizontalAlignment::center);
    (*hud_.loading_title).set_text("Filling the bowl...");
    // the panels, over the dimmed worksite
    hud_.sheet = gf::make_control<Paper>(gf::StableId("zc.sheet"), 14, paper(), 2);
    (*hud_.sheet).set_visible(false);
    add_child(hud_.sheet);
    hud_.sheet_title = make_label("sheet.title", 20, 700, true, ink(kInk));
    (*hud_.sheet_title).set_alignment(gf::HorizontalAlignment::center);
    hud_.sheet_note = make_label("sheet.note", 12.5, 400, false, brown, true);
    hud_.seed_note = make_label("sheet.seed", 11, 400, false, muted, true);
    for (const char* line : kHelp) {
        hud_.help_lines.push_back(make_label("help." + std::to_string(hud_.help_lines.size()), 12.5, 400, false, ink(kInk), true));
        (*hud_.help_lines.back()).set_text(line);
    }
    hud_.name = gf::make_control<gf::TextBox>(gf::StableId("zc.name"), name_entry_);
    (*hud_.name).set_maximum_length(28);
    (*hud_.name).set_font({gf::FontRole::content, 16, 700, false});
    (*hud_.name).set_accessible_name("Your company's name, for the sign");
    (*hud_.name).set_paint_plane(gf::PaintPlane::overlay);
    (*hud_.name).set_visible(false);
    add_child(hud_.name);
    gf::on((*hud_.name).text_changed(), *this, &ZenView::name_changed);
    gf::on((*hud_.name).committed(), *this, &ZenView::name_committed);
    gf::on((*hud_.name).cancelled(), *this, &ZenView::name_cancelled);
    hud_.reroll = make_button("reroll", "Other rocks", games::GlossTone::chrome);
    hud_.cancel = make_button("cancel", "Cancel", games::GlossTone::chrome);
    hud_.start = make_button("start", "Start the site", games::GlossTone::gold);
    hud_.close = make_button("close", "Close", games::GlossTone::chrome);
    update_hud();
}

// The controls follow the game: words change, controls appear and go. Layout is
// asked for only when something moves or resizes.
void ZenView::update_hud() {
    if (!hud_.card) {
        return;
    }
    const bool small = compact();
    const bool loaded = run_ != nullptr;
    const bool open = panel_ != Panel::none;
    const Crane* crane = loaded ? &(*run_).crane : nullptr;
    const bool steering = crane != nullptr && (*crane).attached && (*crane).mode == CraneMode::steering;
    const bool speaking = loaded && !open && say_.age < say_.life && !say_.text.empty();
    std::string line;
    if (loaded) {
        char buffer[160];
        std::snprintf(buffer, sizeof buffer, "Height %s   Best %s   Stack %d", centimetres((*run_).height()).c_str(),
                      centimetres((*run_).best_height()).c_str(), (*run_).stack_count());
        line = buffer;
    }
    std::string hint;
    if (crane != nullptr && !open && !speaking) {
        if ((*crane).mode == CraneMode::parked) {
            hint = "Click a rock to fetch it   ·   drag to look around   ·   scroll to zoom";
        } else if (steering || (*crane).mode == CraneMode::steering) {
            hint = small ? "↑↓ reach · ←→ swing · W/S line · Q/E turn · R/F tip · Z/C roll · Space let go"
                         : "↑ ↓ reach  ·  ← → swing  ·  W / S raise, lower  ·  Q / E turn  ·  R / F tip  ·  Z / C roll  ·  Shift: fine  ·  Space: let go";
        } else if ((*crane).mode == CraneMode::fetching || (*crane).mode == CraneMode::attaching) {
            hint = "Off to fetch it   ·   Esc to call the crane back";
        } else if ((*crane).mode == CraneMode::lifting) {
            hint = "Lifting...";
        }
    }
    const bool loading = !loaded && pending_.valid();
    const bool can_reset = loaded && !pending_.valid() && !(*run_).busy();
    const int rows = panel_ == Panel::sites
                         ? static_cast<int>(std::max(1.0, std::min(static_cast<double>(sites_.size()), std::floor((H_ - 140) / 34))))
                         : 0;
    // A panel opening or closing moves the keyboard.
    if (attached_window() != nullptr && static_cast<int>(panel_) != hud_panel_) {
        const bool first = hud_panel_ < 0;
        hud_panel_ = static_cast<int>(panel_);
        if (panel_ == Panel::new_site) {
            (*hud_.name).set_text(name_entry_);
            (*hud_.name).select_all();
            static_cast<void>((*attached_window()).request_focus(hud_.name));
        } else if (!first && (*hud_.name).visible()) {
            activate();
        }
    }
    std::string key;
    key.reserve(256);
    key += std::to_string(static_cast<int>(panel_)) + (small ? "s" : "l") + std::to_string(static_cast<int>(W_)) + "x" + std::to_string(static_cast<int>(H_));
    key += loaded ? "|" + (*run_).company() + "|" + line : std::string("|-");
    key += "|" + hint + "|" + (speaking ? say_.text : std::string()) + (steering ? "|S" : "|-") + (loading ? "L" : "-") + (can_reset ? "R" : "-");
    key += music_ ? "M" : "Q";
    if (panel_ == Panel::new_site) {
        key += std::to_string(seed_entry_) + (run_ ? "c" : "-");
    } else if (panel_ == Panel::sites) {
        key += std::to_string(sites_.size()) + "@" + std::to_string(current_);
    }
    // what changes without moving anything
    const double fade = speaking ? std::min(1.0, std::min(say_.age * 6, (say_.life - say_.age) * 3)) : 0;
    if (std::abs(fade - say_fade_) > 0.004 || (fade == 0) != (say_fade_ == 0)) {
        say_fade_ = fade;
        (*hud_.say).set_fade(fade);
        (*hud_.say_text).set_foreground(ink(kInk, fade));
    }
    if (steering && !open) {
        (*hud_.slings).set_tension((*run_).world().hold_state().tension);
    }
    const int dots = loading ? static_cast<int>(t_ * 3) % 4 : -1;
    if (dots != loading_dots_) {
        loading_dots_ = dots;
        const std::string d(static_cast<size_t>(std::max(0, dots)), '.');
        set_words(*hud_.loading_dots, d + " Tipping in fresh rocks " + d);
    }
    (*hud_.reset).set_progress(hold_reset_ / 1.6);
    if (key == hud_key_) {
        return;
    }
    hud_key_ = key;
    // the worksite's own controls, hidden while a panel is open
    const bool working = loaded && !open;
    show(*hud_.card, working);
    show(*hud_.company, working);
    show(*hud_.line, working);
    if (loaded) {
        set_words(*hud_.company, (*run_).company());
        set_words(*hud_.line, line);
    }
    font(*hud_.company, small ? 14 : 17, 700, true);
    font(*hud_.line, small ? 11 : 12);
    show(*hud_.say, speaking);
    show(*hud_.say_text, speaking);
    if (speaking) {
        set_words(*hud_.say_text, say_.text);
        font(*hud_.say_text, small ? 11.5 : 13);
    }
    show(*hud_.hint, working && !hint.empty());
    set_words(*hud_.hint, hint);
    {
        const gf::FontSpec f{gf::FontRole::content, small ? 10.5 : 11.5, 400, false};
        if (!(*hud_.hint).has_font_override() || !((*hud_.hint).font() == f)) {
            (*hud_.hint).set_font(f);
        }
    }
    show(*hud_.slings, working && steering);
    show(*hud_.music, working && !options_.hosted);
    show(*hud_.help, working && !options_.hosted);
    show(*hud_.new_site, working && !options_.hosted);
    show(*hud_.sites, working && !options_.hosted);
    show(*hud_.reset, working);
    enable(*hud_.reset, can_reset);
    enable(*hud_.sites, !sites_.empty());
    (*hud_.music).set_text(music_ ? "Music" : "Quiet");
    show(*hud_.let_go, working && steering);
    show(*hud_.back, working && steering);
    show(*hud_.loading, loading);
    show(*hud_.loading_title, loading);
    show(*hud_.loading_dots, loading);
    // the panels
    show(*hud_.sheet, open);
    show(*hud_.sheet_title, open);
    show(*hud_.sheet_note, panel_ == Panel::new_site || panel_ == Panel::sites);
    show(*hud_.seed_note, panel_ == Panel::new_site);
    show(*hud_.name, panel_ == Panel::new_site);
    show(*hud_.start, panel_ == Panel::new_site);
    show(*hud_.reroll, panel_ == Panel::new_site);
    show(*hud_.cancel, panel_ == Panel::new_site && run_ != nullptr);
    show(*hud_.close, panel_ == Panel::sites || panel_ == Panel::help);
    for (const std::shared_ptr<gf::Label>& l : hud_.help_lines) {
        show(*l, panel_ == Panel::help);
        font(*l, std::min(H_ - 60, 420.0) < 360 ? 11 : 12.5);
    }
    if (panel_ == Panel::new_site) {
        set_words(*hud_.sheet_title, "A new worksite");
        set_words(*hud_.sheet_note, "Your company's name goes on the sign:");
        font(*hud_.sheet_note, 12.5);
        char seed[96];
        std::snprintf(seed, sizeof seed, "Rocks no. %u: fifty stones in the bowl, the same for this number every time.", seed_entry_);
        set_words(*hud_.seed_note, seed);
    } else if (panel_ == Panel::sites) {
        set_words(*hud_.sheet_title, "Your worksites");
        set_words(*hud_.sheet_note, "Pick one to work on.  [ and ] flip between them.");
        font(*hud_.sheet_note, 11.5);
    } else if (panel_ == Panel::help) {
        set_words(*hud_.sheet_title, "Rock Stack");
    }
    font(*hud_.sheet_title, panel_ == Panel::sites ? 19 : 20, 700, true);
    {
        const gf::HorizontalAlignment a = panel_ == Panel::sites ? gf::HorizontalAlignment::center : gf::HorizontalAlignment::near;
        if ((*hud_.sheet_note).alignment() != a) {
            (*hud_.sheet_note).set_alignment(a);
        }
    }
    // the sites, as many rows as fit, around the current one
    while (static_cast<int>(hud_.site_rows.size()) < rows) {
        hud_.site_rows.push_back(make_button("row" + std::to_string(hud_.site_rows.size()), "", games::GlossTone::chrome));
    }
    hud_.first_row = 0;
    if (static_cast<int>(sites_.size()) > rows) {
        hud_.first_row = std::clamp(current_ - rows / 2, 0, static_cast<int>(sites_.size()) - rows);
    }
    for (int i = 0; i < static_cast<int>(hud_.site_rows.size()); i += 1) {
        games::SuiteButton& row = *hud_.site_rows[static_cast<size_t>(i)];
        const int index = hud_.first_row + i;
        const bool shown = i < rows && index < static_cast<int>(sites_.size());
        show(row, shown);
        if (shown) {
            const SavedSite& saved = sites_[static_cast<size_t>(index)];
            char label[160];
            std::snprintf(label, sizeof label, "%s   (rocks no. %u)   best %.1f cm", saved.company.c_str(), saved.seed, saved.best * 100);
            if (row.text() != label) {
                row.set_text(label);
                row.set_accessible_name(label);
            }
            row.set_tone(index == current_ ? games::GlossTone::gold : games::GlossTone::chrome);
        }
    }
    invalidate(gf::Dirty::layout);
}

void ZenView::layout_hud() {
    if (!hud_.card) {
        return;
    }
    const bool small = compact();
    const double top = top_inset();
    const double bh = small ? 24 : 28;
    // the site's card, top left (hosted, below the capsule)
    if ((*hud_.card).visible()) {
        const gf::Size c = size_of(*hud_.company, 0);
        const gf::Size l = size_of(*hud_.line, 0);
        set_child_layout(hud_.card, {10, top, std::max(c.width, l.width) + 28, small ? 46.0 : 56.0});
        set_child_layout(hud_.company, {24, top + (small ? 4 : 6), c.width + 2, c.height});
        set_child_layout(hud_.line, {24, top + (small ? 25 : 31), l.width + 2, l.height});
    }
    // the top-right row, sized to the labels, from the right
    {
        double x = W_ - 10;
        games::SuiteButton* row[5] = {hud_.music.get(), hud_.help.get(), hud_.reset.get(), hud_.new_site.get(), hud_.sites.get()};
        for (games::SuiteButton* b : row) {
            if (!(*b).visible()) {
                continue;
            }
            // Start over is as wide as its words while held, so it doesn't grow under the pointer
            const double w = b == hud_.reset.get() ? 15 * 7.3 + 24 : (*b).preferred_width();
            x -= w;
            set_child_layout((*b).shared_from_this(), {x, top, w, bh + 3});
            x -= 6;
        }
    }
    // a line from the worksite, bottom left
    if ((*hud_.say).visible()) {
        const double wrap = std::min(260.0, W_ - 170);
        const gf::Size m = size_of(*hud_.say_text, wrap);
        const double tw = std::min(wrap, m.width);
        const double cy = H_ - (small ? 42 : 54);
        const double by = cy - m.height / 2 - 8;
        set_child_layout(hud_.say, {16, by, tw + 22, m.height + 16});
        set_child_layout(hud_.say_text, {27, by + 8, tw + 1, m.height});
    }
    const bool steering = (*hud_.let_go).visible();
    if ((*hud_.hint).visible()) {
        const double wrap = W_ - 16 - (steering ? 240 : 20);
        const gf::Size m = size_of(*hud_.hint, wrap);
        set_child_layout(hud_.hint, {16, H_ - 14 - m.height, std::min(wrap, m.width) + 1, m.height});
    }
    if (steering) {
        const double bw = small ? 86 : 104;
        set_child_layout(hud_.let_go, {W_ - 10 - bw, H_ - 10 - bh, bw, bh + 3});
        set_child_layout(hud_.back, {W_ - 10 - bw - 8 - (bw + 14), H_ - 10 - bh, bw + 14, bh + 3});
        const double mw = small ? 90 : 130;
        set_child_layout(hud_.slings, {W_ - 10 - mw, H_ - 10 - bh - 42, mw, 30});
    }
    if ((*hud_.loading).visible()) {
        const double w = std::min(W_ - 40, 360.0);
        const double x = (W_ - w) / 2;
        const double y = (H_ - 96) / 2;
        set_child_layout(hud_.loading, {x, y, w, 96});
        set_child_layout(hud_.loading_title, {x, y + 18, w, 28});
        set_child_layout(hud_.loading_dots, {x, y + 54, w, 20});
    }
    // the panels
    if (panel_ == Panel::new_site) {
        const double w = std::min(W_ - 24, 440.0);
        const double h = 230;
        const double x = (W_ - w) / 2;
        const double y = std::max(10.0, (H_ - h) / 2);
        set_child_layout(hud_.sheet, {x, y, w, h});
        set_child_layout(hud_.sheet_title, {x, y + 12, w, 30});
        set_child_layout(hud_.sheet_note, {x + 22, y + 52, w - 44, 22});
        set_child_layout(hud_.name, {x + 20, y + 78, w - 40, 38});
        set_child_layout(hud_.seed_note, {x + 22, y + 124, w - 44, size_of(*hud_.seed_note, w - 44).height});
        set_child_layout(hud_.reroll, {x + 20, y + h - 46, 120, 35});
        set_child_layout(hud_.cancel, {x + w - 270, y + h - 46, 88, 35});
        set_child_layout(hud_.start, {x + w - 170, y + h - 46, 150, 35});
    } else if (panel_ == Panel::sites) {
        const double w = std::min(W_ - 24, 520.0);
        const double rows = std::max(1.0, std::min(static_cast<double>(sites_.size()), std::floor((H_ - 140) / 34)));
        const double h = 74 + rows * 34 + 44;
        const double x = (W_ - w) / 2;
        const double y = std::max(10.0, (H_ - h) / 2);
        set_child_layout(hud_.sheet, {x, y, w, h});
        set_child_layout(hud_.sheet_title, {x, y + 10, w, 30});
        set_child_layout(hud_.sheet_note, {x + 16, y + 38, w - 32, 20});
        for (size_t i = 0; i < hud_.site_rows.size(); i += 1) {
            set_child_layout(hud_.site_rows[i], {x + 16, y + 60 + static_cast<double>(i) * 34, w - 32, 33});
        }
        set_child_layout(hud_.close, {x + w / 2 - 50, y + h - 40, 100, 33});
    } else if (panel_ == Panel::help) {
        const double w = std::min(W_ - 24, 560.0);
        const double h = std::min(H_ - 60, 420.0);
        const double x = (W_ - w) / 2;
        const double y = std::max(10.0, (H_ - h) / 2);
        set_child_layout(hud_.sheet, {x, y, w, h});
        set_child_layout(hud_.sheet_title, {x, y + 10, w, 30});
        double ly = y + 50;
        for (const std::shared_ptr<gf::Label>& l : hud_.help_lines) {
            const double lh = size_of(*l, w - 48).height;
            set_child_layout(l, {x + 24, ly, w - 48, lh});
            ly += lh + 8;
        }
        set_child_layout(hud_.close, {W_ / 2 - 50, y + h - 42, 100, 33});
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
        state.time = cab_reduced_ ? 0 : t_;
        state.hover_rock = (*run_).crane.mode == CraneMode::parked ? hover_rock_ : -1;
        state.hover_ok = hover_ok_;
        state.mood = mood_;
        site_.render(state, (*run_).quiet() && !(*run_).busy());
        present_scene();
        draw_height_marks();
    } else {
        frame_.clear(hex(0xC8D7DF));
    }
    // a panel's dimming belongs to the scene; the panel itself is a control above it
    if (panel_ != Panel::none) {
        frame_.fill_rect(0, 0, phys_w_, phys_h_, hex(0x000000, .45f));
    }
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

}  // namespace zc

namespace zc {
std::vector<games::GameCommand> ZenView::commands() const {
    return {{"new", "New site", true, panel_ == Panel::new_site},
            {"sites", "Sites", !sites_.empty(), panel_ == Panel::sites},
            {"help", "Help", true, panel_ == Panel::help}};
}
void ZenView::run_command(std::string_view id) { action(std::string(id)); }
void ZenView::on_focus_changed(bool focused) {
    if (!focused) {
        std::fill(std::begin(keys_), std::end(keys_), false);
        dragging_ = false;
        hold_reset_ = 0;
    }
}
}
