// The Parrot's Table's live sounds (CTest parrots_cues). Without arguments: every cue
// renders at its master's length and level and reaches the shell's bank. With
// --reference DIR (repeatable): every cue in DIR's manifest equals its NumPy float master
// to 1e-9 of its peak and quantises to the same 16-bit samples. DIR is written by
//   shared/voice/tools/numpy_reference.py cues --script games/parrots/audio_src/make_sfx.py --out DIR
//   games/penthesheep/tools/music_reference.py --script games/parrots/audio_src/parlor_music.py --out DIR
// With --time: each music cue's render time and the memory its clip keeps.
#include "parrot_cues.hpp"
#include "voice_reference.hpp"

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
    std::size_t channels;
    std::size_t frames;
    double peak_db;  // the effects' write() level; the music's ceiling
};

// The masters' lengths and the levels make_sfx.py and parlor_music.py wrote them at.
constexpr Expected expected[] = {
    {"pt_voice_01", 1, 4560, -16},        {"pt_voice_02", 1, 5280, -16},     {"pt_voice_03", 1, 6000, -16},
    {"pt_voice_04", 1, 4560, -16},        {"pt_voice_05", 1, 5280, -16},     {"pt_voice_06", 1, 6000, -16},
    {"pt_bell", 1, 76800, -12},           {"pt_mark_honest", 1, 12000, -16}, {"pt_mark_liar", 1, 12000, -14},
    {"pt_unmark", 1, 3840, -22},          {"pt_contradiction", 1, 24000, -14}, {"pt_ask", 1, 14880, -15},
    {"pt_gavel", 1, 32640, -9},           {"pt_flap", 1, 38880, -13},        {"pt_cheer", 1, 52800, -12},
    {"pt_tick", 1, 2400, -20},            {"pt_click", 1, 1920, -22},        {"pt_music", 2, 2194272, -3},
    {"pt_stinger_win", 2, 163200, -2.5},  {"pt_stinger_lose", 2, 163200, -2.5},
};

const voice::Cue* cue_named(const std::string& name) {
    for (const voice::Cue& cue : pt::sound_cues()) {
        if (cue.name == name) {
            return &cue;
        }
    }
    return nullptr;
}

// A cue's master, channel after channel (left then right), as the references store it.
std::vector<double> render_master(const voice::Cue& cue, std::size_t& channels) {
    if (cue.mono != nullptr) {
        channels = 1;
        return cue.mono();
    }
    const voice::Stereo s = cue.stereo();
    channels = 2;
    std::vector<double> both(s.left);
    both.insert(both.end(), s.right.begin(), s.right.end());
    return both;
}

void self_tests() {
    require(pt::sound_cues().size() == std::size(expected), "every sound has a cue");
    for (const Expected& e : expected) {
        const voice::Cue* cue = cue_named(e.name);
        require(cue != nullptr, std::string(e.name) + " has a cue");
        if (cue == nullptr) {
            continue;
        }
        std::size_t channels = 0;
        const std::vector<double> y = render_master(*cue, channels);
        bool finite = true;
        for (const double value : y) {
            finite = finite && std::isfinite(value);
        }
        const double level = std::pow(10.0, e.peak_db / 20);
        require(channels == e.channels, std::string(e.name) + " has its master's channels");
        require(y.size() == e.channels * e.frames, std::string(e.name) + " has its master's length");
        require(finite && voice::peak(y) <= level * (1 + 1e-12), std::string(e.name) + " stays at its master's level");
        require(finite && voice::peak(y) > 0.5 * level, std::string(e.name) + " is audible");
        if (channels == 1) {
            require((*cue).mono() == y, std::string(e.name) + " renders the same every time");
        }
    }
    pt::register_sound_cues();
    require(voice::Bank::shared().find("pt_cheer").has_value(), "the cues reach the shell's bank");
    const voice::Interleaved clip = voice::render(*voice::Bank::shared().find("pt_stinger_win"));
    require(clip.size() == 2 * 163200, "a stereo clip is interleaved");
}

void reference_tests(const std::string& directory) {
    const std::vector<voice::reference::Case> cases = voice::reference::read_manifest(directory);
    require(!cases.empty(), "the reference in " + directory + " holds cues");
    std::printf("%-17s %9s %13s %10s %9s %9s\n", "cue", "samples", "max err/peak", "error dB", "identical", "pcm16");
    for (const voice::reference::Case& c : cases) {
        const bool music = c.kind == "music";
        const voice::Cue* cue = c.args.empty() ? nullptr : cue_named(c.args[0]);
        require(cue != nullptr, "a cue for " + (c.args.empty() ? c.file : c.args[0]));
        if (cue == nullptr) {
            continue;
        }
        if (music) {
            require(c.args.size() >= 4 && c.args[3] == "trunc", c.args[0] + " was written by truncation");
        }
        std::size_t channels = 0;
        const std::vector<double> ours = render_master(*cue, channels);
        require(channels == (music ? std::stoul(c.args[1]) : 1), c.args[0] + " has the reference's channels");
        const std::vector<double> reference = voice::reference::read_array<double>(directory, c.file);
        const voice::reference::Difference d = voice::reference::compare(ours, reference);
        const std::vector<std::int16_t> ours16 = voice::pcm16(ours);
        const std::vector<std::int16_t> reference16 = voice::pcm16(reference);
        std::size_t pcm_differences = ours16.size() == reference16.size() ? 0 : reference16.size();
        for (std::size_t i = 0; i < ours16.size() && i < reference16.size(); ++i) {
            pcm_differences += ours16[i] != reference16[i] ? 1 : 0;
        }
        const double share = 100.0 * static_cast<double>(d.identical) / static_cast<double>(d.reference_length);
        std::printf("%-17s %9zu %13.3g %10.1f %8.2f%% %9s\n", c.args[0].c_str(), d.reference_length, d.relative,
                    d.error_db, share, pcm_differences == 0 ? "same" : std::to_string(pcm_differences).c_str());
        require(d.same_length() && d.relative <= 1e-9, c.args[0] + " equals its NumPy master");
        require(pcm_differences == 0, c.args[0] + " quantises to the same 16-bit samples");
    }
}

void time_tests() {
    for (const voice::Cue& cue : pt::sound_cues()) {
        if (cue.stereo == nullptr) {
            continue;
        }
        const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
        const voice::Interleaved clip = voice::render(cue);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        std::printf("%-17s %7.2f s of audio rendered in %6.3f s, clip keeps %6.1f MB\n", std::string(cue.name).c_str(),
                    static_cast<double>(clip.size() / 2) / voice::sample_rate, seconds,
                    static_cast<double>(clip.size() * sizeof(float)) / 1e6);
    }
}

}  // namespace

int main(int argc, char** argv) {
    bool ran = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--reference" && i + 1 < argc) {
            reference_tests(argv[++i]);
            ran = true;
        } else if (arg == "--time") {
            time_tests();
            ran = true;
        }
    }
    if (!ran) {
        self_tests();
    }
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
