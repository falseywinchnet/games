// Eggy's live sounds (CTest eggy_cues). Without arguments: every effect and bed renders
// finite, at its master's level, the same every time, and reaches the shell's bank
// (the music loops are rendered by --music, which takes longer). With --reference DIR
// (the float masters captured from eggy_sfx.py / eggy_music.py as they handed them to
// write_wav): every cue in DIR equals its master to 1e-9 of its peak and quantises to
// the same samples as write_wav wrote them (24-bit rounding; the beds' 16-bit TPDF
// dither from default_rng(1) and default_rng(2)). --music renders each loop and prints
// its time and memory.
#include "eggy_cues.hpp"
#include "eggy_music.hpp"
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

struct Level {
    const char* name;
    double peak_db;
};

// The levels eggy_sfx.py finishes each effect at (the beds are -12 dB).
constexpr Level levels[] = {
    {"eggy_chirp_01", -17},   {"eggy_chirp_02", -17},     {"eggy_chirp_03", -17},    {"eggy_chirp_04", -17},
    {"eggy_chirp_05", -17},   {"eggy_chirp_06", -17},     {"eggy_step_grass", -27},  {"eggy_step_snow", -25},
    {"eggy_step_rock", -27},  {"eggy_step_ice", -28},     {"eggy_step_water", -23},  {"eggy_hop", -19},
    {"eggy_land", -19},       {"eggy_splash", -15},       {"eggy_drink", -17},       {"eggy_refresh", -12},
    {"eggy_pant", -21},       {"eggy_bonk", -12},         {"eggy_star", -10},        {"eggy_gust", -15},
    {"eggy_slide", -17},      {"eggy_preen", -23},        {"eggy_flap", -19},        {"eggy_milestone", -12},
    {"eggy_biome", -16},      {"eggy_tent", -15},         {"eggy_salute", -12},      {"eggy_medal", -10},
    {"eggy_ui_click", -19},   {"eggy_ui_key", -21},       {"eggy_fanfare", -6},      {"eggy_thunder_near", -5},
    {"eggy_thunder_far", -9}, {"eggy_tree_fall", -10},    {"eggy_ribbit", -20},      {"eggy_fish", -22},
    {"eggy_amb_wind", -12},   {"eggy_amb_brook", -12},    {"eggy_amb_forest", -12},  {"eggy_amb_rain", -12},
    {"eggy_amb_fire", -12},
};

const voice::Cue* cue_named(const std::string& name) {
    for (const auto table : {eggy::effect_cues(), eggy::music_cues()}) {
        for (const voice::Cue& cue : table) {
            if (cue.name == name) {
                return &cue;
            }
        }
    }
    return nullptr;
}

// A cue's master as planar doubles (left, then right, for a stereo one).
std::vector<double> planar(const voice::Cue& cue, double& ms) {
    const auto begin = std::chrono::steady_clock::now();
    std::vector<double> out{};
    if (cue.mono != nullptr) {
        out = cue.mono();
    } else {
        const voice::Stereo s = cue.stereo();
        out = s.left;
        out.insert(out.end(), s.right.begin(), s.right.end());
    }
    ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    return out;
}

