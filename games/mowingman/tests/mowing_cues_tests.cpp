// Mowing's live one-shots and gnome voice (CTest mowingman_cues). Without arguments:
// every cue renders at its master's length and level, deterministically, and reaches
// the shell's bank. With --reference DIR: every cue the directory holds (the float
// stereo masters captured from make_audio.py, or the gnome's from gnome_voice.mjs)
// equals its master to the tolerance given for it and quantises to the same 16-bit
// samples as the script wrote them.
#include "gnome_voice.hpp"
#include "mowing_cues.hpp"
#include "voice_reference.hpp"

#include <chrono>
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
    std::size_t frames;
    double peak;       // the level the script scales to; 0: the gnome's loudness levelling
    bool truncate;     // make_audio.py truncates to 16 bits; gnome_voice.mjs rounds (Math.round)
    double tolerance;  // of the master's peak
};

// make_audio.py's lengths and peaks (stereo masters), and the gnome's lengths from
// gnome_voice.mjs (mono masters, levelled to -17 dBFS RMS and soft-limited below 0.95).
// The gnome's references are Node's renders on macOS. V8's Math.pow there is macOS's
// own pow, which no portable pow reproduces (gnome_math.cpp), so the clips are bit for
// bit only where std::pow is macOS's. Elsewhere (the browser build's musl) a last-bit
// pow difference can start one glottal cycle a tick early, which shifts the rest of
// that clip by a fraction of a cycle's jitter: measured 2.96e-3 of peak at most
// (mm_gnome_giggle, in the browser build and with a correctly rounded pow alike). There
// the gnome is held to 5e-3 of peak and its 16-bit samples are not compared.
#if defined(__APPLE__)
constexpr bool gnome_exact = true;
#else
constexpr bool gnome_exact = false;
#endif
constexpr double gnome_tolerance = gnome_exact ? 1e-9 : 5e-3;
constexpr Expected expected[] = {
    {"mm_freeze", 28800, 0.5, true, 1e-9},      {"mm_shatter", 57600, 0.75, true, 1e-9},
    {"mm_scream", 40800, 0.35, true, 1e-9},     {"mm_puff", 21600, 0.3, true, 1e-9},
    {"mm_mushroom", 12000, 0.45, true, 1e-9},   {"mm_bump", 21600, 0.5, true, 1e-9},
    {"mm_granny_bonk", 124800, 0.75, true, 1e-9},
    {"mm_gnome_hoo", 28800, 0, false, gnome_tolerance},    {"mm_gnome_hoo2", 36000, 0, false, gnome_tolerance},
    {"mm_gnome_giggle", 132000, 0, false, gnome_tolerance}, {"mm_gnome_giggle2", 72000, 0, false, gnome_tolerance},
    {"mm_gnome_nyah", 108000, 0, false, gnome_tolerance},
};

const voice::Cue* cue_named(const std::string& name) {
    for (const std::span<const voice::Cue> set : {mm::sound_cues(), mm::gnome_cues()}) {
        for (const voice::Cue& cue : set) {
            if (cue.name == name) {
                return &cue;
            }
        }
    }
    return nullptr;
}

// Math.round: halves go up (toward +infinity)
double js_round(double x) {
    const double r = std::floor(x);
    return (x - r >= 0.5) ? r + 1.0 : r;
}

// A cue's master as stereo (a mono master plays as left = right).
voice::Stereo render_stereo(const voice::Cue& cue) {
    if (cue.stereo != nullptr) {
        return cue.stereo();
    }
    const voice::Signal y = cue.mono();
    return {y, y};
}

const Expected* expected_named(const std::string& name) {
    for (const Expected& e : expected) {
        if (name == e.name) {
            return &e;
        }
    }
    return nullptr;
}

