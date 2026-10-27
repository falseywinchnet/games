#include "sudoku_score.hpp"

#include "gui_forms/audio/audio.hpp"
#include "live_composer.hpp"
#include "repertoire.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>

namespace ps_sudoku {
namespace {

#ifdef GUI_FORMS_AUDIO_GENERATOR
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
        // a koto in the garden (src/music/styles/koto_garden.hpp), tuned for the time of day
        const char* style = night ? "koto_garden_night" : "koto_garden";
        music_ = std::make_shared<games::LiveComposer>(games::music::make_score(style, seed ^ (night ? 0x9E3779B9U : 0U)),
                                                       games::music::score_level(style), 2.5);
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
    std::shared_ptr<games::LiveComposer> music_;
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
