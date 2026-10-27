#pragma once
// A live score (repertoire.hpp) as the toolkit's audio source: the device's callback
// asks it for samples as it plays. The window thread may ask it to come up from silence
// again (restart), pass it a game's moment to answer (cue), or hurry it; those reach the
// audio thread through atomics, never a lock.
#include "music_core.hpp"

#include "gui_forms/audio/audio.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>

namespace games {

#ifdef GUI_FORMS_AUDIO_GENERATOR
class LiveComposer final : public gui_forms::AudioGenerator {
  public:
    // `fade_seconds`: how long it takes to come up from silence when it is heard again.
    LiveComposer(std::unique_ptr<music::Composer> score, double gain, double fade_seconds = 2.0)
        : score_(std::move(score)), gain_(gain), fade_seconds_(fade_seconds) {}
    // From the window thread: come up from silence again.
    void restart() noexcept { restart_.store(true, std::memory_order_release); }
    // From the window thread: a game's moment for the music to answer.
    void cue(int cue) noexcept {
        const unsigned head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) >= cues_.size()) return;  // full: the moment passes
        cues_[head % cues_.size()].store(cue, std::memory_order_relaxed);
        head_.store(head + 1, std::memory_order_release);
    }
    // From the window thread: play this much faster (1: as written).
    void set_hurry(double factor) noexcept { hurry_.store(factor, std::memory_order_relaxed); }

    void render(std::span<float> stereo) noexcept override {
        std::fill(stereo.begin(), stereo.end(), 0.0F);
        if (!score_) return;
        if (restart_.exchange(false, std::memory_order_acq_rel)) (*score_).fade_in(fade_seconds_);
        for (unsigned tail = tail_.load(std::memory_order_relaxed); tail != head_.load(std::memory_order_acquire); ++tail) {
            (*score_).cue(cues_[tail % cues_.size()].load(std::memory_order_relaxed));
            tail_.store(tail + 1, std::memory_order_release);
        }
        (*score_).set_hurry(hurry_.load(std::memory_order_relaxed));
        (*score_).render_add(stereo, gain_);
    }

  private:
    std::unique_ptr<music::Composer> score_;
    double gain_;
    double fade_seconds_;
    std::atomic<bool> restart_{false};
    std::atomic<double> hurry_{1.0};
    std::array<std::atomic<int>, 32> cues_{};
    std::atomic<unsigned> head_{0}, tail_{0};
};
#endif

}  // namespace games
