// Maze 95's live effects and jingles (CTest maze_cues). Without arguments: every cue
// renders at its master's length, channels and level and reaches the shell's bank. With
// --reference DIR (repeatable): every cue in DIR's manifest equals the NumPy float master to
// 1e-9 of its peak and quantises to the same 16-bit samples. DIR comes from
// shared/voice/tools/numpy_reference.py cues --script games/maze/audio_src/make_sfx.py
// --fade 0.005 (the effects) or games/penthesheep/tools/music_reference.py --script
// games/maze/audio_src/mall_music.py with arrange() run first in main(), as the masters
// were made (the jingles). --time prints how long each cue takes to render.
#include "maze_cues.hpp"
#include "voice_reference.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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
    std::size_t samples;  // frames
    double peak_db;       // the effect's write() level, or the jingle's tanh ceiling
    bool stereo;
};

constexpr Expected expected[] = {
    {"mz_step_01", 7680, -19, false},       {"mz_step_02", 7680, -19, false},     {"mz_step_03", 7680, -19, false},
    {"mz_turn", 10560, -26, false},         {"mz_bump", 12000, -15, false},       {"mz_locked", 21600, -13, false},
    {"mz_pad", 28800, -14, false},          {"mz_door", 72000, -12, false},       {"mz_flip", 62400, -12, false},
    {"mz_bulb_off", 28800, -12, false},     {"mz_bulb_on", 33600, -16, false},    {"mz_elevator", 124800, -12, false},
    {"mz_portal", 38400, -13, false},       {"mz_marble_roll", 36000, -12, false}, {"mz_marble_bounce", 19200, -14, false},
    {"mz_marble_bump", 33600, -10, false},  {"mz_appear", 28800, -16, false},     {"mz_poof", 19200, -17, false},
    {"mz_click", 2400, -22, false},         {"mz_snail_squelch", 24000, -22, false}, {"mz_snail_hide", 15360, -17, false},
    {"mz_snail_peek", 10560, -24, false},   {"mz_meow", 29760, -15, false},       {"mz_honk", 17280, -12, false},
    {"mz_bell", 48000, -14, false},         {"mz_burp", 20160, -13, false},       {"mz_keys", 91200, -14, false},
    {"mz_ooh", 43200, -15, false},          {"mz_thinker", 192000, -10, false},   {"mz_stinger_win", 153600, -2.5, true},
    {"mz_start", 153600, -2.5, true},
};

const voice::Cue* cue_named(const std::string& name) {
    for (const voice::Cue& cue : mz::sound_cues()) {
        if (cue.name == name) {
            return &cue;
        }
    }
    return nullptr;
}

// The cue's channels, one after another (a mono cue has one).
std::vector<voice::Signal> channels(const voice::Cue& cue) {
    std::vector<voice::Signal> result{};
    if (cue.mono != nullptr) {
        result.push_back(cue.mono());
    } else if (cue.stereo != nullptr) {
        const voice::Stereo s = cue.stereo();
        result.push_back(s.left);
        result.push_back(s.right);
    }
    return result;
}

void self_tests() {
    require(mz::sound_cues().size() == std::size(expected), "every effect and jingle has a cue");
    for (const Expected& e : expected) {
        const voice::Cue* cue = cue_named(e.name);
        const bool kind = cue != nullptr && (e.stereo ? (*cue).stereo != nullptr : (*cue).mono != nullptr);
        require(kind, std::string(e.name) + (e.stereo ? " is a stereo cue" : " is a mono cue"));
        if (!kind) {
            continue;
        }
        const std::vector<voice::Signal> y = channels(*cue);
        const double level = std::pow(10.0, e.peak_db / 20);
        double top = 0;
        bool finite = true;
        bool lengths = true;
        for (const voice::Signal& c : y) {
            lengths = lengths && c.size() == e.samples;
            top = std::max(top, voice::peak(c));
            for (const double value : c) {
                finite = finite && std::isfinite(value);
            }
        }
        require(lengths, std::string(e.name) + " has its master's length");
        require(finite && top <= level * (1 + 1e-12), std::string(e.name) + " stays within its master's level");
        require(finite && top > (e.stereo ? 0.1 : 0.5) * level, std::string(e.name) + " is audible");
        require(channels(*cue) == y, std::string(e.name) + " renders the same every time");
    }
    mz::register_sound_cues();
    require(voice::Bank::shared().find("mz_snail_peek").has_value(), "the cues reach the shell's bank");
    require(voice::Bank::shared().find("mz_start").has_value(), "the jingles reach the shell's bank");
    const voice::Interleaved clip = voice::render(*voice::Bank::shared().find("mz_click"));
    require(clip.size() == 2 * 2400, "a clip is interleaved stereo");
}

