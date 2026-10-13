// Headless frames for look development: writes PPM images.
//   preview view out.ppm LEVEL [steps] [W H]   first-person, after walking the solution `steps` squares
#include "maze.hpp"
#include "soft3d.hpp"
#include "textures.hpp"
#include "world.hpp"
#include "rewards.hpp"
#include "band.hpp"
#include "cast.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace mz;

static void write_ppm(const char* path, const Soft3D& r) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6 %d %d 255\n", r.W, r.H);
    for (std::uint32_t c : r.color) { const unsigned char q[3] = {static_cast<unsigned char>(c >> 16), static_cast<unsigned char>(c >> 8), static_cast<unsigned char>(c)}; std::fwrite(q, 1, 3, f); }
    std::fclose(f);
}

static void sheet(const char* path, int count, const Tex32& (*get)(int), int tw, int th) {
    const int cols = 8, rows = (count + cols - 1) / cols;
    Soft3D r;
    r.resize(cols * tw, rows * th);
    std::fill(r.color.begin(), r.color.end(), 0xFF6A8AA0u);
    for (int i = 0; i < count; ++i) {
        const Tex32& t = get(i);
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x) {
                const std::uint32_t c = t.at(x * t.w / tw, y * t.h / th);
                if ((c >> 24) < 128) continue;
                r.color[static_cast<size_t>((i / cols * th + y) * r.W + i % cols * tw + x)] = c | 0xFF000000u;
            }
    }
    write_ppm(path, r);
}

// The live band to a 16-bit stereo WAV: `band STYLE SECONDS out.wav [SEED] [dark|back]`.
void write_wav(const char* path, const std::vector<float>& stereo) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    const std::uint32_t frames = static_cast<std::uint32_t>(stereo.size() / 2);
    const std::uint32_t bytes = frames * 4;
    const std::uint32_t riff = 36 + bytes, fmt = 16, rate = MazeBand::sample_rate, byte_rate = rate * 4;
    const std::uint16_t pcm = 1, channels = 2, align = 4, bits = 16;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmt, 4, 1, f); std::fwrite(&pcm, 2, 1, f); std::fwrite(&channels, 2, 1, f); std::fwrite(&rate, 4, 1, f);
    std::fwrite(&byte_rate, 4, 1, f); std::fwrite(&align, 2, 1, f); std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f); std::fwrite(&bytes, 4, 1, f);
    for (float v : stereo) {
        const std::int16_t q = static_cast<std::int16_t>(std::lround(std::clamp(static_cast<double>(v), -1.0, 1.0) * 32767.0));
        std::fwrite(&q, 2, 1, f);
    }
    std::fclose(f);
}

double rms_db(const std::vector<float>& stereo) {
    double sum = 0;
    for (std::size_t i = 0; i + 1 < stereo.size(); i += 2) {
        const double mono = .5 * (static_cast<double>(stereo[i]) + static_cast<double>(stereo[i + 1]));
        sum += mono * mono;
    }
    const double mean = sum / std::max<double>(1.0, static_cast<double>(stereo.size() / 2));
    return 10.0 * std::log10(mean + 1e-20);
}

