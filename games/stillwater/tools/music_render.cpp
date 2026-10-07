// Stillwater's live voices rendered to 48 kHz 16-bit WAV, with the time they took.
//
//   sw_music_render band out.wav [seconds [seed]]   the band, as the game plays it
//   sw_music_render band out.wav seconds seed part  one part alone (see IslandVoice::solo)
//   sw_music_render loop out.wav [seconds [seed]]     a seamless loop of the band (the fallback
//                                                     where the toolkit has no live voices)
//   sw_music_render tank out.wav [seconds [seed [tank]]]  the tank's water at the suite's level
//                                                     (tank 0 planted, 1 reef, 2 river pool)
//   sw_music_render original out.wav [seconds [seed]] the planted tank at the native Stillwater's
//                                                     own digital level, for comparison with it
//   sw_music_render tankloop out.wav [seconds [seed]] a seamless loop of the planted tank (the
//                                                     fallback bed)
//   sw_music_render taps out.wav                       taps alone: left, centre, right, then six
//                                                     at the centre, to hear them vary
//   sw_music_render tap out.wav                        one centred tap (the fallback clip)
//   sw_music_render tapped out.wav [seconds [seed [tank]]]  the tank with a tap every few seconds
#include "island_voice.hpp"
#include "tank_voice.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr int rate = 48000;
// The level the game plays the band at (stillwater_view.cpp), so the loop matches it.
constexpr double band_gain = 0.25;

bool write_wav(const char* path, const std::vector<float>& all) {
    std::FILE* file = std::fopen(path, "wb");
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

// Renders the tank in device-sized blocks; taps are posted as their moments come.
double render_tank(sw::TankVoice& water, std::vector<float>& all, double gain, double tap_every) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const std::size_t block = 256 * 2;
    double next_tap = tap_every > 0 ? 1.5 : 1e30;
    int taps = 0;
    for (std::size_t at = 0; at < all.size(); at += block) {
        const double now = static_cast<double>(at / 2) / rate;
        if (now >= next_tap) {
            // Across the glass and back.
            const double pans[] = {-0.7, 0.0, 0.6, -0.2, 0.9, 0.3, -0.9};
            water.knock(pans[taps % 7]);
            ++taps;
            next_tap += tap_every;
        }
        const std::size_t count = std::min(block, all.size() - at);
        std::span<float> part(all.data() + at, count);
        water.render_add(part, gain);
    }
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return took;
}

void crossfade_loop(std::vector<float>& all, double seconds) {
    const std::size_t frames = all.size() / 2;
    const std::size_t body = static_cast<std::size_t>(seconds * rate);
    const std::size_t tail = frames - body;
    for (std::size_t i = 0; i < tail; ++i) {
        const double t = (static_cast<double>(i) + 0.5) / static_cast<double>(tail);
        const float in = static_cast<float>(std::sin(t * 1.5707963267948966));
        const float out = static_cast<float>(std::cos(t * 1.5707963267948966));
        for (std::size_t c = 0; c < 2; ++c)
            all[i * 2 + c] = all[i * 2 + c] * in + all[(body + i) * 2 + c] * out;
    }
    all.resize(body * 2);
}

