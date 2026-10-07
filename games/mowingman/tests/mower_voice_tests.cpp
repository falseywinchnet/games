// The live mower voice: silent until started, bounded and deterministic, governed
// at idle and at mowing speed, bogging under the blades and in heavy grass, louder
// when it cuts, silent again after the key, and still at its calibrated levels.
#include "mower_voice.hpp"

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

constexpr int rate = mm::MowerVoice::sample_rate;

// Runs the voice in 60 Hz frames, as the game does, and returns what it played.
std::vector<float> run(mm::MowerVoice& voice, const mm::MowerControls& controls, double seconds) {
    std::vector<float> sound;
    std::vector<float> block(static_cast<std::size_t>(rate / 60) * 2);
    for (int frame = 0; frame < static_cast<int>(seconds * 60); ++frame) {
        voice.control(controls);
        voice.render(block);
        sound.insert(sound.end(), block.begin(), block.end());
    }
    return sound;
}

double power(const std::vector<float>& sound) {
    double sum = 0;
    for (const float sample : sound)
        sum += static_cast<double>(sample) * sample;
    return std::sqrt(sum / static_cast<double>(sound.size()));
}

void test_rest() {
    mm::MowerVoice voice(3);
    require(voice.silent(), "a mower that was never started is silent");
    const std::vector<float> sound = run(voice, mm::MowerControls{}, 0.5);
    require(power(sound) == 0, "and plays nothing");
}

void test_day() {
    mm::MowerVoice voice(3);
    mm::MowerControls controls{};
    controls.ignition = true;
    std::vector<float> all = run(voice, controls, 3);
    require(!voice.silent(), "the engine catches");
    require(std::abs(voice.rpm() - 1500) < 90, "it idles near 1500 rpm");
    const double idle = power(run(voice, controls, 1));
    require(idle > 0.02, "the idle is audible");

    controls.governed_rpm = 3600;
    run(voice, controls, 1);
    require(voice.rpm() > 1700 && voice.rpm() < 3300, "the throttle takes its time");
    run(voice, controls, 4);
    const double free_rpm = voice.rpm();
    require(std::abs(free_rpm - 3600) < 120, "it is governed near 3600 rpm");
    const double running = power(run(voice, controls, 1));
    require(running > idle * 1.2, "full speed is louder than idle");

    controls.blades = true;
    run(voice, controls, 0.25);
    require(voice.rpm() < free_rpm - 60, "engaging the blades bogs the engine");
    run(voice, controls, 3);
    const double deck_rpm = voice.rpm();
    require(deck_rpm < free_rpm && deck_rpm > 3200, "the deck costs a little speed");
    require(std::abs(voice.blade_revs_per_second() - deck_rpm / 60 * 1.0125) < 1.5, "the blades follow the belt");
    const double spinning = power(run(voice, controls, 2));

    controls.load = 0.8;
    double lowest = 1e9;
    std::vector<float> cutting;
    for (int slice = 0; slice < 16; ++slice) {
        const std::vector<float> part = run(voice, controls, 0.25);
        cutting.insert(cutting.end(), part.begin(), part.end());
        lowest = std::min(lowest, voice.rpm());
    }
    require(lowest < deck_rpm - 150, "heavy grass pulls the speed down");
    require(lowest > 2400, "but does not stall it");
    require(power(cutting) > spinning * 1.3, "cutting is louder than spinning free");

    controls.load = 0;
    run(voice, controls, 3);
    require(voice.rpm() > deck_rpm - 60, "the governor recovers when the grass clears");

    controls = mm::MowerControls{};
    const std::vector<float> stopping = run(voice, controls, 8);
    require(voice.silent() && voice.rpm() == 0, "the key stops it");
    all.insert(all.end(), cutting.begin(), cutting.end());
    all.insert(all.end(), stopping.begin(), stopping.end());
    bool bounded = true;
    for (const float sample : all)
        bounded = bounded && std::isfinite(sample) && std::abs(sample) <= 0.75f;
    require(bounded, "every sample is finite and within range");

    controls.ignition = true;
    run(voice, controls, 2);
    require(!voice.silent() && voice.rpm() > 1200, "and it starts again");
}

void test_deterministic() {
    mm::MowerControls controls{true, 3600, true, 0.5};
    mm::MowerVoice first(11);
    mm::MowerVoice again(11);
    mm::MowerVoice other(12);
    const std::vector<float> a = run(first, controls, 2);
    const std::vector<float> b = run(again, controls, 2);
    const std::vector<float> c = run(other, controls, 2);
    require(a == b, "one seed, one sound");
    require(a != c, "another seed, another take");
}

} // namespace

int main() {
    test_rest();
    test_day();
    test_deterministic();
    require(mm::MowerVoice::calibration_drift() < 0.06, "the part levels match their calibration");
    std::printf("mower voice: %d checks passed\n", checks);
    return 0;
}
