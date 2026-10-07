#include "mowing_view.hpp"

#include "ambience_voice.hpp"
#include "banjo_voice.hpp"
#include "chipper_voice.hpp"
#include "granny_voice.hpp"
#include "mower_voice.hpp"

#include <algorithm>
#include <random>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <span>
#include <sstream>

namespace mm {

#ifdef GUI_FORMS_AUDIO_GENERATOR
// The mower's voice, synthesized as it plays. The audio device pulls render(); the
// scene only posts what the key and the grass are doing. The engine is always asked
// for mowing speed, and its blades go in once it has had a moment to get there.
class LiveMower final : public gui_forms::AudioGenerator {
  public:
    void set(bool ignition, double load, double wreck) {
        ignition_.store(ignition, std::memory_order_relaxed);
        load_.store(static_cast<float>(load), std::memory_order_relaxed);
        wreck_.store(static_cast<float>(wreck), std::memory_order_relaxed);
    }
    // The garden around the mower; read by the renderer under the same lock-free rule:
    // whole small values, each written once a frame.
    void set_garden(const AmbienceControls& controls) {
        const std::lock_guard<std::mutex> lock(garden_guard_);
        garden_ = controls;
    }
    void render(std::span<float> stereo) noexcept override {
        MowerControls controls{};
        controls.ignition = ignition_.load(std::memory_order_relaxed);
        controls.governed_rpm = 3600;
        if (controls.ignition)
            running_frames_ += stereo.size() / 2;
        else
            running_frames_ = 0;
        controls.blades = running_frames_ > static_cast<std::uint64_t>(MowerVoice::sample_rate) * 17 / 10;
        controls.load = controls.blades ? static_cast<double>(load_.load(std::memory_order_relaxed)) : 0.0;
        voice_.control(controls);
        voice_.render(stereo);
        chipper_.render_add(stereo, controls.blades ? static_cast<double>(wreck_.load(std::memory_order_relaxed)) : 0.0);
        if (garden_guard_.try_lock()) {
            ambience_.set(garden_);
            garden_guard_.unlock();
        }
        ambience_.render_add(stereo);
        granny_.render_add(stereo);
    }
    void say(GrannyLine line, double pan) {
        granny_.say(line, pan);
    }

  private:
    MowerVoice voice_{1};
    ChipperVoice chipper_{3};
    AmbienceVoice ambience_{11};
    GrannyVoice granny_{5};
    AmbienceControls garden_{};
    std::mutex garden_guard_{};
    std::atomic<bool> ignition_{false};
    std::atomic<float> load_{0};
    std::atomic<float> wreck_{0};
    std::uint64_t running_frames_{};
};

// The music: a bluegrass band improvising as it plays, never the same twice.
class LiveBand final : public gui_forms::AudioGenerator {
  public:
    void render(std::span<float> stereo) noexcept override {
        std::fill(stereo.begin(), stereo.end(), 0.0F);
        band_.render_add(stereo, 1.0);
    }

