#include "cube_score.hpp"

#include "glass_effects.hpp"
#include "glass_music.hpp"
#include "gui_forms/audio/audio.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>

namespace ps_cube {
namespace {

#ifdef GUI_FORMS_AUDIO_GENERATOR
// The music as a live source. The interface thread posts cues; the device callback takes
// them, then renders.
class LiveMusic final : public gui_forms::AudioGenerator {
  public:
    LiveMusic(std::uint32_t seed, std::shared_ptr<Harmony> harmony)
        : harmony_(std::move(harmony)), music_(seed, harmony_.get()) {}
    void post(Cue cue) noexcept {
        cues_.push(cue);
    }
    void restart() noexcept {
        restart_.store(true, std::memory_order_release);
    }
    void render(std::span<float> stereo) noexcept override {
        std::fill(stereo.begin(), stereo.end(), 0.0F);
        if (restart_.exchange(false, std::memory_order_acq_rel))
            music_.fade_in(2.5);
        Cue cue = Cue::none;
        while (cues_.pop(cue))
            music_.cue(cue);
        music_.render_add(stereo, 1.0);
    }

  private:
    std::shared_ptr<Harmony> harmony_;
    GlassMusic music_;
    CueRing cues_{};
    std::atomic<bool> restart_{false};
};

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
        std::shared_ptr<Harmony> harmony = std::make_shared<Harmony>();
        music_ = std::make_shared<LiveMusic>(seed, harmony);
        effects_ = std::make_shared<LiveEffects>(seed ^ 0x5bd1e995U, harmony);
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
            (*music_).post(cue);
        if (sound)
            (*effects_).post(cue);
        return true;
    }

  private:
    std::shared_ptr<LiveMusic> music_;
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
