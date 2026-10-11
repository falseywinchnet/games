// The koto garden: it plays, stays in range, rests between phrases, and is the same for
// the same seed.
#include "sudoku_music.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
int checks = 0;
void require(bool condition, const char* what) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

struct Heard {
    double peak = 0;
    double rms = 0;
    bool finite = true;
    double quiet_share = 0;  // of 100 ms windows that are near silent
    std::vector<float> samples;
};

Heard listen(std::uint32_t seed, bool night, double seconds) {
    ps_sudoku::GardenMusic music(seed, night);
    Heard heard;
    const int frames = static_cast<int>(seconds * ps_sudoku::GardenMusic::sample_rate);
    std::vector<float> block(2 * 480);
    double sum = 0;
    int quiet = 0;
    for (int done = 0; done < frames; done += 480) {
        std::fill(block.begin(), block.end(), 0.0F);
        music.render_add(block, 1.0);
        double window = 0;
        for (float v : block) {
            heard.finite = heard.finite && std::isfinite(v);
            heard.peak = std::max(heard.peak, static_cast<double>(std::abs(v)));
            sum += static_cast<double>(v) * v;
            window += static_cast<double>(v) * v;
            heard.samples.push_back(v);
        }
        quiet += std::sqrt(window / block.size()) < 1e-3 ? 1 : 0;
    }
    heard.rms = std::sqrt(sum / (2.0 * frames));
    heard.quiet_share = static_cast<double>(quiet) / (frames / 480.0);
    std::printf("%s: %d phrases, %ld notes, %ld breaths; peak %.2f rms %.3f, %.0f%% near silent\n", night ? "night" : "day",
                music.phrases(), music.notes_plucked(), music.breaths(), heard.peak, heard.rms, heard.quiet_share * 100);
    if (seconds >= 60) {
        require(music.phrases() >= 5, "it makes up phrase after phrase");
        require(music.notes_plucked() > 25, "with notes in them");
        require(heard.quiet_share > .15, "and leaves room between them");
    }
    return heard;
}
}  // namespace

int main() {
    const Heard day = listen(7, false, 60);
    const Heard night = listen(7, true, 60);
    for (const Heard* h : {&day, &night}) {
        require(h->finite, "every sample is a number");
        require(h->peak < .95, "it never clips");
        require(h->rms > .01 && h->rms < .2, "quiet music, but music");
    }
    const Heard again = listen(7, false, 20);
    bool same = true;
    for (std::size_t i = 0; i < again.samples.size(); ++i)
        same = same && again.samples[i] == day.samples[i];
    require(same, "the same seed plays the same piece");
    std::printf("sudoku music: %d checks passed\n", checks);
    return 0;
}
