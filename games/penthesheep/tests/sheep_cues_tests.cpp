// Pen the Sheep's live effects (CTest penthesheep_cues). Without arguments: every cue
// renders at its master's length and level and reaches the shell's bank. With
// --reference DIR (shared/voice/tools/numpy_reference.py cues --script
// games/penthesheep/audio_src/make_sfx.py --out DIR): every cue equals the NumPy float
// master to 1e-9 of its peak, and its 16-bit quantisation equals the shipped WAV.
#include "sheep_cues.hpp"
#include "voice_reference.hpp"

#include <cmath>
#include <cstdio>
#include <string>

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
    double peak_db;
};

// The masters' lengths and the levels make_sfx.py wrote them at.
constexpr Expected expected[] = {{"sh_place", 19200, -12}, {"sh_hop", 12000, -20},    {"sh_baa_01", 28800, -13},
                                 {"sh_baa_02", 36000, -13}, {"sh_baa_03", 38400, -13}, {"sh_baa_04", 21600, -13},
                                 {"sh_munch", 43200, -17},  {"sh_hint", 43200, -17},   {"sh_undo", 8640, -22},
                                 {"sh_click", 1920, -23},   {"sh_nope", 6720, -20},    {"sh_penned", 62400, -14},
                                 {"sh_escape", 24000, -13}};

const voice::Cue* cue_named(const std::string& name) {
    for (const voice::Cue& cue : sh::sound_cues()) {
        if (cue.name == name) {
            return &cue;
        }
    }
    return nullptr;
}

void self_tests() {
    require(sh::sound_cues().size() == std::size(expected), "every effect has a cue");
    for (const Expected& e : expected) {
        const voice::Cue* cue = cue_named(e.name);
        require(cue != nullptr && (*cue).mono != nullptr, std::string(e.name) + " is a mono cue");
        if (cue == nullptr || (*cue).mono == nullptr) {
            continue;
        }
        const voice::Signal y = (*cue).mono();
        bool finite = true;
        for (const double value : y) {
            finite = finite && std::isfinite(value);
        }
        const double level = std::pow(10.0, e.peak_db / 20);
        require(y.size() == e.samples, std::string(e.name) + " has its master's length");
        require(finite && voice::peak(y) <= level * (1 + 1e-12), std::string(e.name) + " peaks at its master's level");
        require(finite && voice::peak(y) > 0.5 * level, std::string(e.name) + " is audible");
        require((*cue).mono() == y, std::string(e.name) + " renders the same every time");
    }
    sh::register_sound_cues();
    require(voice::Bank::shared().find("sh_baa_03").has_value(), "the cues reach the shell's bank");
    const voice::Interleaved clip = voice::render(*voice::Bank::shared().find("sh_click"));
    require(clip.size() == 2 * 1920, "a clip is interleaved stereo");
}

void reference_tests(const std::string& directory) {
    const std::vector<voice::reference::Case> cases = voice::reference::read_manifest(directory);
    require(cases.size() == std::size(expected), "the reference holds every cue");
    std::printf("%-10s %8s %13s %10s %9s %9s\n", "cue", "samples", "max err/peak", "error dB", "identical", "pcm16");
    for (const voice::reference::Case& c : cases) {
        const voice::Cue* cue = c.args.empty() ? nullptr : cue_named(c.args[0]);
        require(cue != nullptr, "a cue for " + (c.args.empty() ? c.file : c.args[0]));
        if (cue == nullptr) {
            continue;
        }
        const voice::Signal ours = (*cue).mono();
        const std::vector<double> reference = voice::reference::read_array<double>(directory, c.file);
        const voice::reference::Difference d = voice::reference::compare(ours, reference);
        const std::vector<std::int16_t> ours16 = voice::pcm16(ours);
        const std::vector<std::int16_t> reference16 = voice::pcm16(reference);
        std::size_t pcm_differences = ours16.size() == reference16.size() ? 0 : reference16.size();
        for (std::size_t i = 0; i < ours16.size() && i < reference16.size(); ++i) {
            pcm_differences += ours16[i] != reference16[i] ? 1 : 0;
        }
        const double share = 100.0 * static_cast<double>(d.identical) / static_cast<double>(d.reference_length);
        std::printf("%-10s %8zu %13.3g %10.1f %8.2f%% %9s\n", c.args[0].c_str(), d.reference_length, d.relative,
                    d.error_db, share, pcm_differences == 0 ? "same" : std::to_string(pcm_differences).c_str());
        require(d.same_length() && d.relative <= 1e-9, c.args[0] + " equals its NumPy master");
        require(pcm_differences == 0, c.args[0] + " quantises to the shipped WAV");
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--reference") {
        reference_tests(argv[2]);
    } else {
        self_tests();
    }
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
