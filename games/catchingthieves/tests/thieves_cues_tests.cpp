// Catching Thieves' live sound (CTest catchingthieves_cues). Without arguments: every cue
// but the music loops renders (--loops: every cue) at its master's length and level and
// reaches the shell's bank. With --reference DIR (shared/voice/tools/numpy_reference.py
// cues --script games/catchingthieves/audio_src/make_sfx.py for the effects,
// shared/voice/tools/score_reference.py --script
// games/catchingthieves/audio_src/garden_music.py for the music; one DIR each): every cue
// in DIR equals the NumPy float master to 1e-9 of its peak, and its 16-bit quantisation
// equals the script's (and so the shipped WAVs and audio_src/masters, which the reference
// scripts compare byte for byte). With --time NAME: renders that cue once and prints the
// time it took and the peak memory (the process's resident peak natively, the wasm heap
// under Node).
#include "thieves_cues.hpp"
#include "voice_reference.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten/heap.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace {

int checks = 0;
int failures = 0;

void require(bool condition, const std::string& what) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAILED: %s\n", what.c_str());
    }
}

struct Expected {
    const char* name;
    std::size_t frames;
    double peak;  // the master's peak (effects), or its ceiling (the music)
};

double db(double x) {
    return std::pow(10.0, x / 20);
}

// The masters' lengths and the levels make_sfx.py and garden_music.py wrote them at.
const Expected expected[] = {
    {"ct_step_01", 5760, db(-20)},          {"ct_step_02", 5760, db(-20)},
    {"ct_step_03", 5760, db(-20)},          {"ct_push_01", 15360, db(-15)},
    {"ct_push_02", 15360, db(-15)},         {"ct_push_03", 15360, db(-15)},
    {"ct_trap", 28800, db(-10)},            {"ct_pop", 5280, db(-16)},
    {"ct_duck", 5760, db(-18)},             {"ct_muffle_01", 24315, db(-17)},
    {"ct_muffle_02", 34445, db(-17)},       {"ct_raspberry", 26400, db(-14)},
    {"ct_crunch", 8640, db(-18)},           {"ct_laugh", 31680, db(-15)},
    {"ct_bump", 12000, db(-20)},            {"ct_undo", 5760, db(-22)},
    {"ct_restart", 19200, db(-17)},         {"ct_hint", 28800, db(-16)},
    {"ct_click", 1920, db(-22)},            {"ct_music_spring", 2468544, db(-3)},
    {"ct_music_summer", 2304000, db(-3)},  {"ct_music_autumn", 2658432, db(-3)},
    {"ct_music_winter", 2880000, db(-3)},  {"ct_music_night", 3291456, db(-3)},
    {"ct_stinger_win", 172800, db(-2.5)},  {"ct_stinger_stuck", 172800, db(-2.5)},
    {"ct_stinger_book", 172800, db(-2.5)},
};

const voice::Cue* cue_named(const std::string& name) {
    for (const voice::Cue& cue : ct::sound_cues()) {
        if (cue.name == name) {
            return &cue;
        }
    }
    return nullptr;
}

// A cue's master as channels (one for a mono cue).
std::vector<voice::Signal> render_channels(const voice::Cue& cue) {
    if (cue.mono != nullptr) {
        return {cue.mono()};
    }
    voice::Stereo s = cue.stereo();
    return {std::move(s.left), std::move(s.right)};
}

void self_tests(bool with_loops) {
    require(ct::sound_cues().size() == std::size(expected), "every sound has a cue");
    for (const Expected& e : expected) {
        const voice::Cue* cue = cue_named(e.name);
        require(cue != nullptr, std::string(e.name) + " has a cue");
        if (cue == nullptr || (e.frames > 1000000 && !with_loops)) {
            continue;
        }
        const std::vector<voice::Signal> channels = render_channels(*cue);
        double top = 0;
        bool finite = true;
        for (const voice::Signal& c : channels) {
            require(c.size() == e.frames, std::string(e.name) + " has its master's length");
            for (const double value : c) {
                finite = finite && std::isfinite(value);
            }
            top = std::max(top, voice::peak(c));
        }
        require(finite && top <= e.peak * (1 + 1e-12), std::string(e.name) + " stays within its master's level");
        require(finite && top > .5 * e.peak, std::string(e.name) + " is audible");
        if (cue->mono != nullptr) {
            require(cue->mono() == channels[0], std::string(e.name) + " renders the same every time");
        }
    }
    ct::register_sound_cues();
    require(voice::Bank::shared().find("ct_music_night").has_value(), "the cues reach the shell's bank");
    const voice::Interleaved clip = voice::render(*voice::Bank::shared().find("ct_click"));
    require(clip.size() == 2 * 1920, "a clip is interleaved stereo");
}