bool finite(const voice::Signal& x) {
    for (const double value : x) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

void self_tests() {
    for (const Expected& e : expected) {
        const voice::Cue* cue = cue_named(e.name);
        const bool gnome = e.peak == 0;
        require(cue != nullptr && (gnome ? (*cue).mono != nullptr : (*cue).stereo != nullptr),
                std::string(e.name) + (gnome ? " is a mono cue" : " is a stereo cue"));
        if (cue == nullptr || (gnome ? (*cue).mono == nullptr : (*cue).stereo == nullptr)) {
            continue;
        }
        const auto begin = std::chrono::steady_clock::now();
        const voice::Stereo y = render_stereo(*cue);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        const double top = std::max(voice::peak(y.left), voice::peak(y.right));
        std::printf("%-18s %7zu frames  peak %.4f  %7.1f ms\n", e.name, y.left.size(), top, ms);
        require(y.left.size() == e.frames && y.right.size() == e.frames, std::string(e.name) + " has its master's length");
        require(finite(y.left) && finite(y.right), std::string(e.name) + " is finite");
        if (gnome) {
            require(top < 0.95 && top > 0.1, std::string(e.name) + " is levelled under the soft limit");
        } else {
            require(top <= e.peak * (1 + 1e-12) && top >= e.peak * (1 - 1e-12),
                    std::string(e.name) + " peaks at its master's level");
        }
        const voice::Stereo again = render_stereo(*cue);
        require(again.left == y.left && again.right == y.right, std::string(e.name) + " renders the same every time");
    }
    mm::register_sound_cues();
    require(voice::Bank::shared().find("mm_bump").has_value(), "the cues reach the shell's bank");
    const voice::Interleaved clip = voice::render(*voice::Bank::shared().find("mm_mushroom"));
    require(clip.size() == 2 * 12000, "a clip is interleaved stereo");
    require(voice::Bank::shared().find("mm_gnome_nyah").has_value(), "the gnome reaches the shell's bank");
}

std::vector<std::int16_t> quantise(const voice::Stereo& x, bool truncate) {
    std::vector<std::int16_t> out(2 * x.left.size());
    for (std::size_t i = 0; i < x.left.size(); ++i) {
        for (int c = 0; c < 2; ++c) {
            const double v = std::clamp(c == 0 ? x.left[i] : x.right[i], -1.0, 1.0) * 32767;
            out[2 * i + c] = static_cast<std::int16_t>(truncate ? std::trunc(v) : js_round(v));
        }
    }
    return out;
}

void reference_tests(const std::string& directory) {
    const std::vector<voice::reference::Case> cases = voice::reference::read_manifest(directory);
    require(!cases.empty(), "the reference holds cues");
    std::printf("%-18s %8s %13s %10s %9s %9s %11s\n", "cue", "frames", "max err/peak", "error dB", "identical", "pcm16",
                "render");
    for (const voice::reference::Case& c : cases) {
        const std::string name = c.args.empty() ? c.file : c.args[0];
        const voice::Cue* cue = cue_named(name);
        const Expected* e = expected_named(name);
        const bool mono = c.kind == "cue";
        require(cue != nullptr && e != nullptr && (mono ? (*cue).mono != nullptr : (*cue).stereo != nullptr),
                "a cue of the reference's kind for " + name);
        if (cue == nullptr || e == nullptr || (mono ? (*cue).mono == nullptr : (*cue).stereo == nullptr)) {
            continue;
        }
        const auto begin = std::chrono::steady_clock::now();
        const voice::Stereo ours = render_stereo(*cue);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        std::vector<double> planar = voice::reference::read_array<double>(directory, c.file);
        if (mono) {
            const std::vector<double> copy = planar;
            planar.insert(planar.end(), copy.begin(), copy.end());
        }
        const std::size_t frames = planar.size() / 2;
        const voice::Stereo reference{{planar.begin(), planar.begin() + static_cast<std::ptrdiff_t>(frames)},
                                      {planar.begin() + static_cast<std::ptrdiff_t>(frames), planar.end()}};
        std::vector<double> ours_planar = ours.left;
        ours_planar.insert(ours_planar.end(), ours.right.begin(), ours.right.end());
        const voice::reference::Difference d = voice::reference::compare(ours_planar, planar);
        const std::vector<std::int16_t> ours16 = quantise(ours, (*e).truncate);
        const std::vector<std::int16_t> reference16 = quantise(reference, (*e).truncate);
        std::size_t pcm_differences = ours16.size() == reference16.size() ? 0 : reference16.size();
        for (std::size_t i = 0; i < ours16.size() && i < reference16.size(); ++i) {
            pcm_differences += ours16[i] != reference16[i] ? 1 : 0;
        }
        const double share = 100.0 * static_cast<double>(d.identical) / static_cast<double>(d.reference_length);
        std::printf("%-18s %8zu %13.3g %10.1f %8.2f%% %9s %8.1f ms\n", name.c_str(), frames, d.relative, d.error_db, share,
                    pcm_differences == 0 ? "same" : std::to_string(pcm_differences).c_str(), ms);
        require(d.same_length() && d.relative <= (*e).tolerance, name + " equals its master");
        if ((*e).peak != 0 || gnome_exact) {
            require(pcm_differences == 0, name + " quantises to the same 16-bit samples");
        }
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
