#include "diorama.hpp"

#include "runtime_paths.hpp"
#include "scene_view.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace ambient {
namespace {

using Clock = std::chrono::steady_clock;

// A change of scene: the shown scene dims to dark, then the next one brightens.
constexpr double fade_out_seconds = 0.45;
constexpr double fade_in_seconds = 0.6;

template <typename Result> bool finished(const std::future<Result>& future) {
    const bool result = future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    return result;
}

template <typename Result> void abandon(std::future<Result>& future) {
    if (future.valid())
        future.wait();
    future = std::future<Result>{};
}

} // namespace

Diorama::Diorama(DioramaSetup setup) : setup_(std::move(setup)) {
    scenes_ = setup_.scenes;
    if (scenes_.empty()) {
        DioramaScene only{};
        only.id = "scene";
        only.name = "Scene";
        only.archive = setup_.archive;
        only.make_look = setup_.make_look;
        only.dress = setup_.dress;
        only.bounds = setup_.bounds;
        scenes_.push_back(only);
    }
    // A single scene starts loading at once; a choice of scenes waits for the first
    // poll, which brings the saved settings.
    if (scenes_.size() == 1) {
        started_ = true;
        start_loading(0);
    }
}

Diorama::~Diorama() {
    // Workers read the bundles and the stages; wait for them before anything they use goes away.
    if (swaying_.valid())
        swaying_.wait();
    if (building_.valid())
        building_.wait();
    if (next_building_.valid())
        next_building_.wait();
    if (loading_.valid())
        loading_.wait();
}

std::unique_ptr<Diorama::Bundle> Diorama::load_bundle(std::filesystem::path archive, LookFactory make_look,
                                                      SceneDressing dress) {
    std::unique_ptr<Bundle> bundle = std::make_unique<Bundle>();
    if (!load_scene(archive.string(), (*bundle).scene, (*bundle).error))
        return bundle;
    if (dress != nullptr)
        dress((*bundle).scene);
    if (make_look == nullptr) {
        (*bundle).error = "The scene has no look";
        return bundle;
    }
    (*bundle).look = make_look((*bundle).scene);
    (*bundle).shadows = build_shadow_map((*bundle).scene, *(*bundle).look, 1024);
    (*bundle).foliage = prepare_foliage((*bundle).scene, *(*bundle).look, (*bundle).shadows);
    return bundle;
}

void Diorama::start_loading(std::size_t scene) {
    const DioramaScene& choice = scenes_[scene];
    const std::filesystem::path archive = std::filesystem::path(games::asset_directory()) / setup_.id / choice.archive;
    loading_scene_ = scene;
    loading_ = std::async(std::launch::async, &Diorama::load_bundle, archive, choice.make_look, choice.dress);
}

bool Diorama::ready() const {
    const bool result = shown_.stage && (*shown_.stage).ready();
    return result;
}

std::string Diorama::failure() const {
    return error_;
}

void Diorama::resize(SceneSize size, bool settled) {
    wanted_ = size;
    settled_ = settled;
}

double Diorama::fade_level() const {
    const double elapsed = std::chrono::duration<double>(Clock::now() - fade_start_).count();
    if (fade_ == Fade::out)
        return std::max(0.0, fade_from_ - elapsed / fade_out_seconds);
    if (fade_ == Fade::in)
        return std::min(1.0, fade_from_ + elapsed / fade_in_seconds);
    return 1.0;
}