int band(int argc, char** argv) {
    if (argc >= 2 && !std::strcmp(argv[1], "band-levels")) {
        // each style's first tune: every bus at unity (paste into band.cpp's kRaw), then the
        // master's level and the wall's, with the tables as they are
        static const double kSeconds[3] = {127.3, 123.9, 120.0};
        for (int s = 0; s < 3; ++s) {
            MazeBand raw(1, static_cast<Style>(s));
            raw.measure_raw(true);
            std::vector<float> out(static_cast<std::size_t>(kSeconds[s] * MazeBand::sample_rate) * 2, 0.0F);
            raw.render_add(out, 1.0);
            std::printf("    {");
            for (int b = 0; b < MazeBand::bus_count; ++b) {
                const double p = raw.bus_power(static_cast<MazeBand::Bus>(b));
                std::printf("%s%.1f", b ? ", " : "", p > 1e-12 ? 10.0 * std::log10(p) : -200.0);
            }
            std::printf("},\n");
            MazeBand mixed(1, static_cast<Style>(s));
            std::fill(out.begin(), out.end(), 0.0F);
            mixed.render_add(out, 1.0);
            MazeBand dark(1, static_cast<Style>(s));
            dark.muffle(true);
            std::vector<float> dull(out.size(), 0.0F);
            dark.render_add(dull, 1.0);
            std::printf("    // style %d: mix %.1f dB RMS, through the wall %.1f dB\n", s, rms_db(out), rms_db(dull));
        }
        return 0;
    }
    if (argc < 5) { std::fprintf(stderr, "usage: preview band STYLE SECONDS out.wav [SEED] [dark|back]\n"); return 1; }
    const int style = std::clamp(std::atoi(argv[2]), 0, 2);
    const double secs = std::atof(argv[3]);
    const std::uint32_t seed = argc > 5 ? static_cast<std::uint32_t>(std::strtoul(argv[5], nullptr, 10)) : 1u;
    MazeBand b(seed, static_cast<Style>(style));
    if (argc > 6 && !std::strcmp(argv[6], "dark")) b.muffle(true);
    if (argc > 6 && !std::strcmp(argv[6], "back")) b.backwards(true);
    std::vector<float> out(static_cast<std::size_t>(secs * MazeBand::sample_rate) * 2, 0.0F);
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    const std::size_t block = 512 * 2;
    for (std::size_t at = 0; at < out.size(); at += block) {
        const std::size_t n = std::min(block, out.size() - at);
        b.render_add(std::span<float>(out.data() + at, n), 1.0);
    }
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    write_wav(argv[4], out);
    std::printf("%s: %.0f s of style %d in %.2f s (%.0fx real time), %d bars, %d tunes, %.1f dB RMS\n", argv[4], secs, style, took, secs / took,
                b.bars_played(), b.tunes_played() + 1, rms_db(out));
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 2 && (!std::strcmp(argv[1], "band") || !std::strcmp(argv[1], "band-levels"))) return band(argc, argv);
    if (argc >= 3 && !std::strcmp(argv[1], "rewards")) { sheet(argv[2], reward_count(), &reward_tex, 64, 64); return 0; }
    if (argc >= 3 && !std::strcmp(argv[1], "cast")) { sheet(argv[2], cast_count(), &cast_tex, 128, 128); return 0; }
    if (argc >= 3 && !std::strcmp(argv[1], "posters")) { sheet(argv[2], poster_count(), &tex_poster, 64, 64); return 0; }
    if (argc < 4) { std::fprintf(stderr, "usage: preview view out.ppm LEVEL [yawdeg] [W H] [blackout] [roll]\n"); return 1; }
    const int n = std::atoi(argv[3]);
    const double yawdeg = argc > 4 ? std::atof(argv[4]) : -1;
    const int W = argc > 6 ? std::atoi(argv[5]) : 366, H = argc > 6 ? std::atoi(argv[6]) : 240;
    const auto t0 = std::chrono::steady_clock::now();
    Level lv = generate(params_for(n, 1234 + n));
    // the surfaces (art.hpp), when given: MZ_FLOOR, MZ_WALL, MZ_CEILING
    if (const char* v = std::getenv("MZ_FLOOR")) lv.carpet = std::atoi(v);
    if (const char* v = std::getenv("MZ_WALL")) lv.wall = std::atoi(v);
    if (const char* v = std::getenv("MZ_CEILING")) lv.ceiling = std::atoi(v);
    std::printf("level %d: %dx%d floors %zu doors %d steps %d (gen %.0f ms)\n", n, lv.floors[0].w, lv.floors[0].h, lv.floors.size(), lv.params.doors, lv.optimal_steps,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    Soft3D r;
    r.resize(W, H);
    r.eye = cell_center(lv.start, .5);
    r.yaw = (yawdeg >= 0 ? yawdeg : lv.start_dir * 90.0) * M_PI / 180;
    r.blackout = argc > 7 && std::atoi(argv[7]);
    r.roll = argc > 8 ? std::atof(argv[8]) : 0;
    r.fog_rgb = 0x000000;
    WorldState s;
    s.door_open.assign(8, 0);
    s.door_open[0] = .5;
    static Tex32 cheese;
    cheese.make(4, 4);
    s.reward = &cheese;
    const auto t1 = std::chrono::steady_clock::now();
    for (int k = 0; k < 20; ++k) { r.begin(0x000000); draw_world(r, lv, s, 1.0); }
    std::printf("render %.2f ms/frame, %lld tris\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count() / 20, r.tris);
    write_ppm(argv[2], r);
    // and the map, as text
    const Floor& fl = lv.floors[0];
    for (int y = fl.h - 1; y >= 0; --y) {
        for (int x = 0; x < fl.w; ++x) {
            const Cell& c = fl.at(x, y);
            char ch = c.block == Block::wall ? '#' : c.block == Block::door ? 'D' : ' ';
            if (c.pad >= 0) ch = 'p';
            if (c.cpad >= 0) ch = 'c';
            if (c.portal >= 0) ch = 'O';
            if (c.elevator) ch = 'E';
            if (c.goal) ch = 'g';
            if (lv.thing_at({0, x, y}) >= 0) ch = lv.things[static_cast<size_t>(lv.thing_at({0, x, y}))].kind == ThingKind::flip ? 'F' : 'b';
            if (Pos{0, x, y} == lv.start) ch = 'S';
            if (Pos{0, x, y} == lv.goal) ch = 'G';
            std::putchar(ch);
        }
        std::putchar('\n');
    }
    return 0;
}
