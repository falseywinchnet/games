// Canopy Falls' music rendered to 48 kHz 16-bit WAV files to be listened to: every voice
// together, then each alone (the same music, so each can be judged against the others).
//
//   falls_music out_dir [seconds [seed]]
#include "falls_music.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr int rate = cf::FallsMusic::sample_rate;

bool write_wav(const std::string& path, const std::vector<float>& all) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr)
        return false;
    const std::uint32_t data = static_cast<std::uint32_t>(all.size() * 2);
    const std::uint32_t header[] = {0x46464952U, 36 + data, 0x45564157U, 0x20746d66U, 16, 0x00020001U,
                                    static_cast<std::uint32_t>(rate), static_cast<std::uint32_t>(rate * 4),
                                    0x00100004U, 0x61746164U, data};
    std::fwrite(header, 4, 11, file);
    for (const float s : all) {
        const std::int16_t v = static_cast<std::int16_t>(std::lround(std::clamp(s, -1.0F, 1.0F) * 32767));
        std::fwrite(&v, 2, 1, file);
    }
    std::fclose(file);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s out_dir [seconds [seed]]\n", argv[0]);
        return 2;
    }
    const std::string directory = argv[1];
    const double seconds = argc > 2 ? std::atof(argv[2]) : 60;
    const std::uint32_t seed = argc > 3 ? static_cast<std::uint32_t>(std::atoi(argv[3])) : 1;
    if (seconds <= 0 || seconds > 600)
        return 2;
    const char* names[] = {"music-harp", "music-celesta", "music-chimes", "music-pad"};
    for (int voice = -1; voice < cf::FallsMusic::voices; ++voice) {
        cf::FallsMusic music(seed);
        music.solo(voice);
        std::vector<float> all(static_cast<std::size_t>(seconds * rate) * 2, 0.0F);
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        for (std::size_t at = 0; at < all.size(); at += 1024)
            music.render_add(std::span<float>(all.data() + at, std::min<std::size_t>(1024, all.size() - at)), 1.0);
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        double peak = 0, power = 0;
        for (const float v : all) {
            peak = std::max(peak, static_cast<double>(std::fabs(v)));
            power += static_cast<double>(v) * v;
        }
        const std::string name = voice < 0 ? "music-all" : names[voice];
        if (!write_wav(directory + "/" + name + ".wav", all))
            return 1;
        std::printf("%-14s peak %5.1f dBFS  rms %5.1f dBFS  %ld phrases  %.0fx real time\n", name.c_str(),
                    20 * std::log10(std::max(peak, 1e-9)),
                    20 * std::log10(std::max(std::sqrt(power / static_cast<double>(all.size())), 1e-9)), music.phrases(),
                    seconds / took);
    }
    return 0;
}