bool Diorama::poll(SceneContext& context) {
    if (!started_) {
        started_ = true;
        const std::string saved = setting(context.settings, "scene", scenes_[0].id);
        for (std::size_t index = 0; index < scenes_.size(); ++index) {
            if (scenes_[index].id == saved)
                chosen_ = index;
        }
        start_loading(chosen_);
    }
    if (finished(loading_)) {
        std::unique_ptr<Bundle> bundle = loading_.get();
        const bool usable = bundle && (*bundle).error.empty();
        if (!shown_.stage && loading_scene_ != chosen_) {
            // The player chose another scene before the first one was shown.
            start_loading(chosen_);
        } else if (!shown_.stage) {
            // The first scene: shown as soon as its first layer is built.
            if (usable) {
                shown_.stage = std::make_unique<Stage>((*bundle).scene, *(*bundle).look, (*bundle).foliage);
                shown_.creatures = creatures_from((*bundle).scene.actors);
                shown_.scene = loading_scene_;
                shown_.bundle = std::move(bundle);
            } else {
                error_ = bundle ? (*bundle).error : "The scene could not be loaded";
            }
        } else if (loading_scene_ != chosen_) {
            // The player moved on while it loaded.
            if (chosen_ != shown_.scene)
                start_loading(chosen_);
        } else if (usable) {
            next_.stage = std::make_unique<Stage>((*bundle).scene, *(*bundle).look, (*bundle).foliage);
            next_.creatures = creatures_from((*bundle).scene.actors);
            next_.scene = loading_scene_;
            next_.bundle = std::move(bundle);
        } else {
            // A scene that cannot load leaves the shown one in place.
            chosen_ = shown_.scene;
            set_setting(context.settings, "scene", scenes_[chosen_].id);
            context.persist = true;
            fade_from_ = fade_level();
            fade_start_ = Clock::now();
            fade_ = Fade::in;
        }
    }
    // The next scene's first layer, at the size the view wants now.
    if (finished(next_building_)) {
        FixedLayer layer = next_building_.get();
        if (next_.stage && layer.width == wanted_.width && layer.height == wanted_.height)
            (*next_.stage).adopt(std::move(layer));
    }
    if (next_.stage && !(*next_.stage).ready() && !next_building_.valid() && wanted_.width > 0) {
        next_size_ = wanted_;
        const Bundle& bundle = *next_.bundle;
        next_building_ = std::async(std::launch::async, &build_fixed_layer, std::cref(bundle.scene),
                                    std::cref(*bundle.look), std::cref(bundle.shadows), std::cref(bundle.foliage),
                                    next_size_.width, next_size_.height, 2, 0.5F);
    }
    if (next_.stage && (*next_.stage).ready() && fade_ == Fade::out && fade_level() <= 0.0) {
        swap_in();
        fade_from_ = 0;
        fade_start_ = Clock::now();
        fade_ = Fade::in;
    }
    if (fade_ == Fade::in && fade_level() >= 1.0)
        fade_ = Fade::none;
    if (fade_ != Fade::none)
        context.redraw = true;

    if (finished(building_)) {
        FixedLayer layer = building_.get();
        if (shown_.stage && layer.width == building_size_.width && layer.height == building_size_.height) {
            finish_sway(nullptr);  // a sway build for the old size is discarded
            (*shown_.stage).adopt(std::move(layer));
            last_sway_ = -1;
        }
    }
    if (shown_.stage && !building_.valid() && wanted_.width > 0) {
        const bool stale = (*shown_.stage).width() != wanted_.width || (*shown_.stage).height() != wanted_.height;
        // The first layer is built at once; later ones wait for a resize to settle, and
        // the previous picture is enlarged to the new size in the meantime.
        if (stale && (!(*shown_.stage).ready() || settled_))
            start_build();
    }
    const bool pending = loading_.valid() || building_.valid() || next_building_.valid() || next_.stage != nullptr ||
                         fade_ != Fade::none;
    return pending;
}

void Diorama::swap_in() {
    // Nothing may still be drawing into the outgoing stage.
    finish_sway(nullptr);
    abandon(building_);
    shown_.stage.reset();
    shown_.bundle.reset();
    shown_ = std::move(next_);
    next_ = Showing{};
    last_sway_ = -1;
}