void report(const char* what, const std::vector<float>& all, double seconds, double took) {
    float peak = 1e-9F;
    double square = 0;
    for (const float s : all) {
        peak = std::max(peak, std::abs(s));
        square += static_cast<double>(s) * s;
    }
    const double rms = std::sqrt(square / static_cast<double>(std::max<std::size_t>(1, all.size())));
    std::printf("%s: %.1f s, peak %.4f (%.1f dBFS), rms %.5f (%.1f dBFS), rendered in %.3f s (%.0fx real time, "
                "%.3f %% of a core)",
                what, static_cast<double>(all.size() / 2) / rate, static_cast<double>(peak),
                20 * std::log10(static_cast<double>(peak)), rms, 20 * std::log10(std::max(rms, 1e-12)), took,
                seconds / std::max(took, 1e-9), 100 * took / std::max(seconds, 1e-9));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s band|loop|tank|original|tankloop|taps|tap|tapped out.wav [seconds [seed]]\n",
                     argv[0]);
        return 2;
    }
    const std::string mode = argv[1];
    const bool water_mode = mode == "tank" || mode == "original" || mode == "tankloop" || mode == "taps" ||
                            mode == "tap" || mode == "tapped";
    const bool loop = mode == "loop" || mode == "tankloop";
    const double default_seconds = mode == "tap" ? 0.25 : (mode == "taps" ? 6.5 : 60);
    const double seconds = argc > 3 ? std::atof(argv[3]) : default_seconds;
    const std::uint32_t seed = argc > 4 ? static_cast<std::uint32_t>(std::atoi(argv[4])) : 1720;
    // A loop is rendered with a tail that is crossfaded over its own beginning.
    const double fade = loop ? 6.0 : 0.0;
    const std::size_t frames = static_cast<std::size_t>((seconds + fade) * rate);
    std::vector<float> all(frames * 2, 0.0F);
    if (water_mode) {
        const int tank = argc > 5 && (mode == "tank" || mode == "tapped") ? std::atoi(argv[5]) : 0;
        sw::TankVoice water(seed, tank);
        double took = 0;
        if (mode == "taps" || mode == "tap") {
            // The taps alone: the bed is rendered into a scratch buffer and dropped.
            sw::TankVoice quiet(seed, tank);
            std::vector<float> bed(all.size(), 0.0F);
            const std::size_t block = 256 * 2;
            const double times[] = {0.05, 0.85, 1.65, 2.6, 3.2, 3.8, 4.4, 5.0, 5.6};
            const double pans[] = {-0.8, 0.0, 0.8, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
            std::size_t next = 0;
            const std::size_t count = mode == "tap" ? 1 : 9;
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            for (std::size_t at = 0; at < all.size(); at += block) {
                const double now = static_cast<double>(at / 2) / rate;
                const double when = mode == "tap" ? 0.0 : times[next];
                if (next < count && now >= when) {
                    quiet.knock(pans[next]);
                    ++next;
                }
                const std::size_t n = std::min(block, all.size() - at);
                std::span<float> mixed(all.data() + at, n);
                std::span<float> alone(bed.data() + at, n);
                quiet.render_add(alone, sw::TankVoice::suite_level);
            }
            took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            // Subtract the same voice's bed without taps: what remains is the taps.
            sw::TankVoice untouched(seed, tank);
            std::vector<float> plain(all.size(), 0.0F);
            static_cast<void>(render_tank(untouched, plain, sw::TankVoice::suite_level, 0));
            for (std::size_t i = 0; i < all.size(); ++i)
                all[i] = bed[i] - plain[i];
        } else {
            const double gain = mode == "original" ? 1.0 : sw::TankVoice::suite_level;
            took = render_tank(water, all, gain, mode == "tapped" ? 3.7 : 0);
        }
        if (loop)
            crossfade_loop(all, seconds);
        if (!write_wav(argv[2], all)) {
            std::fprintf(stderr, "cannot write %s\n", argv[2]);
            return 1;
        }
        report(argv[1], all, seconds + fade, took);
        std::printf(", %u taps\n", water.knocks_played());
        return 0;
    }
    sw::IslandVoice band(seed);
    // A sixth argument hears one part alone: 0 steel, 1 mallets, 2 ukulele, 3 bass, 4 shaker and bubbles.
    if (argc > 5)
        band.solo(std::atoi(argv[5]));
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    // The device asks for 256 frames at a time; render in the same blocks.
    const std::size_t block = 256 * 2;
    for (std::size_t at = 0; at < all.size(); at += block) {
        const std::size_t count = std::min(block, all.size() - at);
        std::span<float> part(all.data() + at, count);
        band.render_add(part, band_gain);
    }
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (loop)
        crossfade_loop(all, seconds);
    if (!write_wav(argv[2], all)) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    report(argv[1], all, seconds + fade, took);
    std::printf(", %d verses\n", band.verses_played());
    return 0;
}
