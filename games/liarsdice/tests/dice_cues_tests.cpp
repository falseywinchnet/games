// Liar's Dice's live sounds (CTest liarsdice_cues). Without arguments: every cue renders
// at its master's length, channels and level and reaches the shell's bank. With
// --reference DIR (repeatable): every cue in DIR's manifest equals the NumPy float master
// to 1e-9 of its peak and quantises to the same 16-bit samples. DIR is written by
// shared/voice/tools/numpy_reference.py cues --script games/liarsdice/audio_src/make_sfx.py
// (`cue` lines) or games/penthesheep/tools/music_reference.py --script
// games/liarsdice/audio_src/locker_music.py (`music` lines). --time renders each music cue
// and prints its render time and the memory its clip keeps.
#include "dice_cues.hpp"
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
    std::size_t samples;
    double peak_db;  // the write() level of an effect; the finish() ceiling of music
    bool stereo;
};

// The masters' lengths and the levels the scripts wrote them at.
constexpr Expected expected[] = {
    {"ld_rattle_01", 43200, -13, false},    {"ld_rattle_02", 43200, -13, false},
    {"ld_rattle_03", 43200, -13, false},    {"ld_cup_down", 16800, -12, false},
    {"ld_cup_lift", 14400, -18, false},     {"ld_tap", 5760, -16, false},
    {"ld_count", 12000, -17, false},        {"ld_plunk", 48000, -11, false},
    {"ld_out", 62400, -15, false},          {"ld_nope", 7200, -18, false},
    {"ld_click", 1920, -22, false},         {"ld_voice_0_01", 3360, -17, false},
    {"ld_voice_0_02", 3360, -17, false},    {"ld_voice_0_03", 3360, -17, false},
    {"ld_voice_1_01", 5280, -17, false},    {"ld_voice_1_02", 5280, -17, false},
    {"ld_voice_1_03", 5280, -17, false},    {"ld_voice_2_01", 3840, -17, false},
    {"ld_voice_2_02", 3840, -17, false},    {"ld_voice_2_03", 3840, -17, false},
    {"ld_voice_3_01", 6240, -17, false},    {"ld_voice_3_02", 6240, -17, false},
    {"ld_voice_3_03", 6240, -17, false},    {"ld_voice_4_01", 4320, -17, false},
    {"ld_voice_4_02", 4320, -17, false},    {"ld_voice_4_03", 4320, -17, false},
    {"ld_voice_5_01", 7680, -17, false},    {"ld_voice_5_02", 7680, -17, false},
    {"ld_voice_5_03", 7680, -17, false},    {"ld_voice_6_01", 4800, -17, false},
    {"ld_voice_6_02", 4800, -17, false},    {"ld_voice_6_03", 4800, -17, false},
    {"ld_voice_7_01", 7200, -17, false},    {"ld_voice_7_02", 7200, -17, false},
    {"ld_voice_7_03", 7200, -17, false},    {"ld_keeper_01", 9600, -14, false},
    {"ld_keeper_02", 9600, -14, false},     {"ld_keeper_03", 9600, -14, false},
    {"ld_music", 1570896, -3, true},        {"ld_music_tense", 1570896, -3, true},
    {"ld_liar", 86400, -2.5, true},         {"ld_true", 86400, -2.5, true},
    {"ld_false", 86400, -2.5, true},        {"ld_stinger_win", 201600, -2.5, true},
    {"ld_stinger_lose", 201600, -2.5, true}, {"ld_stinger_free", 201600, -2.5, true},
};

const voice::Cue* cue_named(const std::string& name) {
    for (const voice::Cue& cue : ld::sound_cues()) {
        if (cue.name == name) {
            return &cue;
        }
    }
    return nullptr;
}

