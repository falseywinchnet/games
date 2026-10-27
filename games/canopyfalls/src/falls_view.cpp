#include "falls_view.hpp"

#include "falls_voice.hpp"
#include "live_composer.hpp"
#include "repertoire.hpp"

#include "gui_forms/threading.hpp"

#include <algorithm>
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

// A batch of jobs shared by the baking thread and GUI.Forms Workers helping it, all
// taking the next job from one count until there are none left, or the bake is
// cancelled. (AtomicThreadPool runs a batch this small on the caller alone: it wants
// over 64 tasks per thread; see docs/TOOLKIT_REQUESTS.md. The card table's crew,
// shared/cards/table_art.cpp, borrowed.)
struct Round {
    void (*job)(void*, int) noexcept;
    void* context;
    int count;
    const gui_forms::CancellationFlag* cancel;
    std::atomic<int> next{0};
};
void take_jobs(Round& round) noexcept {
    for (int i = round.next.fetch_add(1); i < round.count; i = round.next.fetch_add(1)) {
        if ((*round.cancel).requested())
            return;
        round.job(round.context, i);
    }
}
void crew_member(const gui_forms::CancellationFlag&, void* context) {
    take_jobs(*static_cast<Round*>(context));
}
struct Crew {
    const gui_forms::CancellationFlag* cancel;
    int threads;
};
void crew_run(void* runner, int count, void (*job)(void*, int) noexcept, void* context) {
    const Crew& crew = *static_cast<const Crew*>(runner);
    Round round{job, context, count, crew.cancel};
    std::vector<std::unique_ptr<gui_forms::Worker>> helpers;
    const int wanted = std::min(crew.threads, count) - 1;
    try {
        for (int i = 0; i < wanted; ++i)
            helpers.push_back(std::make_unique<gui_forms::Worker>(&crew_member, &round));
    } catch (...) {
        // fewer helpers than hoped: the caller and those started do the work
    }
    take_jobs(round);
    for (std::unique_ptr<gui_forms::Worker>& helper : helpers)
        (*helper).join();
}

// One band of the pending size; a failure (memory) is reported and the size dropped.
void bake_band(void* context, int band) noexcept {
    FallsScenery::Baking& baking = *static_cast<FallsScenery::Baking*>(context);
    try {
        (*baking.falls).bake_band(band);
    } catch (...) {
        baking.failed.store(true);
    }
}

// The baking thread: the one-time preparation, then every band of the pending size
// over up to eight threads, then the size finished. Cancellation stops it between
// jobs (the scenery is going away).
void bake(const gui_forms::CancellationFlag& cancel, void* context) {
    FallsScenery::Baking& baking = *static_cast<FallsScenery::Baking*>(context);
    try {
        const int threads = static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1U, 8U));
        Crew crew{&cancel, threads};
        LiveFalls& falls = *baking.falls;
        falls.prepare_once(&crew_run, &crew);
        crew_run(&crew, falls.bands(), &bake_band, &baking);
        if (!cancel.requested() && !baking.failed.load())
            falls.finish_size();
    } catch (...) {
        baking.failed.store(true);
    }
    baking.done.store(true);
}

} // namespace

FallsScenery::FallsScenery() {
    Look look{};
    look.sky.sun_elevation = 46 * pi / 180;
    look.sky.haze = 2.0;
    look.sky.haze_forward = .76;
    falls_ = std::make_unique<LiveFalls>(look, Camera{});
    (*falls_).set_density(density_for(detail_));
    baking_.falls = falls_.get();
}

FallsScenery::~FallsScenery() {
    stop_bake();
}

void FallsScenery::stop_bake() noexcept {
    if (!baker_)
        return;
    (*baker_).request_cancel();
    try {
        (*baker_).join();
    } catch (...) {
    }
    baker_.reset();
}

bool FallsScenery::ready() const {
    return (*falls_).ready();
}

// A scene pixel for every point at Balanced, for every device pixel at Fine, one per two
// points at Light; never more than a million pixels at Balanced (1.6 million at Fine,
// half a million at Light), so a full-screen window costs no more than a large one.
ambient::SceneSize FallsScenery::size_for(int device_width, int device_height, double scale,
                                          ambient::Detail detail) const {
    double per = std::max(1.0, scale);
    double budget = 1000000.0;
    if (detail == ambient::Detail::fine) {
        per = 1;
        budget = 1600000.0;
    }
    if (detail == ambient::Detail::light) {
        per = std::max(1.0, scale) * 2;
        budget = 500000.0;
    }
    double width = device_width / per;
    double height = device_height / per;
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
    if (!baker_ && (settled || !ready()))
        start_bake();
}

void FallsScenery::set_detail(ambient::Detail detail) {
    detail_ = detail;
    (*falls_).set_density(density_for(detail));
}

void FallsScenery::start_bake() {
    if (wanted_.width <= 0 || wanted_.height <= 0)
        return;
    if (wanted_.width == shown_size_.width && wanted_.height == shown_size_.height && ready() && !quick_shown_)
        return;
    baking_size_ = wanted_;
    // Edges smoothed with four more rays at Balanced and Fine; one ray at Light. With
    // nothing shown yet, a third of the size with one ray comes first, in a moment.
    int samples = detail_ == ambient::Detail::light ? 1 : 2;
    const bool quick = !ready() && wanted_.width * wanted_.height > 120000;
    if (quick) {
        baking_size_ = {std::max(32, wanted_.width / 3), std::max(24, wanted_.height / 3)};
        samples = 1;
    }
    (*falls_).begin_size(baking_size_.width, baking_size_.height, samples);
    baking_.done.store(false);
    baking_.failed.store(false);
    try {
        baker_ = std::make_unique<gui_forms::Worker>(&bake, &baking_);
        quick_ = quick;
    } catch (...) {
        baker_.reset();
    }
}

bool FallsScenery::poll(ambient::SceneContext& context) {
    if (baker_ && baking_.done.load()) {
        try {
            (*baker_).join();
        } catch (...) {
            baking_.failed.store(true);
        }
        baker_.reset();
        if (!baking_.failed.load()) {
            (*falls_).adopt();
            shown_size_ = baking_size_;
            quick_shown_ = quick_;
            context.redraw = true;
        }
        // The full picture follows the quick one, and the window may have changed size
        // again while that one was baking.
        if (quick_shown_ || (settled_ && (wanted_.width != shown_size_.width || wanted_.height != shown_size_.height)))
            start_bake();
    }
    return baker_ != nullptr;
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