  private:
    BanjoVoice band_{static_cast<std::uint32_t>(std::random_device{}())};
};
#endif

namespace {

template <typename Result> bool finished(const std::future<Result>& future) {
    const bool result = future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    return result;
}

GrassArt grow_grass(std::uint64_t seed, const std::atomic<bool>* cancel) {
    GrassArt art = make_grass_art(make_garden(seed), cancel);
    return art;
}

Livery livery_from(const std::string& key) {
    if (key == "t")
        return Livery::red_t;
    if (key == "jd")
        return Livery::green_jd;
    return Livery::orange_h;
}

ambient::HostSetup host_setup() {
    ambient::HostSetup host{};
    host.id = "mowingman";
    host.settings_file = "mowing_man-v1.txt";
    host.accessible_name = "Mowing. Eggy mows the garden on a ride-on mower. Drag the mower to take over.";
    host.help_title = "Mowing";
    host.help_paragraphs = {
        "Eggy mows the whole garden on his own: the mower sees a little way ahead, steers round the beds, "
        "and works up and down until every blade is cut. Then the grass grows back somewhere new.",
        "Drag the mower to drive it yourself; let go and it carries on from wherever you leave it.",
        "Now and then a garden gnome pops up out of the long grass for a look round. Click him while he is up "
        "and he is stuck where he stands, until the mower finds him.",
        "Drive over a flower bed and the old lady who planted it comes after you with her rolling pin. Keep out of her reach for half a minute and a gnome will see her off; let her catch you and the garden starts again.",
        "E turns the engine off and on. Mower changes between the orange, red and green machines. Space pauses."};
    host.limits = ambient::CadenceLimits{.frames_preferred = 30,
                                         .frames_lowest = 15,
                                         .sway_preferred = 8,
                                         .sway_lowest = 3,
                                         .budget = 0.10};
    host.backdrop = 0x3E6B2A;
    return host;
}

} // namespace

MowingScenery::MowingScenery() {
    mowing_ = std::make_unique<Mowing>(seed_, livery_);
    start_art(seed_);
}

MowingScenery::~MowingScenery() {
    cancel_.store(true);
    if (art_loading_.valid())
        art_loading_.wait();
}

void MowingScenery::start_art(std::uint64_t seed) {
    cancel_.store(false);
    art_loading_ = std::async(std::launch::async, &grow_grass, seed, &cancel_);
}

bool MowingScenery::ready() const {
    return yard_.built();
}

// One scene pixel per point at Balanced: the lawn is retained, so finer pictures
// cost little more than coarser ones. Fine draws device pixels, Light one pixel per
// two points.
ambient::SceneSize MowingScenery::size_for(int device_width, int device_height, double scale,
                                           ambient::Detail detail) const {
    double per = std::max(1.0, scale);
    if (detail == ambient::Detail::fine)
        per = 1.0;
    if (detail == ambient::Detail::light)
        per = std::max(1.0, scale * 2);
    double width = device_width / per;
    double height = device_height / per;
    const double budget = 2400000.0;
    if (width * height > budget) {
        const double shrink = std::sqrt(budget / (width * height));
        width *= shrink;
        height *= shrink;
    }
    ambient::SceneSize size{std::max(16, static_cast<int>(std::lround(width))),
                            std::max(16, static_cast<int>(std::lround(height)))};
    return size;
}

void MowingScenery::resize(ambient::SceneSize size, bool settled) {
    if (size.width != wanted_.width || size.height != wanted_.height)
        dirty_all_ = true;
    wanted_ = size;
    settled_ = settled;
}

void MowingScenery::rebuild() {
    if (wanted_.width <= 0 || !mowing_)
        return;
    yard_.build(wanted_.width, wanted_.height, art_, *mowing_);
    static_cast<void>((*mowing_).take_dirty());
    dirty_all_ = false;
}

bool MowingScenery::poll(ambient::SceneContext& context) {
    if (!livery_restored_) {
        livery_restored_ = true;
        livery_ = livery_from(ambient::setting(context.settings, "mower", "h"));
        (*mowing_).set_livery(livery_);
    }
    if (finished(art_loading_)) {
        art_ = art_loading_.get();
        if (next_) {
            // The next garden's grass is ready: fade from the finished one into it.
            previous_ = picture_;
            fade_ = previous_.empty() ? 0 : 1;
            mowing_ = std::move(next_);
            keyboard_ = false;
        }
        dirty_all_ = true;
    }
    // The first picture is drawn at once (with plain grass until the art is ready);
    // later sizes wait for a resize to settle.
    if (dirty_all_ && wanted_.width > 0 && (!yard_.built() || settled_ || art_.empty()))
        rebuild();
    return art_loading_.valid();
}

void MowingScenery::next_garden() {
    seed_ += 1;
    next_ = std::make_unique<Mowing>(seed_, livery_);
    start_art(seed_);
}

void MowingScenery::advance(double seconds, ambient::SceneContext& context) {
    Mowing& mowing = *mowing_;
    // The arrow keys (or WASD) drive just as a hand on the mower does: held, they take the
    // controls and steer it the way they point; let go, Eggy has it back.
    {
        const double dx = (keys_[3] ? 1.0 : 0.0) - (keys_[1] ? 1.0 : 0.0);
        const double dy = (keys_[2] ? 1.0 : 0.0) - (keys_[0] ? 1.0 : 0.0);
        const coverage::Pose& pose = mowing.mower().pose();
        if (dx != 0 || dy != 0) {
            if (!keyboard_)
                keyboard_ = mowing.grab(pose.x, pose.y);
            if (keyboard_) {
                const double length = std::hypot(dx, dy);
                mowing.steer(pose.x + dx / length * 2.0, pose.y + dy / length * 2.0);
            }
        } else if (keyboard_) {
            mowing.let_go();
            keyboard_ = false;
        }
    }
    mowing.advance(seconds, context.reduced);
    if (yard_.built() && !dirty_all_)
        yard_.refresh(art_, mowing, mowing.take_dirty());
    for (const Cue& cue : mowing.take_cues()) {
#ifdef GUI_FORMS_AUDIO_GENERATOR
        // the live voice starts, stops and grinds itself, and speaks for the old lady
        if (cue.name == "mm_mower_start" || cue.name == "mm_mower_stop" || cue.name == "mm_chipper")
            continue;
        if (cue.name.rfind("mm_granny_", 0) == 0 && cue.name != "mm_granny_bonk") {
            if (live_) {
                const GrannyLine line = cue.name == "mm_granny_shout" ? GrannyLine::shout : cue.name == "mm_granny_scold" ? GrannyLine::scold
                                                                                           : cue.name == "mm_granny_shriek" ? GrannyLine::shriek : GrannyLine::wail;
                (*live_).say(line, cue.pan);
            }
            continue;
        }
#endif
        context.sound.effect(cue.name, cue.gain, cue.rate, cue.pan);
    }
    if (mowing.complete() && !next_ && !art_loading_.valid())
        next_garden();
    if (mowing.caught()) {
        // Caught by the old lady: the same garden, from the start. Its grass is already grown.
        const bool engine = mowing.engine_on();
        mowing_ = std::make_unique<Mowing>(seed_, livery_);
        keyboard_ = false;
        (*mowing_).set_engine(engine);
        static_cast<void>((*mowing_).take_cues());
        dirty_all_ = true;
        return;
    }
    fade_ = std::max(0.0, fade_ - seconds / 1.4);
    // Eggy keeps an eye out: he looks toward the gnome when he is up, else glances about.
    const Gnome& gnome = mowing.gnome();
    const coverage::Pose& pose = mowing.mower().pose();
    double target = 0.5 * std::sin(context.time * 0.37) * std::sin(context.time * 0.11);
    if (gnome.state == GnomeState::looking || gnome.state == GnomeState::frozen) {
        double toward = std::atan2(gnome.y - pose.y, gnome.x - pose.x) - pose.heading;
        toward = std::atan2(std::sin(toward), std::cos(toward));
        target = std::clamp(toward, -1.3, 1.3);
    }
    eggy_look_ += (target - eggy_look_) * (1 - std::exp(-seconds / 0.25));
}

const std::vector<std::uint32_t>& MowingScenery::draw(ambient::SceneContext& context, int& width, int& height) {
    const Mowing& mowing = *mowing_;
    const coverage::Pose& pose = mowing.mower().pose();
    const MowerPose drawn{pose.x, pose.y, pose.heading, context.paused ? 0.0 : 0.4 + mowing.voice().load * 0.6, eggy_look_,
                          mowing.held()};
    const std::vector<std::uint8_t>& bytes = yard_.compose(mowing, drawn, context.time);
    width = yard_.width();
    height = yard_.height();
    picture_.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    // Canvas pixels are BGRA bytes: on little-endian machines, 0xAARRGGBB words.
    std::memcpy(picture_.data(), bytes.data(), picture_.size() * sizeof(std::uint32_t));
    if (fade_ > 0 && previous_.size() == picture_.size()) {
        const std::uint32_t keep = static_cast<std::uint32_t>(fade_ * 256);
        const std::uint32_t take = 256U - keep;
        for (std::size_t index = 0; index < picture_.size(); ++index) {
            const std::uint32_t a = previous_[index];
            const std::uint32_t b = picture_[index];
            const std::uint32_t red_blue = (((a & 0xFF00FFU) * keep + (b & 0xFF00FFU) * take) >> 8U) & 0xFF00FFU;
            const std::uint32_t green = (((a & 0x00FF00U) * keep + (b & 0x00FF00U) * take) >> 8U) & 0x00FF00U;
            picture_[index] = 0xFF000000U | red_blue | green;
        }
    }
    return picture_;
}

double MowingScenery::lawn_x(double fraction) const {
    return yard_.lawn_x(fraction * yard_.width());
}

double MowingScenery::lawn_y(double fraction) const {
    return yard_.lawn_y(fraction * yard_.height());
}

void MowingScenery::press(double x, double y, ambient::SceneContext&) {
    if (!ready())
        return;
    const double wx = lawn_x(x);
    const double wy = lawn_y(y);
    // The gnome first: a click on him freezes him even if the mower is near.
    if (!(*mowing_).poke(wx, wy))
        static_cast<void>((*mowing_).grab(wx, wy));
}

void MowingScenery::drag(double x, double y, ambient::SceneContext&) {
    if (ready())
        (*mowing_).steer(lawn_x(x), lawn_y(y));
}

void MowingScenery::release(double, double, ambient::SceneContext&) {
    (*mowing_).let_go();
}

bool MowingScenery::grabbable(double x, double y) const {
    if (!ready())
        return false;
    return (*mowing_).near_mower(lawn_x(x), lawn_y(y));
}

void MowingScenery::add_commands(std::vector<games::GameCommand>& list, const ambient::Settings&) const {
    list.push_back({"new-garden", "New Garden", true, false, false});
    list.push_back({"engine", (*mowing_).engine_on() ? "Engine: On" : "Engine: Off", true, false, false});
}

void MowingScenery::add_settings(std::vector<games::GameSetting>& list, const ambient::Settings&) const {
    games::GameSetting mower{"mower", "Mower", games::GameSetting::Kind::choice,
                             static_cast<double>(static_cast<int>(livery_))};
    for (int i = 0; i < livery_count; ++i)
        mower.choices.push_back(livery_name(static_cast<Livery>(i)));
    list.push_back(mower);
}

bool MowingScenery::change_setting(std::string_view id, double value, ambient::SceneContext& context) {
    const int chosen = static_cast<int>(std::lround(value));
    if (id != "mower" || chosen < 0 || chosen >= livery_count || chosen == static_cast<int>(livery_))
        return false;
    livery_ = static_cast<Livery>(chosen);
    (*mowing_).set_livery(livery_);
    ambient::set_setting(context.settings, "mower", livery_key(livery_));
    context.persist = true;
    return true;
}

bool MowingScenery::run_command(std::string_view id, ambient::SceneContext& context) {
    if (id == "new-garden") {
        if (!next_ && !art_loading_.valid())
            next_garden();
        return true;
    }
    if (id == "engine") {
        (*mowing_).set_engine(!(*mowing_).engine_on());
        return true;
    }
    if (id != "mower")
        return false;
    livery_ = static_cast<Livery>((static_cast<int>(livery_) + 1) % livery_count);
    (*mowing_).set_livery(livery_);
    ambient::set_setting(context.settings, "mower", livery_key(livery_));
    context.persist = true;
    return true;
}

bool MowingScenery::hold_key(std::uint32_t key, bool down, ambient::SceneContext&) {
    using Key = ambient::gf::PhysicalKey;
    int slot = -1;
    if (key == Key::w || key == Key::up)
        slot = 0;
    else if (key == Key::a || key == Key::left)
        slot = 1;
    else if (key == Key::s || key == Key::down)
        slot = 2;
    else if (key == Key::d || key == Key::right)
        slot = 3;
    if (slot < 0)
        return false;
    keys_[slot] = down;
    return true;
}

std::string MowingScenery::key_command(std::uint32_t key) const {
    if (key == ambient::gf::PhysicalKey::e)
        return "engine";
    if (key == ambient::gf::PhysicalKey::n)
        return "new-garden";
    return {};
}

bool MowingScenery::scripted_action(std::string_view action, ambient::SceneContext& context) {
    std::istringstream input{std::string(action)};
    std::string verb{};
    input >> verb;
    double x = -1;
    double y = -1;
    if (verb == "grab" || verb == "steer") {
        input >> x >> y;
        if (!input || x < 0 || x > 1 || y < 0 || y > 1)
            return false;
        if (verb == "grab")
            press(x, y, context);
        else
            drag(x, y, context);
        return true;
    }
    if (verb == "engine") {
        (*mowing_).set_engine(!(*mowing_).engine_on());
        return true;
    }
    if (verb == "release") {
        release(0, 0, context);
        return true;
    }
    if (verb == "freeze-gnome") {
        const Gnome& gnome = (*mowing_).gnome();
        return (*mowing_).poke(gnome.x, gnome.y);
    }
    if (verb == "grab-mower") {
        const coverage::Pose& pose = (*mowing_).mower().pose();
        return (*mowing_).grab(pose.x, pose.y);
    }
    return false;
}

void MowingScenery::sound(bool running, ambient::SceneContext& context) {
    if (!running || !ready()) {
        context.sound.quiet_beds();
        return;
    }
    const Mowing& mowing = *mowing_;
#ifdef GUI_FORMS_AUDIO_GENERATOR
    // The music is a banjo band improvising live (banjo_voice.hpp), under the Music control.
    if (!band_)
        band_ = std::make_shared<LiveBand>();
    context.sound.live_music(band_, 0.8);
    // The mower's voice is synthesized live: the key and the grass under the deck go
    // straight to it, and it starts, labours, clears and spins down on its own.
    if (!live_)
        live_ = std::make_shared<LiveMower>();
    (*live_).set(mowing.engine_on(), 0.75 * mowing.voice().load, mowing.wreck());
    AmbienceControls garden{};
    garden.engine_on = mowing.engine_on();
    // Each bee as heard from above the middle of the lawn: louder as it comes near, its
    // pitch lifted by speed and by its approach (Doppler), silent when it has landed.
    {
        int slot = 0;
        const double ear_x = lawn_width * 0.5;
        const double ear_y = lawn_height * 0.6;
        const double ear_z = lawn_width * 0.45;
        for (const Bee& bee : mowing.bees()) {
            if (slot >= AmbienceControls::max_bees)
                break;
            BeeSound& sound = garden.bees[slot];
            ++slot;
            const double dx = bee.x - ear_x;
            const double dy = bee.y - ear_y;
            const double dz = bee.z - ear_z;
            const double d = std::max(0.5, std::sqrt(dx * dx + dy * dy + dz * dz));
            const double speed = std::hypot(bee.vx, bee.vy);
            const double closing = -(dx * bee.vx + dy * bee.vy) / d;
            sound.id = bee.id;
            sound.flying = !(bee.state == BeeState::visiting && bee.z < 0.065);
            sound.gain = std::min(1.0, std::pow(ear_z / d, 2.0));
            sound.pan = std::clamp(dx / (lawn_width * 0.5), -1.0, 1.0) * 0.8;
            sound.pitch = (1 + 0.03 * std::min(1.0, speed / 2.4)) / (1 - closing / 343.0);
        }
    }
    for (const Bird& bird : mowing.birds()) {
        if (bird.singing) {
            garden.singing[bird.kind] = true;
            garden.bird_pan[bird.kind] = std::clamp(bird.x / lawn_width * 2 - 1, -1.0, 1.0) * 0.7;
        }
    }
    for (const Prop& prop : mowing.garden().props) {
        garden.fountain = garden.fountain || prop.kind == PropKind::fountain;
        garden.water = garden.water || prop.kind == PropKind::fountain || prop.kind == PropKind::birdbath || prop.kind == PropKind::pool;
    }
    garden.trees = std::min(1.0, mowing.garden().trees.size() * 0.4);
    (*live_).set_garden(garden);
    context.sound.live(live_, 0.85);
#else
    // Without a live voice in the toolkit: the same voice rendered to one long loop of
    // mowing (see audio_src), with one-shots for starting and stopping. The loop takes
    // over as the starting clip settles and gives way as the stopping clip begins.
    constexpr double start_hands_over = 4.6;
    const bool mowing_sound = mowing.engine_on() && mowing.engine_seconds() >= start_hands_over;
    context.sound.bed("mm_mower_run", mowing_sound ? 0.74 + 0.10 * mowing.voice().load : 0.0, 1.0);
    // An easy summer tune to mow to (audio_src/make_audio.py), under the Music control.
    context.sound.music("mm_music_mowing", true);
#endif
}

MowingView::MowingView(ambient::gf::StableId id, ambient::ViewOptions options)
    : SceneView(std::move(id), host_setup(), std::make_unique<MowingScenery>(), options) {}

} // namespace mm
