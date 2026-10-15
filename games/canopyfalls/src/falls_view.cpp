#include "falls_view.hpp"

#include "falls_voice.hpp"
#include "live_composer.hpp"
#include "repertoire.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <thread>

namespace cf {

#ifdef GUI_FORMS_AUDIO_GENERATOR
// The falls, the pool and the forest, synthesized as they play (falls_voice.hpp).
class LiveForest final : public gui_forms::AudioGenerator {
  public:
    void render(std::span<float> stereo) noexcept override {
        std::fill(stereo.begin(), stereo.end(), 0.0F);
        voice_.render_add(stereo, 1.0);
    }

  private:
    FallsVoice voice_{static_cast<std::uint32_t>(std::random_device{}())};
};
#else
class LiveForest {};
#endif

namespace {

ambient::HostSetup host_setup() {
    ambient::HostSetup host{};
    host.id = "canopyfalls";
    host.settings_file = "canopy_falls-v1.txt";
    host.accessible_name = "Canopy Falls. A jungle waterfall seen from a walkway high over its pool.";
    host.help_title = "Canopy Falls";
    host.help_paragraphs = {
        "You stand on a wooden walkway high in the rainforest, looking out over a lily pool at a great curtain of "
        "water pouring forty metres over a basalt cliff. The sun stands high over its brink.",
        "There is nothing to do but look and listen: the falls roar, the forest calls, and a harp plays somewhere on "
        "the breeze. Space pauses; Detail in Settings trades sharpness for a lighter load on the computer."};
    // The falls are always moving, so the scene keeps its frames up and lets the
    // governor trim them before it goes over its share of the processor.
    host.limits = ambient::CadenceLimits{.frames_preferred = 24,
                                         .frames_lowest = 12,
                                         .sway_preferred = 8,
                                         .sway_lowest = 3,
                                         .budget = 0.14};
    host.backdrop = 0x2E4A36;
    return host;
}

// How many particles each detail asks of the falls.
double density_for(ambient::Detail detail) {
    if (detail == ambient::Detail::light)
        return .4;
    if (detail == ambient::Detail::fine)
        return 1.0;
    return .7;
}

// Takes bands in turn until there are none left (one of several threads at once).
void bake_bands(LiveFalls* falls, std::atomic<int>* next, int bands, const std::atomic<bool>* cancel) {
    for (;;) {
        if ((*cancel).load())
            return;
        const int band = (*next).fetch_add(1);
        if (band >= bands)
            return;
        (*falls).bake_band(band);
    }
}

// Bakes every band of the pending size over the cores, then finishes it. Runs on its
// own thread; `cancel` stops it between bands (the scenery is going away).
void bake(LiveFalls* falls, const std::atomic<bool>* cancel) {
    LiveFalls& f = *falls;
    f.prepare_once();
    const int bands = f.bands();
    std::atomic<int> next{0};
    const unsigned cores = std::max(1U, std::min(8U, std::thread::hardware_concurrency()));
    std::vector<std::future<void>> workers;
    for (unsigned k = 0; k < cores; ++k)
        workers.push_back(std::async(std::launch::async, &bake_bands, falls, &next, bands, cancel));
    for (std::future<void>& worker : workers)
        worker.wait();
    if (!(*cancel).load())
        f.finish_size();
}

} // namespace

FallsScenery::FallsScenery() {
    Look look{};
    look.sky.sun_elevation = 46 * pi / 180;
    look.sky.haze = 2.0;
    look.sky.haze_forward = .76;
    falls_ = std::make_unique<LiveFalls>(look, Camera{});
    (*falls_).set_density(density_for(detail_));
}

FallsScenery::~FallsScenery() {
    cancel_.store(true);
    if (baking_.valid())
        baking_.wait();
}

bool FallsScenery::ready() const {
    return (*falls_).ready();
}

// A scene pixel for every two points at Balanced (the falls' fine detail is in its
// motion), one per point at Fine, one per three at Light; never more than about two
// thirds of a million pixels, so a full-screen window costs no more than a large one.
ambient::SceneSize FallsScenery::size_for(int device_width, int device_height, double scale,
                                          ambient::Detail detail) const {
    double per = std::max(1.0, scale) * 2;
    if (detail == ambient::Detail::fine)
        per = std::max(1.0, scale);
    if (detail == ambient::Detail::light)
        per = std::max(1.0, scale) * 3;
    double width = device_width / per;
    double height = device_height / per;
    const double budget = 680000.0;
    if (width * height > budget) {
        const double shrink = std::sqrt(budget / (width * height));
        width *= shrink;
        height *= shrink;
    }
    return {std::max(32, static_cast<int>(width)), std::max(24, static_cast<int>(height))};
}

void FallsScenery::resize(ambient::SceneSize size, bool settled) {
    wanted_ = size;
    settled_ = settled;
    // The first picture is baked at once; later sizes wait for the resize to settle.
    if (!baking_.valid() && (settled || !ready()))
        start_bake();
}

void FallsScenery::set_detail(ambient::Detail detail) {
    detail_ = detail;
    (*falls_).set_density(density_for(detail));
}

void FallsScenery::start_bake() {
    if (wanted_.width <= 0 || wanted_.height <= 0)
        return;
    if (wanted_.width == shown_size_.width && wanted_.height == shown_size_.height && ready())
        return;
    baking_size_ = wanted_;
    // Edges smoothed with four more rays at Balanced and Fine; one ray at Light.
    (*falls_).begin_size(wanted_.width, wanted_.height, detail_ == ambient::Detail::light ? 1 : 2);
    cancel_.store(false);
    baking_ = std::async(std::launch::async, &bake, falls_.get(), &cancel_);
}

bool FallsScenery::poll(ambient::SceneContext& context) {
    if (baking_.valid() && baking_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        baking_.get();
        (*falls_).adopt();
        shown_size_ = baking_size_;
        context.redraw = true;
        // The window may have changed size again while that one was baking.
        if (settled_ && (wanted_.width != shown_size_.width || wanted_.height != shown_size_.height))
            start_bake();
    }
    return baking_.valid();
}

void FallsScenery::advance(double seconds, ambient::SceneContext& context) {
    // With reduced motion the falls still fall, but gently: half speed.
    (*falls_).advance(context.reduced ? seconds * .5 : seconds);
}

const std::vector<std::uint32_t>& FallsScenery::draw(ambient::SceneContext&, int& width, int& height) {
    (*falls_).draw(picture_);
    (*falls_).size(width, height);
    return picture_;
}

bool FallsScenery::scripted_action(std::string_view action, ambient::SceneContext&) {
    // Development scripts: "particles" prints how many the falls are drawing.
    if (action == "particles") {
        std::fprintf(stderr, "canopyfalls: %zu particles, %.1f s of water\n", (*falls_).particle_count(), (*falls_).time());
        return true;
    }
    return false;
}

void FallsScenery::sound(bool running, ambient::SceneContext& context) {
    if (!running || !ready()) {
        context.sound.quiet_beds();
        return;
    }
#ifdef GUI_FORMS_AUDIO_GENERATOR
    // The falls, the pool and the forest, under Sound; the harp, under Music.
    if (!forest_)
        forest_ = std::make_shared<LiveForest>();
    context.sound.live(forest_, 1.0);
    if (!music_)
        music_ = std::make_shared<games::LiveComposer>(
            games::music::make_score("heavenly_harp", static_cast<std::uint32_t>(std::random_device{}())),
            games::music::score_level("heavenly_harp"));
    context.sound.live_music(music_, .6);
#endif
}

FallsView::FallsView(ambient::gf::StableId id, ambient::ViewOptions options)
    : SceneView(std::move(id), host_setup(), std::make_unique<FallsScenery>(), options) {}

} // namespace cf
