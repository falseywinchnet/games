#include "cube_score.hpp"

#include "glass_effects.hpp"
#include "gui_forms/audio/audio.hpp"
#include "live_composer.hpp"
#include "repertoire.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>

namespace ps_cube {
namespace {

#ifdef GUI_FORMS_AUDIO_GENERATOR
class LiveEffects final : public gui_forms::AudioGenerator {
  public:
    LiveEffects(std::uint32_t seed, std::shared_ptr<Harmony> harmony)
        : harmony_(std::move(harmony)), effects_(seed, harmony_.get()) {}
    void post(Cue cue) noexcept {
        cues_.push(cue);
    }
    void render(std::span<float> stereo) noexcept override {
        std::fill(stereo.begin(), stereo.end(), 0.0F);
        Cue cue = Cue::none;
        while (cues_.pop(cue))
            effects_.cue(cue);
        effects_.render_add(stereo, 1.0);
    }

  private:
    std::shared_ptr<Harmony> harmony_;
    GlassEffects effects_;
    CueRing cues_{};
};

class Score final : public games::LiveScore {
  public:
    Score() {
        // A new piece each time the game is opened; Dice makes it from this seed alike on
        // every platform.
        const std::uint32_t seed = static_cast<std::uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count() * 2654435761ULL >> 7);
        harmony_ = std::make_shared<Harmony>();
        // the suite's glasshouse (src/music/styles/glasshouse.hpp), publishing its harmony for
        // the effects to tune to
        music_ = std::make_shared<games::LiveComposer>(std::make_unique<games::music::Glasshouse>(seed, harmony_.get()),
                                                       games::music::score_level("glasshouse"), 2.5);
        effects_ = std::make_shared<LiveEffects>(seed ^ 0x5bd1e995U, harmony_);
    }
    std::shared_ptr<gui_forms::AudioGenerator> music() override {
        (*music_).restart();
        return music_;
    }
    std::shared_ptr<gui_forms::AudioGenerator> effects() override {
        return effects_;
    }
    bool cue(std::string_view name, bool sound) override {
        const Cue cue = cue_for(name);
        if (cue == Cue::none)
            return false;
        // The music hears joins, levels and wins even when effects are off; the effects
        // play only with Sound.
        if (cue == Cue::connect || cue == Cue::level || cue == Cue::win)
            (*music_).cue(music_cue(cue));
        if (sound)
            (*effects_).post(cue);
        return true;
    }

  private:
    std::shared_ptr<Harmony> harmony_;  // the music writes it, the effects read it
    std::shared_ptr<games::LiveComposer> music_;
    std::shared_ptr<LiveEffects> effects_;
};
#endif

} // namespace

std::shared_ptr<games::LiveScore> make_score() {
#ifdef GUI_FORMS_AUDIO_GENERATOR
    return std::make_shared<Score>();
#else
    return nullptr;
#endif
}

} // namespace ps_cube