bool finite(const std::vector<double>& x) {
    for (const double value : x) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

void self_tests() {
    require(eggy::effect_cues().size() == std::size(levels), "every effect and bed has a cue");
    double total = 0;
    for (const Level& level : levels) {
        const voice::Cue* cue = cue_named(level.name);
        require(cue != nullptr, std::string(level.name) + " has a cue");
        if (cue == nullptr) {
            continue;
        }
        double ms = 0;
        const std::vector<double> y = planar(*cue, ms);
        total += ms;
        const double expected = std::pow(10.0, level.peak_db / 20);
        const double top = voice::peak(y);
        std::printf("%-18s %8zu samples  %8.1f ms\n", level.name, y.size(), ms);
        require(finite(y) && !y.empty(), std::string(level.name) + " is finite");
        require(std::fabs(top - expected) <= expected * 1e-12, std::string(level.name) + " peaks at its master's level");
        double again_ms = 0;
        require(planar(*cue, again_ms) == y, std::string(level.name) + " renders the same every time");
    }
    std::printf("effects and beds: %.0f ms in all\n", total);
    eggy::register_sound_cues();
    require(voice::Bank::shared().find("eggy_ui_key").has_value(), "the cues reach the shell's bank");
}

void music_tests() {
    for (const voice::Cue& cue : eggy::music_cues()) {
        double ms = 0;
        const std::vector<double> y = planar(cue, ms);
        const double seconds = static_cast<double>(y.size() / 2) / voice::sample_rate;
        std::printf("%-16s %7.2f s  render %8.0f ms  doubles %6.1f MB  clip floats %6.1f MB\n", std::string(cue.name).c_str(),
                    seconds, ms, static_cast<double>(y.size() * sizeof(double)) / 1e6,
                    static_cast<double>(y.size() * sizeof(float)) / 1e6);
        require(cue.stereo != nullptr && finite(y) && y.size() > 0, std::string(cue.name) + " renders");
        require(voice::peak(y) <= std::pow(10.0, -3.0 / 20) * (1 + 1e-12) + 1e-12, std::string(cue.name) + " stays under -3 dBFS");
    }
}

// write_wav's quantisation: 24 bits rounded; 16 bits with its TPDF dither.
std::vector<long> quantise(const std::vector<double>& planar_x, bool stereo, int bits) {
    const std::size_t channels = stereo ? 2 : 1;
    const std::size_t frames = planar_x.size() / channels;
    std::vector<long> out(planar_x.size());
    voice::Generator a(1);
    voice::Generator b(2);
    for (std::size_t i = 0; i < frames; ++i) {
        for (std::size_t c = 0; c < channels; ++c) {
            const double v = std::clamp(planar_x[c * frames + i], -1.0, 1.0);
            if (bits == 24) {
                out[i * channels + c] = std::lround(std::nearbyint(v * 8388607));
            } else {
                const double first = a.random();
                const double tp = (first - b.random()) / 32767;
                out[i * channels + c] = std::lround(std::clamp(std::nearbyint((v + tp) * 32767), -32768.0, 32767.0));
            }
        }
    }
    return out;
}

void reference_tests(const std::string& directory) {
    const std::vector<voice::reference::Case> cases = voice::reference::read_manifest(directory);
    require(!cases.empty(), "the reference holds cues");
    std::printf("%-18s %9s %13s %10s %9s %8s %9s\n", "cue", "samples", "max err/peak", "error dB", "identical", "pcm",
                "render");
    for (const voice::reference::Case& c : cases) {
        const std::string name = c.args.empty() ? c.file : c.args[0];
        const int bits = c.args.size() > 1 ? std::stoi(c.args[1]) : 24;
        const bool stereo = c.kind == "stereo";
        double ms = 0;
        std::vector<double> ours{};
        const std::string suffix = "_prelufs";
        const bool prelufs = name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
        if (prelufs) {
            // a loop before the script's levelling, and ffmpeg's loudness reading of it
            const auto begin = std::chrono::steady_clock::now();
            const voice::Stereo m = eggy::render_master(name.substr(0, name.size() - suffix.size()));
            ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            ours = m.left;
            ours.insert(ours.end(), m.right.begin(), m.right.end());
            const double loudness = eggy::integrated_loudness(m);
            std::printf("%-18s integrated loudness %.4f LUFS, printed %.1f\n", name.c_str(), loudness, eggy::printed_loudness(loudness));
        } else {
            const voice::Cue* cue = cue_named(name);
            require(cue != nullptr && (stereo ? (*cue).stereo != nullptr : (*cue).mono != nullptr), "a cue for " + name);
            if (cue == nullptr || (stereo ? (*cue).stereo == nullptr : (*cue).mono == nullptr)) {
                continue;
            }
            ours = planar(*cue, ms);
        }
        const std::vector<double> reference = voice::reference::read_array<double>(directory, c.file);
        const voice::reference::Difference d = voice::reference::compare(ours, reference);
        std::size_t pcm_differences = reference.size();
        if (d.same_length()) {
            const std::vector<long> a = quantise(ours, stereo, bits);
            const std::vector<long> b = quantise(reference, stereo, bits);
            pcm_differences = 0;
            for (std::size_t i = 0; i < a.size(); ++i) {
                pcm_differences += a[i] != b[i] ? 1 : 0;
            }
        }
        const double share = 100.0 * static_cast<double>(d.identical) / static_cast<double>(d.reference_length);
        std::printf("%-18s %9zu %13.3g %10.1f %8.2f%% %8s %7.0f ms\n", name.c_str(), d.reference_length, d.relative,
                    d.error_db, share, pcm_differences == 0 ? "same" : std::to_string(pcm_differences).c_str(), ms);
        require(d.same_length() && d.relative <= 1e-9, name + " equals its master");
        // The intermediate pre-levelling WAV only feeds ffmpeg's loudness reading (checked by
        // the finished loop matching): a sample sitting within 1e-15 of a rounding boundary
        // may land one 24-bit step away there, as one of eggy_summit's 11 million does.
        require(pcm_differences == 0 || (prelufs && pcm_differences <= 2), name + " quantises to the same samples");
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--reference") {
        reference_tests(argv[2]);
    } else if (argc == 2 && std::string(argv[1]) == "--music") {
        music_tests();
    } else {
        self_tests();
    }
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