void reference_tests(const std::string& directory) {
    const std::vector<voice::reference::Case> cases = voice::reference::read_manifest(directory);
    require(!cases.empty(), "the reference holds cues");
    std::printf("%-12s %3s %9s %13s %10s %9s %9s %8s\n", "cue", "ch", "frames", "max err/peak", "error dB", "identical",
                "pcm16", "seconds");
    for (const voice::reference::Case& c : cases) {
        const voice::Cue* cue = c.args.empty() ? nullptr : cue_named(c.args[0]);
        require(cue != nullptr, "a cue for " + (c.args.empty() ? c.file : c.args[0]));
        if (cue == nullptr) {
            continue;
        }
        const std::size_t ref_channels = c.args.size() >= 2 ? std::stoul(c.args[1]) : 1;
        const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
        const std::vector<voice::Signal> ours = render_channels(*cue);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        const std::vector<double> reference = voice::reference::read_array<double>(directory, c.file);
        const std::size_t frames = reference.size() / ref_channels;
        double worst = 0;
        double error_db = -400;
        std::size_t identical = 0;
        std::size_t pcm_differences = 0;
        bool same_length = true;
        for (std::size_t ch = 0; ch < ref_channels; ++ch) {
            const voice::Signal& mine = ours[std::min(ch, ours.size() - 1)];
            const std::span<const double> theirs(reference.data() + ch * frames, frames);
            const voice::reference::Difference d = voice::reference::compare(mine, theirs);
            same_length = same_length && d.same_length();
            worst = std::max(worst, d.relative);
            error_db = std::max(error_db, d.error_db);
            identical += d.identical;
            const std::vector<std::int16_t> mine16 = voice::pcm16(mine);
            const std::vector<std::int16_t> theirs16 = voice::pcm16(theirs);
            pcm_differences += mine16.size() == theirs16.size() ? 0 : theirs16.size();
            for (std::size_t i = 0; i < mine16.size() && i < theirs16.size(); ++i) {
                pcm_differences += mine16[i] != theirs16[i] ? 1 : 0;
            }
        }
        const double share = 100.0 * static_cast<double>(identical) / static_cast<double>(reference.size());
        std::printf("%-12s %3zu %9zu %13.3g %10.1f %8.2f%% %9s %8.2f\n", c.args[0].c_str(), ref_channels, frames, worst,
                    error_db, share, pcm_differences == 0 ? "same" : std::to_string(pcm_differences).c_str(), seconds);
        require(same_length && worst <= 1e-9, c.args[0] + " equals its NumPy master");
        require(pcm_differences == 0, c.args[0] + " quantises to the script's 16-bit master");
    }
}

double peak_memory_mb() {
#if defined(__EMSCRIPTEN__)
    return static_cast<double>(emscripten_get_heap_size()) / (1024.0 * 1024.0);
#elif defined(__APPLE__)
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);  // bytes on macOS
#elif defined(__unix__)
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return static_cast<double>(usage.ru_maxrss) / 1024.0;  // kilobytes on Linux
#else
    return 0;
#endif
}

void time_cue(const std::string& name) {
    const voice::Cue* cue = cue_named(name);
    require(cue != nullptr, "a cue called " + name);
    if (cue == nullptr) {
        return;
    }
    const double before = peak_memory_mb();
    const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
    const voice::Interleaved clip = voice::render(*cue);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    std::printf("%s: %zu frames rendered in %.2f s; peak memory %.0f MB (%.0f MB before), clip %.0f MB\n", name.c_str(),
                clip.size() / 2, seconds, peak_memory_mb(), before,
                static_cast<double>(clip.size() * sizeof(float)) / (1024.0 * 1024.0));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--reference") {
        reference_tests(argv[2]);
    } else if (argc == 3 && std::string(argv[1]) == "--time") {
        time_cue(argv[2]);
    } else {
        self_tests(argc == 2 && std::string(argv[1]) == "--loops");
    }
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