// A cue's master as channels (one for a mono cue).
std::vector<voice::Signal> channels_of(const voice::Cue& cue) {
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
    require(ld::sound_cues().size() == std::size(expected), "every sound has a cue");
    for (const Expected& e : expected) {
        const voice::Cue* cue = cue_named(e.name);
        const bool shaped = cue != nullptr && (e.stereo ? (*cue).stereo != nullptr : (*cue).mono != nullptr);
        require(shaped, std::string(e.name) + (e.stereo ? " is a stereo cue" : " is a mono cue"));
        if (!shaped) {
            continue;
        }
        const std::vector<voice::Signal> y = channels_of(*cue);
        bool finite = true;
        double top = 0;
        for (const voice::Signal& channel : y) {
            require(channel.size() == e.samples, std::string(e.name) + " has its master's length");
            for (const double value : channel) {
                finite = finite && std::isfinite(value);
            }
            top = std::max(top, voice::peak(channel));
        }
        const double level = std::pow(10.0, e.peak_db / 20);
        require(finite && top <= level * (1 + 1e-12), std::string(e.name) + " stays at its master's level");
        require(finite && top > (e.stereo ? 0.2 : 0.5) * level, std::string(e.name) + " is audible");
        if (!e.stereo) {
            require(channels_of(*cue) == y, std::string(e.name) + " renders the same every time");
        }
    }
    ld::register_sound_cues();
    require(voice::Bank::shared().find("ld_voice_5_02").has_value(), "the cues reach the shell's bank");
    const voice::Interleaved clip = voice::render(*voice::Bank::shared().find("ld_click"));
    require(clip.size() == 2 * 1920, "a clip is interleaved stereo");
    const voice::Interleaved stinger = voice::render(*voice::Bank::shared().find("ld_true"));
    require(stinger.size() == 2 * 86400, "a stereo cue is interleaved");
}

std::size_t pcm_differences(std::span<const double> ours, std::span<const double> reference) {
    const std::vector<std::int16_t> a = voice::pcm16(ours);
    const std::vector<std::int16_t> b = voice::pcm16(reference);
    std::size_t count = a.size() == b.size() ? 0 : b.size();
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        count += a[i] != b[i] ? 1 : 0;
    }
    return count;
}

void reference_tests(const std::string& directory) {
    const std::vector<voice::reference::Case> cases = voice::reference::read_manifest(directory);
    require(!cases.empty(), "the reference in " + directory + " holds cues");
    std::printf("%-16s %3s %8s %13s %10s %9s %9s\n", "cue", "ch", "samples", "max err/peak", "error dB", "identical",
                "pcm16");
    for (const voice::reference::Case& c : cases) {
        const bool music = c.kind == "music";
        const voice::Cue* cue = c.args.empty() ? nullptr : cue_named(c.args[0]);
        require(cue != nullptr, "a cue for " + (c.args.empty() ? c.file : c.args[0]));
        if (cue == nullptr) {
            continue;
        }
        const std::size_t count = music ? std::stoul(c.args[1]) : 1;
        const std::vector<double> reference = voice::reference::read_array<double>(directory, c.file);
        const std::size_t frames = reference.size() / count;
        const std::vector<voice::Signal> ours = channels_of(*cue);
        require(ours.size() == count, c.args[0] + " has the reference's channels");
        double relative = 0;
        double error_db = -400;
        std::size_t identical = 0;
        std::size_t pcm = 0;
        bool same_length = true;
        for (std::size_t ch = 0; ch < count && ch < ours.size(); ++ch) {
            const std::span<const double> slice(reference.data() + ch * frames, frames);
            const voice::reference::Difference d = voice::reference::compare(ours[ch], slice);
            same_length = same_length && d.same_length();
            relative = std::max(relative, d.relative);
            error_db = std::max(error_db, d.error_db);
            identical += d.identical;
            pcm += pcm_differences(ours[ch], slice);
        }
        const double share = 100.0 * static_cast<double>(identical) / static_cast<double>(reference.size());
        std::printf("%-16s %3zu %8zu %13.3g %10.1f %8.2f%% %9s\n", c.args[0].c_str(), count, frames, relative, error_db,
                    share, pcm == 0 ? "same" : std::to_string(pcm).c_str());
        require(same_length && relative <= 1e-9, c.args[0] + " equals its NumPy master");
        require(pcm == 0, c.args[0] + " quantises to the same 16-bit samples");
    }
}

void timing() {
    for (const Expected& e : expected) {
        const voice::Cue* cue = cue_named(e.name);
        if (!e.stereo || cue == nullptr) {
            continue;
        }
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        const voice::Interleaved clip = voice::render(*cue);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const double kept = static_cast<double>(clip.size() * sizeof(float)) / 1e6;
        const double doubles = static_cast<double>(e.samples * 2 * sizeof(double)) / 1e6;
        std::printf("%-16s %6.2f s audio  render %6.3f s  clip %6.1f MB  stereo master %6.1f MB\n", e.name,
                    static_cast<double>(e.samples) / voice::sample_rate, seconds, kept, doubles);
    }
}

}  // namespace

int main(int argc, char** argv) {
    bool referenced = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--reference" && i + 1 < argc) {
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
