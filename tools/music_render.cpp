// Renders one of the suite's live scores to a WAV file, for listening.
//   music_render <style> out.wav [seconds] [seed]      (music_render --list names them)
#include "repertoire.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {
void put_u32(std::FILE* f, std::uint32_t v) { std::fwrite(&v, 4, 1, f); }
void put_u16(std::FILE* f, std::uint16_t v) { std::fwrite(&v, 2, 1, f); }
// 16-bit stereo PCM at the engine's rate.
bool write_wav(const char* path, const std::vector<float>& stereo) {
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    const std::uint32_t bytes = static_cast<std::uint32_t>(stereo.size() * 2);
    std::fwrite("RIFF", 1, 4, f);
    put_u32(f, 36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16);
    put_u16(f, 1);
    put_u16(f, 2);
    put_u32(f, games::music::sample_rate);
    put_u32(f, games::music::sample_rate * 4);
    put_u16(f, 4);
    put_u16(f, 16);
    std::fwrite("data", 1, 4, f);
    put_u32(f, bytes);
    for (const float s : stereo) {
        const std::int16_t v = static_cast<std::int16_t>(std::lround(std::clamp(static_cast<double>(s), -1.0, 1.0) * 32767));
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--list") {
        for (const std::string_view name : games::music::score_names()) std::printf("%.*s\n", static_cast<int>(name.size()), name.data());
        return 0;
    }
    if (argc < 3) {
        std::fprintf(stderr, "usage: music_render <style> out.wav [seconds] [seed]   (--list for the styles)\n");
        return 1;
    }
    const double seconds = argc > 3 ? std::atof(argv[3]) : 180;
    const std::uint32_t seed = argc > 4 ? static_cast<std::uint32_t>(std::strtoul(argv[4], nullptr, 10)) : 7U;
    const std::unique_ptr<games::music::Composer> score = games::music::make_score(argv[1], seed);
    if (!score) {
        std::fprintf(stderr, "no style called %s\n", argv[1]);
        return 1;
    }
    // at the level its game plays it
    const double level = games::music::score_level(argv[1]);
    const long frames = static_cast<long>(seconds * games::music::sample_rate);
    std::vector<float> all(static_cast<std::size_t>(frames) * 2, 0.0F);
    for (long at = 0; at < frames; at += 1024) {
        const long n = std::min<long>(1024, frames - at);
        (*score).render_add(std::span<float>(all.data() + at * 2, static_cast<std::size_t>(n) * 2), level);
    }
    double sum = 0, peak = 0;
    for (const float s : all) {
        sum += static_cast<double>(s) * s;
        peak = std::max(peak, static_cast<double>(std::fabs(s)));
    }
    std::printf("%s: %.0f s, rms %.1f dB, peak %.1f dB\n", argv[1], seconds, 10 * std::log10(sum / static_cast<double>(all.size()) + 1e-12),
                20 * std::log10(peak + 1e-12));
    return write_wav(argv[2], all) ? 0 : 1;
}
