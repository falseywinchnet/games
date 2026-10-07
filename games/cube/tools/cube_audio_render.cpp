// Nature Cube's live audio, rendered offline as the game plays it.
//   cube_audio_render music out.wav [seconds [seed [part]]]   part: pads bass lead bells heartbeat birds breeze
//   cube_audio_render session out.wav [seconds [seed [part]]]  music and effects under scripted play
//                                                              (with a part, that part of the music alone)
//   cube_audio_render effects out.wav                          every effect, one after another
//   cube_audio_render bench [seconds]                          CPU time per second of audio
#include "glass_effects.hpp"
#include "glass_music.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using ps_cube::Cue;
using ps_cube::sample_rate;

bool write_wav(const std::string& path, const std::vector<float>& stereo) {
    std::ofstream out(path, std::ios::binary);
    const std::uint32_t data = static_cast<std::uint32_t>(stereo.size() * 2);
    const std::uint32_t riff = 36 + data, fmt = 16, rate = sample_rate, bytes = sample_rate * 4;
    const std::uint16_t pcm = 1, channels = 2, align = 4, bits = 16;
    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&riff), 4);
    out.write("WAVEfmt ", 8);
    out.write(reinterpret_cast<const char*>(&fmt), 4);
    out.write(reinterpret_cast<const char*>(&pcm), 2);
    out.write(reinterpret_cast<const char*>(&channels), 2);
    out.write(reinterpret_cast<const char*>(&rate), 4);
    out.write(reinterpret_cast<const char*>(&bytes), 4);
    out.write(reinterpret_cast<const char*>(&align), 2);
    out.write(reinterpret_cast<const char*>(&bits), 2);
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&data), 4);
    for (float v : stereo) {
        const double clipped = std::fmax(-1.0, std::fmin(1.0, static_cast<double>(v)));
        const std::int16_t s = static_cast<std::int16_t>(std::lround(clipped * 32767));
        out.write(reinterpret_cast<const char*>(&s), 2);
    }
    return static_cast<bool>(out);
}

void report(const std::vector<float>& stereo) {
    double sum = 0, peak = 0;
    for (float v : stereo) {
        sum += static_cast<double>(v) * v;
        peak = std::fmax(peak, std::fabs(static_cast<double>(v)));
    }
    const double rms = std::sqrt(sum / std::fmax(1.0, static_cast<double>(stereo.size())));
    std::printf("rms %.1f dBFS, peak %.1f dBFS\n", 20 * std::log10(rms + 1e-12), 20 * std::log10(peak + 1e-12));
}

int part_of(const std::string& name) {
    const char* names[] = {"pads", "bass", "lead", "bells", "heartbeat", "birds", "breeze"};
    for (int k = 0; k < 7; ++k)
        if (name == names[k])
            return k;
    return -1;
}