void Diorama::choose(std::size_t scene, SceneContext& context) {
    if (scene >= scenes_.size() || scene == chosen_)
        return;
    chosen_ = scene;
    set_setting(context.settings, "scene", scenes_[chosen_].id);
    context.persist = true;
    context.redraw = true;
    if (!shown_.stage) {
        // Still loading the first scene: load the new choice instead once it is done.
        return;
    }
    if (next_.stage && next_.scene != chosen_) {
        abandon(next_building_);
        next_.stage.reset();
        next_.bundle.reset();
        next_ = Showing{};
    }
    if (chosen_ == shown_.scene) {
        // Back to the scene still showing: brighten it again from where it is.
        fade_from_ = fade_level();
        fade_start_ = Clock::now();
        fade_ = Fade::in;
        return;
    }
    if (!loading_.valid() && !next_.stage)
        start_loading(chosen_);
    if (fade_ != Fade::out) {
        fade_from_ = fade_level();
        fade_start_ = Clock::now();
        fade_ = Fade::out;
    }
}

void Diorama::start_build() {
    building_size_ = wanted_;
    const Bundle& bundle = *shown_.bundle;
    building_ = std::async(std::launch::async, &build_fixed_layer, std::cref(bundle.scene), std::cref(*bundle.look),
                           std::cref(bundle.shadows), std::cref(bundle.foliage), building_size_.width,
                           building_size_.height, 2, 0.5F);
}

void Diorama::advance(double, SceneContext& context) {
    if (!context.reduced)
        light_time_ = context.time;
}

const std::vector<std::uint32_t>& Diorama::draw(SceneContext& context, int& width, int& height) {
    width = 0;
    height = 0;
    if (!ready())
        return empty_;
    Stage& stage = *shown_.stage;
    const Rates current = context.governor.rates();
    const bool first = last_sway_ < 0;
    const double time = context.time;
    const bool due = time - last_sway_ >= 1.0 / std::max(current.sway, 0.5) || time < last_sway_;
    // A finished foliage update is shown with this frame.
    if (finished(swaying_))
        finish_sway(&context);
    if (first) {
        // The first picture waits for its foliage.
        finish_sway(nullptr);
        const Clock::time_point start = Clock::now();
        stage.update_sway(time);
        context.charged += std::chrono::duration<double>(Clock::now() - start).count();
        last_sway_ = time;
    } else if (!context.reduced && !context.paused && due && !swaying_.valid()) {
        // Later updates run beside the frames, so a frame never waits for the grass.
        swaying_ = std::async(std::launch::async, &Diorama::sway_job, &stage, time);
        last_sway_ = time;
    }
    stage.compose(time, light_time_, shown_.creatures);
    width = stage.width();
    height = stage.height();
    const double level = fade_level();
    if (level >= 1.0)
        return stage.frame();
    // Dimmed toward dark for a change of scene: each channel scaled by the level.
    const std::vector<std::uint32_t>& frame = stage.frame();
    faded_.resize(frame.size());
    const std::uint32_t k = static_cast<std::uint32_t>(std::clamp(level, 0.0, 1.0) * 256.0);
    for (std::size_t index = 0; index < frame.size(); ++index) {
        const std::uint32_t p = frame[index];
        const std::uint32_t red_blue = (((p & 0xFF00FFU) * k) >> 8U) & 0xFF00FFU;
        const std::uint32_t green = (((p & 0x00FF00U) * k) >> 8U) & 0x00FF00U;
        faded_[index] = 0xFF000000U | red_blue | green;
    }
    return faded_;
}

double Diorama::sway_job(Stage* stage, double time) {
    const Clock::time_point start = Clock::now();
    (*stage).build_sway(time);
    const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
    return seconds;
}

// Collects a foliage update: shown and charged to the governor when a context is
// given, waited for and dropped otherwise.
void Diorama::finish_sway(SceneContext* context) {
    if (!swaying_.valid())
        return;
    const double seconds = swaying_.get();
    if (context == nullptr || !shown_.stage)
        return;
    (*shown_.stage).show_sway();
    (*context).governor.record_sway(seconds);
}

