// The hosted-view contract (new-games/kit/view_contract_test.hpp) run against
// Mowing's real control. Built with the PlaySuite application.
//
// The mower works whenever the garden is visible, so the scene never settles
// untouched (see HANDOFF.md). It must still stop completely when hidden.
#include "mowing_view.hpp"
#include "mower_voice.hpp"
#include "pcm_player.hpp"
#include "view_contract_test.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <span>
#include <vector>

namespace {

#ifdef GUI_FORMS_AUDIO_GENERATOR
// The mower's voice as the game plays it, through the suite's own mixer, offline.
class Voice final : public gui_forms::AudioGenerator {
  public:
    void render(std::span<float> stereo) noexcept override {
        mm::MowerControls controls{};
        controls.ignition = true;
        controls.governed_rpm = 3600;
        controls.blades = frames_ > 48000;
        controls.load = 0.5;
        frames_ += stereo.size() / 2;
        voice_.control(controls);
        voice_.render(stereo);
    }
    std::uint64_t frames_{};

  private:
    mm::MowerVoice voice_{1};
};

double level(const std::vector<float>& samples) {
    double sum = 0;
    for (const float sample : samples)
        sum += static_cast<double>(sample) * sample;
    return std::sqrt(sum / static_cast<double>(samples.size()));
}

// 0 when a live source plays through a player slot, follows its gain, and freezes while paused.
int live_voice_plays() {
    games::PcmPlayer player(true);
    const std::shared_ptr<Voice> voice = std::make_shared<Voice>();
    player.generate(8, voice, 1.0);
    std::vector<float> samples(48000 * 2);
    for (int second = 0; second < 4; ++second) {
        if (player.render(samples) != gui_forms::AudioStatus::ok)
            return 1;
    }
    const double loud = level(samples);
    if (!(loud > 0.05) || (*voice).frames_ != 4U * 48000U)
        return 2;
    player.gain(8, 0.5);
    static_cast<void>(player.render(samples));
    const double half = level(samples);
    if (!(half > loud * 0.3 && half < loud * 0.75))
        return 3;
    player.pause(8);
    static_cast<void>(player.render(samples));
    const std::uint64_t frozen = (*voice).frames_;
    static_cast<void>(player.render(samples));
    if (level(samples) > 1e-6 || (*voice).frames_ != frozen)
        return 4;
    player.resume(8);
    static_cast<void>(player.render(samples));
    if (!(level(samples) > loud * 0.3))
        return 5;
    return 0;
}
#endif

} // namespace

int main(int argc, char** argv) {
    kit::ContractOptions options;
    options.settles_when_untouched = false;
    if (argc > 1) {
        options.preview_directory = argv[1];
    }
    const ambient::ViewOptions view_options{.hosted = true, .dev = true};
    const int result = kit::run_view_contract<mm::MowingView, ambient::ViewOptions>("mowingman", view_options, options);
#ifdef GUI_FORMS_AUDIO_GENERATOR
    const int live = live_voice_plays();
    if (live != 0) {
        std::fprintf(stderr, "FAILED: the live mower voice through the mixer (step %d)\n", live);
        return 1;
    }
    std::printf("mowing: the live mower voice plays through the mixer\n");
#endif
    return result;
}