void reference_tests(const std::string& directory) {
    const std::vector<voice::reference::Case> cases = voice::reference::read_manifest(directory);
    require(!cases.empty(), "the reference in " + directory + " holds cues");
    for (const voice::reference::Case& c : cases) {
        const std::string name = c.args.empty() ? c.file : c.args[0];
        const voice::Cue* cue = cue_named(name);
        require(cue != nullptr, "a cue for " + name);
        if (cue == nullptr) {
            continue;
        }
        const std::vector<voice::Signal> ours = channels(*cue);
        const std::size_t count = c.kind == "music" && c.args.size() >= 3 ? std::stoul(c.args[1]) : 1;
        require(ours.size() == count, name + " has the reference's channels");
        const std::vector<double> all = voice::reference::read_array<double>(directory, c.file);
        const std::size_t frames = count == 0 ? 0 : all.size() / count;
        double worst = 0;
        double worst_db = -400;
        std::size_t identical = 0;
        std::size_t pcm_differences = 0;
        bool lengths = true;
        for (std::size_t ch = 0; ch < count && ch < ours.size(); ++ch) {
            const std::vector<double> reference(all.begin() + static_cast<std::ptrdiff_t>(ch * frames),
                                                all.begin() + static_cast<std::ptrdiff_t>((ch + 1) * frames));
            const voice::reference::Difference d = voice::reference::compare(ours[ch], reference);
            lengths = lengths && d.same_length();
            worst = std::max(worst, d.relative);
            worst_db = std::max(worst_db, d.error_db);
            identical += d.identical;
            const std::vector<std::int16_t> ours16 = voice::pcm16(ours[ch]);
            const std::vector<std::int16_t> reference16 = voice::pcm16(reference);
            pcm_differences += ours16.size() == reference16.size() ? 0 : reference16.size();
            for (std::size_t i = 0; i < ours16.size() && i < reference16.size(); ++i) {
                pcm_differences += ours16[i] != reference16[i] ? 1 : 0;
            }
        }
        const double share = all.empty() ? 0 : 100.0 * static_cast<double>(identical) / static_cast<double>(all.size());
        std::printf("%-17s %2zu ch %8zu %13.3g %10.1f %8.2f%% %9s\n", name.c_str(), count, frames, worst, worst_db, share,
                    pcm_differences == 0 ? "same" : std::to_string(pcm_differences).c_str());
        require(lengths && worst <= 1e-9, name + " equals its NumPy master");
        require(pcm_differences == 0, name + " quantises to the same 16-bit samples");
    }
}

void timing() {
    for (const voice::Cue& cue : mz::sound_cues()) {
        const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
        const std::vector<voice::Signal> y = channels(cue);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        std::printf("%-17s %8.4f s  (%zu frames)\n", std::string(cue.name).c_str(), seconds, y.empty() ? 0 : y[0].size());
    }
}

}  // namespace

int main(int argc, char** argv) {
    bool referenced = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--reference" && i + 1 < argc) {
            if (!referenced) {
                std::printf("%-17s %5s %8s %13s %10s %9s %9s\n", "cue", "ch", "frames", "max err/peak", "error dB",
                            "identical", "pcm16");
            }
            reference_tests(argv[++i]);
            referenced = true;
        } else if (arg == "--time") {
            timing();
            referenced = true;
        }
    }
    if (!referenced) {
        self_tests();
    }
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
