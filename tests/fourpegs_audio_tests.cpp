#include "fourpegs_audio.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
void settle(games::FourPegsAudio& audio) {
    audio.tick(0);
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (audio.pending() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        audio.tick(0);
    }
    require(!audio.pending(), "Four Pegs asynchronous load completes");
}
void render(games::FourPegsAudio& audio, std::span<float> samples) {
    const gui_forms::AudioStatus result = audio.render(samples);
    require(result == gui_forms::AudioStatus::ok, "Four Pegs actual mixer render");
}
void exact_transition(const std::filesystem::path& root) {
    const gui_forms::AudioClipResult first = gui_forms::AudioClip::load_ogg(root / "audio/fp_music_t1.ogg");
    const gui_forms::AudioClipResult second = gui_forms::AudioClip::load_ogg(root / "audio/fp_music_t2.ogg");
    require(first.clip && second.clip, "reference music decoded");
    games::FourPegsAudio audio(true);
    audio.music("fp_music_t1", false);
    audio.tick(.1);
    require(audio.status() == gui_forms::AudioStatus::closed && !audio.pending(), "disabled music opens nothing");
    audio.music("fp_music_t1", true);
    settle(audio);
    std::array<float, 200> prefix{};
    render(audio, prefix);
    audio.music("fp_music_t2", true);
    settle(audio);
    constexpr std::size_t boundary = 99310;
    constexpr std::size_t fade = 5760;
    constexpr std::size_t count = boundary + fade + 256;
    std::vector<float> samples(count * 2);
    // Deliberately vary engine read sizes. None is related to a musical bar.
    std::size_t position{};
    while (position < count) {
        const std::size_t frames = std::min<std::size_t>(257, count - position);
        render(audio, std::span<float>(samples).subspan(position * 2, frames * 2));
        position += frames;
    }
    const gui_forms::AudioLoopReceipt receipt = audio.music_receipt();
    require(receipt.status == gui_forms::AudioLoopStatus::ok && receipt.phase == gui_forms::AudioLoopPhase::applied &&
            receipt.admission_frame == 100 && receipt.application_frame == boundary,
            "game tier change lands on first authored bar after admission and lead");
    const std::span<const float> old_samples = (*first.clip).samples();
    const std::span<const float> new_samples = (*second.clip).samples();
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t frame = i + 100;
        for (std::size_t channel = 0; channel < 2; ++channel) {
            double expected{};
            if (frame < boundary) { expected = old_samples[frame * 2 + channel]; }
            else {
                expected = new_samples[(frame - boundary) * 2 + channel];
                if (frame < boundary + fade) {
                    expected += static_cast<double>(old_samples[frame * 2 + channel]) *
                        (1.0 - static_cast<double>(frame - boundary) / static_cast<double>(fade - 1));
                }
            }
            expected *= .42;
            require(std::abs(static_cast<double>(samples[i * 2 + channel]) - expected) < .0000002,
                    "every real music sample matches independent bar/fade oracle");
        }
    }
    audio.music("fp_music_t2", false);
    audio.tick(0);
    render(audio, prefix);
    for (float sample : prefix) { require(sample == 0, "local music mute is silent"); }
    audio.music("fp_music_t2", true);
    audio.tick(0);
    render(audio, prefix);
    const std::size_t resumed_frame = 100 + count - boundary;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const double expected = static_cast<double>(new_samples[resumed_frame * 2 + i]) * .42;
        require(std::abs(static_cast<double>(prefix[i]) - expected) < .0000002, "resume preserves exact music position");
    }
    audio.cabinet(true, false, false);
    audio.music("fp_music_t2", true);
    audio.effects("fp_hit", 1, 1, true);
    audio.tick(0);
    render(audio, prefix);
    for (float sample : prefix) { require(sample == 0, "master mute overrides local music and effect requests"); }
    require(!audio.pending(), "master sound mute does not queue an effect load");
    audio.cabinet(true, true, true);
    audio.music("fp_music_t3", true);
    audio.tick(0);
    audio.cabinet(false, true, true);
    audio.music("fp_music_t1", true);
    audio.effects("fp_hit", 1, 1, true);
    audio.tick(.1);
    require(audio.status() == gui_forms::AudioStatus::closed && !audio.pending(), "hidden game cannot revive pending load");
}
void replacement(const std::filesystem::path&) {
    games::FourPegsAudio audio(true);
    audio.music("fp_music_t1", true);
    settle(audio);
    std::array<float, 2> sample{};
    render(audio, sample);
    audio.music("fp_music_t2", true);
    settle(audio);
    render(audio, sample); // Admits tier 2 for a future bar.
    audio.music("fp_music_t3", true);
    settle(audio);
    std::vector<float> through_bar(100000 * 2);
    render(audio, through_bar);
    const gui_forms::AudioLoopReceipt receipt = audio.music_receipt();
    require(receipt.phase == gui_forms::AudioLoopPhase::applied && receipt.application_frame == 99310,
            "new desired tier replaces a previously scheduled tier at the same bar");
    audio.music("", true);
    render(audio, sample);
    require(sample[0] == 0 && sample[1] == 0, "blackout empty track stops music");
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "prepared asset root argument");
#ifdef _WIN32
        const int environment = _putenv_s("GAMES_ASSET_DIR", argv[1]);
#else
        const int environment = setenv("GAMES_ASSET_DIR", argv[1], 1);
#endif
        require(environment == 0, "test asset environment");
        const std::filesystem::path root(argv[1]);
        exact_transition(root);
        replacement(root);
        std::cout << "Four Pegs real-asset bar transition, fade, pause, replacement and teardown pass.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
