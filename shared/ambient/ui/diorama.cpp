#include "diorama.hpp"

#include "runtime_paths.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace ambient {
namespace {

template <typename Result> bool finished(const std::future<Result>& future) {
    const bool result = future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    return result;
}

} // namespace

Diorama::Diorama(DioramaSetup setup) : setup_(std::move(setup)) {
    const std::filesystem::path archive = std::filesystem::path(games::asset_directory()) / setup_.id / setup_.archive;
    loading_ = std::async(std::launch::async, &Diorama::load_bundle, archive, setup_.make_look, setup_.dress);
}

Diorama::~Diorama() {
    // Workers read the bundle and the stage; wait for them before anything they use goes away.
    if (swaying_.valid())
        swaying_.wait();
    if (building_.valid())
        building_.wait();
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

bool Diorama::ready() const {
    const bool result = stage_ && (*stage_).ready();
    return result;
}

std::string Diorama::failure() const {
    if (bundle_)
        return (*bundle_).error;
    return {};
}

void Diorama::resize(SceneSize size, bool settled) {
    wanted_ = size;
    settled_ = settled;
}

bool Diorama::poll(SceneContext&) {
    if (finished(loading_)) {
        bundle_ = loading_.get();
        if (bundle_ && (*bundle_).error.empty()) {
            stage_ = std::make_unique<Stage>((*bundle_).scene, *(*bundle_).look, (*bundle_).foliage);
            creatures_ = creatures_from((*bundle_).scene.actors);
        }
    }
    if (finished(building_)) {
        FixedLayer layer = building_.get();
        if (stage_ && layer.width == building_size_.width && layer.height == building_size_.height) {
            finish_sway(nullptr);  // a sway build for the old size is discarded
            (*stage_).adopt(std::move(layer));
            last_sway_ = -1;
        }
    }
    if (stage_ && !building_.valid() && wanted_.width > 0) {
        const bool stale = (*stage_).width() != wanted_.width || (*stage_).height() != wanted_.height;
        // The first layer is built at once; later ones wait for a resize to settle, and
        // the previous picture is enlarged to the new size in the meantime.
        if (stale && (!(*stage_).ready() || settled_))
            start_build();
    }
    const bool pending = loading_.valid() || building_.valid();
    return pending;
}

void Diorama::start_build() {
    building_size_ = wanted_;
    building_ = std::async(std::launch::async, &build_fixed_layer, std::cref((*bundle_).scene),
                           std::cref(*(*bundle_).look), std::cref((*bundle_).shadows), std::cref((*bundle_).foliage),
                           building_size_.width, building_size_.height, 2, 0.5F);
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
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        (*stage_).update_sway(time);
        context.charged += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        last_sway_ = time;
    } else if (!context.reduced && !context.paused && due && !swaying_.valid()) {
        // Later updates run beside the frames, so a frame never waits for the grass.
        swaying_ = std::async(std::launch::async, &Diorama::sway_job, stage_.get(), time);
        last_sway_ = time;
    }
    (*stage_).compose(time, light_time_, creatures_);
    width = (*stage_).width();
    height = (*stage_).height();
    return (*stage_).frame();
}

double Diorama::sway_job(Stage* stage, double time) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    (*stage).build_sway(time);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return seconds;
}

// Collects a foliage update: shown and charged to the governor when a context is
// given, waited for and dropped otherwise.
void Diorama::finish_sway(SceneContext* context) {
    if (!swaying_.valid())
        return;
    const double seconds = swaying_.get();
    if (context == nullptr || !stage_)
        return;
    (*stage_).show_sway();
    (*context).governor.record_sway(seconds);
}

void Diorama::tap(double x, double y, SceneContext& context) {
    if (!ready())
        return;
    const Projection& projection = (*stage_).projection();
    const ScreenTap at{x, y, static_cast<double>(projection.width) / std::max(1, projection.height), 0.2};
    const std::size_t startled = startle(creatures_, projection, at, context.time, setup_.bounds);
    if (!setup_.tap_sound.empty())
        context.sound.effect(setup_.tap_sound, startled > 0 ? 0.85 : 0.6, 1.0, x * 2 - 1);
}

void Diorama::press(double x, double y, SceneContext& context) {
    tap(x, y, context);
}

bool Diorama::scripted_action(std::string_view action, SceneContext& context) {
    std::istringstream input{std::string(action)};
    std::string verb{};
    input >> verb;
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
        context.sound.live(ambience_voice_, 1.0);
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
