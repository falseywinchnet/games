// The audio batch 1/2 cues, rendered live (CTest batch_cues). Without arguments: every
// cue renders at its master's length and level, the same every time, and reaches the
// shell's bank. With --reference DIR... (tools/batch_reference.py `cues` and `music`
// outputs): every cue equals the NumPy float master to 1e-9 of its peak and quantises
// to the same 24-bit samples (the masters are 24-bit WAVs).
#include "batch_cues.hpp"
#include "voice_reference.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
    double peak_db;
};

constexpr Expected expected_mono[] = {
#include "expected_cues.inc"
};

// music_switchbox_loop: 52 bars of 4 beats at 22500 samples a beat, stereo.
constexpr const char* loop_name = "music_switchbox_loop";
constexpr std::size_t loop_samples = 4680000;

std::vector<voice::Cue> cues_cache{};

const voice::Cue* cue_named(const std::string& name) {
    if (cues_cache.empty()) {
        cues_cache = batch_cues::all_cues();
    }
    for (const voice::Cue& cue : cues_cache) {
        if (cue.name == name) {
            return &cue;
        }
    }
    return nullptr;
}

bool finite(const voice::Signal& y) {
    bool result = true;
    for (const double value : y) {
        result = result && std::isfinite(value);
    }
    return result;
}

double seconds_since(std::chrono::steady_clock::time_point start) {
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    const double result = elapsed.count();
    return result;
}

void self_tests(bool with_music) {
    require(batch_cues::all_cues().size() == std::size(expected_mono) + 1, "every played batch file has a cue");
    for (const Expected& e : expected_mono) {
        const voice::Cue* cue = cue_named(e.name);
        require(cue != nullptr && (*cue).mono != nullptr, std::string(e.name) + " is a mono cue");
        if (cue == nullptr || (*cue).mono == nullptr) {
            continue;
        }
        const voice::Signal y = (*cue).mono();
        const double level = std::pow(10.0, e.peak_db / 20);
        require(y.size() == e.samples, std::string(e.name) + " has its master's length");
        require(finite(y) && std::fabs(voice::peak(y) - level) <= level * 1e-12,
                std::string(e.name) + " peaks at its master's level");
        require((*cue).mono() == y, std::string(e.name) + " renders the same every time");
    }
    const voice::Cue* loop = cue_named(loop_name);
    require(loop != nullptr && (*loop).stereo != nullptr, "the Switchbox loop is a stereo cue");
    if (with_music && loop != nullptr && (*loop).stereo != nullptr) {
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        const voice::Stereo y = (*loop).stereo();
        std::printf("%s rendered in %.2f s\n", loop_name, seconds_since(start));
        const double ceiling = std::pow(10.0, -3.0 / 20);
        require(y.left.size() == loop_samples && y.right.size() == loop_samples, "the loop has its master's length");
        require(finite(y.left) && finite(y.right), "the loop is finite");
        require(std::max(voice::peak(y.left), voice::peak(y.right)) <= ceiling, "the loop stays under -3 dBFS");
        require(voice::peak(y.left) > 0.25, "the loop is audible");
    }
    batch_cues::register_batch_cues();
    require(voice::Bank::shared().find("ui_hint_02").has_value(), "the cues reach the shell's bank");
    require(voice::Bank::shared().find(loop_name).has_value(), "the loop reaches the shell's bank");
    const voice::Interleaved clip = voice::render(*voice::Bank::shared().find("sudoku_note_remove"));
    require(clip.size() == 2 * 12000, "a clip is interleaved stereo");
}

std::vector<std::int32_t> pcm24(const std::vector<double>& x) {
    std::vector<std::int32_t> q(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double clipped = std::clamp(x[i], -1.0, 1.0);
        q[i] = static_cast<std::int32_t>(std::nearbyint(clipped * 8388607));
    }
    return q;
}

std::size_t pcm24_differences(const std::vector<double>& ours, const std::vector<double>& reference) {
    const std::vector<std::int32_t> a = pcm24(ours);
    const std::vector<std::int32_t> b = pcm24(reference);
    std::size_t result = a.size() == b.size() ? 0 : std::max(a.size(), b.size());
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        result += a[i] != b[i] ? 1 : 0;
    }
    return result;
}

std::vector<std::string> compared{};

void compare_one(const std::string& name, const std::vector<double>& ours, const std::vector<double>& reference) {
    const voice::reference::Difference d = voice::reference::compare(ours, reference);
    const std::size_t pcm = pcm24_differences(ours, reference);
    const double share = 100.0 * static_cast<double>(d.identical) / static_cast<double>(d.reference_length);
    std::printf("%-28s %8zu %13.3g %10.1f %8.2f%% %9s\n", name.c_str(), d.reference_length, d.relative, d.error_db,
                share, pcm == 0 ? "same" : std::to_string(pcm).c_str());
    require(d.same_length() && d.relative <= 1e-9, name + " equals its NumPy master");
    require(pcm == 0, name + " quantises to the same 24-bit master");
}

void reference_tests(const std::vector<std::string>& directories) {
    std::printf("%-28s %8s %13s %10s %9s %9s\n", "cue", "samples", "max err/peak", "error dB", "identical", "pcm24");
    for (const std::string& directory : directories) {
        const std::vector<voice::reference::Case> cases = voice::reference::read_manifest(directory);
        require(!cases.empty(), "a reference manifest in " + directory);
        for (const voice::reference::Case& c : cases) {
            if (c.args.empty()) {
                continue;
            }
            const std::string& name = c.args[0];
            const voice::Cue* cue = cue_named(name);
            if (cue == nullptr) {
                continue;  // the script's other cues, which no game plays
            }
            const std::vector<double> reference = voice::reference::read_array<double>(directory, c.file);
            if (c.kind == "cue" && (*cue).mono != nullptr) {
                compare_one(name, (*cue).mono(), reference);
            } else if (c.kind == "stereo" && (*cue).stereo != nullptr) {
                const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
                const voice::Stereo ours = (*cue).stereo();
                std::printf("%s rendered in %.2f s\n", name.c_str(), seconds_since(start));
                const std::size_t half = reference.size() / 2;
                const std::vector<double> left(reference.begin(), reference.begin() + static_cast<std::ptrdiff_t>(half));
                const std::vector<double> right(reference.begin() + static_cast<std::ptrdiff_t>(half), reference.end());
                compare_one(name + " (left)", ours.left, left);
                compare_one(name + " (right)", ours.right, right);
            } else {
                require(false, name + " has the reference's channel layout");
            }
            compared.push_back(name);
        }
    }
    for (const voice::Cue& cue : batch_cues::all_cues()) {
        const std::string name(cue.name);
        require(std::find(compared.begin(), compared.end(), name) != compared.end(), name + " was compared");
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--reference") {
        std::vector<std::string> directories{};
        for (int i = 2; i < argc; ++i) {
            directories.push_back(argv[i]);
        }
        reference_tests(directories);
    } else {
        const bool quick = argc == 2 && std::string(argv[1]) == "--quick";
        self_tests(!quick);
    }
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