struct Scripted {
    double at;
    Cue cue;
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: cube_audio_render music|session|effects|bench ...\n";
        return 2;
    }
    const std::string mode = argv[1];
    const int block = 512;
    if (mode == "music" || mode == "session") {
        if (argc < 3)
            return 2;
        const double seconds = argc > 3 ? std::atof(argv[3]) : 75;
        const std::uint32_t seed = argc > 4 ? static_cast<std::uint32_t>(std::strtoul(argv[4], nullptr, 10)) : 1;
        ps_cube::Harmony harmony;
        ps_cube::GlassMusic music(seed, &harmony);
        ps_cube::GlassEffects effects(seed + 7, &harmony);
        const bool solo = argc > 5;
        if (solo)
            music.solo(part_of(argv[5]));
        // A player at work: picking, drawing, joining, a wrong turn, and a win near the end.
        std::vector<Scripted> script;
        if (mode == "session") {
            double t = 6;
            int pair = 0;
            while (t < seconds - 16) {
                script.push_back({t, Cue::pick});
                const int cells = 4 + (pair * 3) % 6;
                for (int c = 0; c < cells; ++c) {
                    t += .22 + .05 * (c % 3);
                    script.push_back({t, Cue::step});
                }
                if (pair % 3 == 1) {
                    t += .3;
                    script.push_back({t, Cue::blocked});
                    t += .4;
                    script.push_back({t, Cue::erase});
                    t += .25;
                    script.push_back({t, Cue::erase});
                    t += .3;
                    script.push_back({t, Cue::step});
                }
                t += .25;
                script.push_back({t, Cue::connect});
                for (int k = 0; k < 12; ++k)
                    script.push_back({t + 2 + k * .05, Cue::turn});
                t += 7 + (pair % 4) * 2.5;
                ++pair;
            }
            script.push_back({seconds - 14, Cue::win});
            script.push_back({seconds - 6, Cue::level});
        }
        std::vector<float> out(static_cast<std::size_t>(seconds * sample_rate) * 2, 0.0F);
        std::size_t next = 0;
        for (std::size_t frame = 0; frame * 2 < out.size(); frame += block) {
            const double now = static_cast<double>(frame) / sample_rate;
            while (next < script.size() && script[next].at <= now) {
                music.cue(script[next].cue);
                effects.cue(script[next].cue);
                ++next;
            }
            const std::size_t n = std::min<std::size_t>(block, out.size() / 2 - frame);
            std::span<float> span(out.data() + frame * 2, n * 2);
            music.render_add(span, 1.0);
            if (!solo)
                effects.render_add(span, 1.0);
        }
        report(out);
        std::printf("sections %d, last key %d mode %d tempo %.1f\n", music.sections_played(), music.tonic(),
                    music.mode(), music.tempo());
        return write_wav(argv[2], out) ? 0 : 1;
    }
    if (mode == "effects") {
        if (argc < 3)
            return 2;
        ps_cube::Harmony harmony;
        ps_cube::GlassMusic music(3, &harmony);  // only to set a key
        std::vector<float> scratch(2 * block, 0.0F);
        music.render_add(scratch, 1.0);
        ps_cube::GlassEffects effects(5, &harmony);
        const char* names[] = {"pick", "step x6", "erase x3", "connect", "blocked x2", "turn", "level", "win"};
        std::vector<Scripted> script;
        double t = .3;
        script.push_back({t, Cue::pick});
        t += 1.2;
        std::printf("%5.2f s  %s\n", .3, names[0]);
        std::printf("%5.2f s  %s\n", t, names[1]);
        for (int k = 0; k < 6; ++k)
            script.push_back({t + k * .24, Cue::step});
        t += 2.2;
        std::printf("%5.2f s  %s\n", t, names[2]);
        for (int k = 0; k < 3; ++k)
            script.push_back({t + k * .26, Cue::erase});
        t += 1.8;
        std::printf("%5.2f s  %s\n", t, names[3]);
        script.push_back({t, Cue::connect});
        t += 2.8;
        std::printf("%5.2f s  %s\n", t, names[4]);
        script.push_back({t, Cue::blocked});
        script.push_back({t + .5, Cue::blocked});
        t += 1.6;
        std::printf("%5.2f s  %s\n", t, names[5]);
        for (int k = 0; k < 20; ++k)
            script.push_back({t + k * .033, Cue::turn});
        t += 2.0;
        std::printf("%5.2f s  %s\n", t, names[6]);
        script.push_back({t, Cue::level});
        t += 3.0;
        std::printf("%5.2f s  %s\n", t, names[7]);
        script.push_back({t, Cue::win});
        t += 5.5;
        std::vector<float> out(static_cast<std::size_t>(t * sample_rate) * 2, 0.0F);
        std::size_t next = 0;
        for (std::size_t frame = 0; frame * 2 < out.size(); frame += 64) {
            const double now = static_cast<double>(frame) / sample_rate;
            while (next < script.size() && script[next].at <= now)
                effects.cue(script[next++].cue);
            const std::size_t n = std::min<std::size_t>(64, out.size() / 2 - frame);
            effects.render_add(std::span<float>(out.data() + frame * 2, n * 2), 1.0);
        }
        report(out);
        return write_wav(argv[2], out) ? 0 : 1;
    }
    if (mode == "bench") {
        const double seconds = argc > 2 ? std::atof(argv[2]) : 600;
        ps_cube::Harmony harmony;
        ps_cube::GlassMusic music(11, &harmony);
        ps_cube::GlassEffects effects(12, &harmony);
        std::vector<float> buffer(2 * block, 0.0F);
        const std::size_t blocks = static_cast<std::size_t>(seconds * sample_rate / block);
        double music_time = 0, effects_time = 0, effects_busy_time = 0;
        for (std::size_t b = 0; b < blocks; ++b) {
            std::fill(buffer.begin(), buffer.end(), 0.0F);
            if (b % 400 == 0)
                music.cue(Cue::connect);
            const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
            music.render_add(buffer, 1.0);
            const std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
            music_time += std::chrono::duration<double>(t1 - t0).count();
        }
        // effects while busy: a step every quarter second, a join every few seconds
        for (std::size_t b = 0; b < blocks; ++b) {
            std::fill(buffer.begin(), buffer.end(), 0.0F);
            if (b % 23 == 0)
                effects.cue(Cue::step);
            if (b % 300 == 0)
                effects.cue(Cue::connect);
            const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
            effects.render_add(buffer, 1.0);
            effects_busy_time += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        }
        ps_cube::GlassEffects idle(13, &harmony);
        for (std::size_t b = 0; b < blocks; ++b) {
            const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
            idle.render_add(buffer, 1.0);
            effects_time += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        }
        std::printf("music: %.3f%% of one core (%.0fx real time)\n", 100 * music_time / seconds, seconds / music_time);
        std::printf("effects, busy: %.3f%% of one core\n", 100 * effects_busy_time / seconds);
        std::printf("effects, idle: %.4f%% of one core\n", 100 * effects_time / seconds);
        return 0;
    }
    return 2;
}