void Diorama::tap(double x, double y, SceneContext& context) {
    if (!ready())
        return;
    const Projection& projection = (*shown_.stage).projection();
    const ScreenTap at{x, y, static_cast<double>(projection.width) / std::max(1, projection.height), 0.2};
    const std::size_t startled =
        startle(shown_.creatures, projection, at, context.time, scenes_[shown_.scene].bounds);
    bool voiced = false;
#ifdef GUI_FORMS_AUDIO_GENERATOR
    // A live bed plays the tap itself (varied and placed); the clip is the fallback.
    if (ambience_voice_ && context.sound_on)
        voiced = (*ambience_voice_).tap(x, y, startled);
#endif
    if (!voiced && !setup_.tap_sound.empty())
        context.sound.effect(setup_.tap_sound, startled > 0 ? 0.85 : 0.6, 1.0, x * 2 - 1);
}

void Diorama::press(double x, double y, SceneContext& context) {
    tap(x, y, context);
}

// The choice of scene is a setting (and S steps through it, as the "scene" command).
void Diorama::add_settings(std::vector<games::GameSetting>& list, const Settings&) const {
    if (scenes_.size() < 2)
        return;
    games::GameSetting choice{"tank", "Tank", games::GameSetting::Kind::choice, static_cast<double>(chosen_), {},
                              0, static_cast<double>(scenes_.size() - 1), 1, "S moves to the next one."};
    for (const DioramaScene& scene : scenes_)
        choice.choices.push_back(scene.name);
    list.push_back(choice);
}

bool Diorama::change_setting(std::string_view id, double value, SceneContext& context) {
    const long index = std::lround(value);
    if (id != "tank" || index < 0 || index >= static_cast<long>(scenes_.size()))
        return false;
    if (static_cast<std::size_t>(index) != chosen_)
        choose(static_cast<std::size_t>(index), context);
    return true;
}

bool Diorama::run_command(std::string_view id, SceneContext& context) {
    if (id != "scene" || scenes_.size() < 2)
        return false;
    choose((chosen_ + 1) % scenes_.size(), context);
    return true;
}

std::string Diorama::key_command(std::uint32_t key) const {
    if (scenes_.size() >= 2 && key == gf::PhysicalKey::s)
        return "scene";
    return {};
}

bool Diorama::scripted_action(std::string_view action, SceneContext& context) {
    std::istringstream input{std::string(action)};
    std::string verb{};
    input >> verb;
    if (verb == "scene") {
        std::string id{};
        input >> id;
        for (std::size_t index = 0; index < scenes_.size(); ++index) {
            if (scenes_[index].id == id) {
                choose(index, context);
                return true;
            }
        }
        return false;
    }
    if (verb != "tap")
        return false;
    double x = -1;
    double y = -1;
    input >> x >> y;
    if (!input || x < 0 || x > 1 || y < 0 || y > 1)
        return false;
    tap(x, y, context);
    return true;
}

void Diorama::sound(bool running, SceneContext& context) {
    // The water is sound, under the Sound master; music is under the Music master.
    // Both play while the tank runs and stop with it.
    if (!running) {
        context.sound.quiet_beds();
        context.sound.music(setup_.music, false);
        return;
    }
#ifdef GUI_FORMS_AUDIO_GENERATOR
    if (setup_.live_ambience != nullptr) {
        if (!ambience_voice_)
            ambience_voice_ = setup_.live_ambience();
        if (ambience_voice_ && shown_.stage && voiced_scene_ != shown_.scene) {
            voiced_scene_ = shown_.scene;
            (*ambience_voice_).show_scene(voiced_scene_);
        }
        const std::shared_ptr<gui_forms::AudioGenerator> voice = ambience_voice_;
        context.sound.live(voice, 1.0);
    } else {
        context.sound.bed(setup_.ambience, 1.0);
    }
    if (setup_.live_music != nullptr) {
        if (!music_voice_)
            music_voice_ = setup_.live_music();
        context.sound.live_music(music_voice_, 1.0);
    } else {
        context.sound.music(setup_.music, !setup_.music.empty());
    }
#else
    context.sound.bed(setup_.ambience, 1.0);
    context.sound.music(setup_.music, !setup_.music.empty());
#endif
}

} // namespace ambient
