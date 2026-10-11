#include "sudoku_score.hpp"

#include "sudoku_music.hpp"
#include "gui_forms/audio/audio.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>

namespace ps_sudoku {
namespace {

#ifdef GUI_FORMS_AUDIO_GENERATOR
class LiveGarden final : public gui_forms::AudioGenerator {
  public:
    LiveGarden(std::uint32_t seed, bool night) : music_(seed, night) {}
    void restart() noexcept {
        restart_.store(true, std::memory_order_release);
    }
    void render(std::span<float> stereo) noexcept override {
        std::fill(stereo.begin(), stereo.end(), 0.0F);
        if (restart_.exchange(false, std::memory_order_acq_rel))
            music_.fade_in(2.5);
        music_.render_add(stereo, 1.0);
    }

  private:
    GardenMusic music_;
    std::atomic<bool> restart_{false};
};

// The effects slot is the score's to fill; Sudoku's effects are recorded, so it is quiet.
class Quiet final : public gui_forms::AudioGenerator {
  public:
    void render(std::span<float> stereo) noexcept override {
        std::fill(stereo.begin(), stereo.end(), 0.0F);
    }
};

class Score final : public games::LiveScore {
  public:
    explicit Score(bool night) {
        const std::uint32_t seed = static_cast<std::uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count() * 2654435761ULL >> 7);
        music_ = std::make_shared<LiveGarden>(seed ^ (night ? 0x9E3779B9U : 0U), night);
        quiet_ = std::make_shared<Quiet>();
    }
    std::shared_ptr<gui_forms::AudioGenerator> music() override {
        (*music_).restart();
        return music_;
    }
    std::shared_ptr<gui_forms::AudioGenerator> effects() override {
        return quiet_;
    }
    bool cue(std::string_view, bool) override {
        return false;  // every effect plays from its recording
    }

  private:
    std::shared_ptr<LiveGarden> music_;
    std::shared_ptr<Quiet> quiet_;
};
#endif

}  // namespace

std::shared_ptr<games::LiveScore> make_score(bool night) {
#ifdef GUI_FORMS_AUDIO_GENERATOR
    return std::make_shared<Score>(night);
#else
    static_cast<void>(night);
    return nullptr;
#endif
}

}  // namespace ps_sudoku
