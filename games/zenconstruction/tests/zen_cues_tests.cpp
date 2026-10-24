// Rock Stack's live sounds (CTest rockstack_cues). Without arguments: every cue renders
// at its master's length, channels and level and reaches the shell's bank. With
// --reference DIR (repeatable; games/penthesheep/tools/music_reference.py --script
// games/zenconstruction/audio_src/make_sfx.py or stone_garden.py --out DIR): every cue
// equals the NumPy float master to 1e-9 of its peak, and the 16-bit file the script
// wrote from it (synth.write_wav's dithered path) comes out the same. With --time: the
// render time and the memory kept for each loop.
#include "zen_cues.hpp"
#include "voice_reference.hpp"

#include <chrono>
#include <cmath>
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
    int channels;
    std::size_t frames;
    double peak_db;
};

// The masters' shapes and the levels make_sfx.py and stone_garden.py wrote them at.
constexpr Expected expected[] = {
    {"zc_clip", 2, 58560, -5},       {"zc_unclip", 2, 64800, -6},     {"zc_knock_1", 2, 36480, -4},
    {"zc_knock_2", 2, 36480, -4},    {"zc_knock_3", 2, 40800, -4},    {"zc_tumble", 2, 153600, -4},
    {"zc_whoosh", 2, 86400, -8},     {"zc_land", 2, 62400, -5},       {"zc_reset", 2, 163200, -5},
    {"zc_click", 1, 3840, -10},      {"zc_relay", 2, 57600, -9},      {"zc_clunk", 2, 86400, -6},
    {"zc_best", 2, 153600, -4},      {"zc_squeak", 2, 72000, -7},     {"zc_bird_1", 2, 144000, -6},
    {"zc_bird_2", 2, 124800, -6},    {"zc_bird_3", 2, 129600, -6},    {"zc_motor", 2, 96000, -3},
    {"zc_brook", 2, 1920000, -3},    {"zc_rush", 2, 1776000, -3},     {"zc_reeds", 2, 2544000, -3},
    {"zc_splash_1", 2, 67200, -5},   {"zc_splash_2", 2, 67200, -5},   {"zc_splash_3", 2, 67200, -5},
    {"zc_splash_4", 2, 67200, -5},   {"zc_bed_1", 2, 76800, -6},      {"zc_bed_2", 2, 76800, -6},
    {"zc_bed_3", 2, 76800, -6},      {"zc_frog_1", 2, 86400, -6},     {"zc_frog_2", 2, 86400, -6},
    {"zc_frog_3", 2, 100800, -6},    {"zc_music", 2, 3072000, -3},
};

const voice::Cue* cue_named(const std::string& name) {
    for (const voice::Cue& cue : zc::sound_cues()) {
        if (cue.name == name) {
            return &cue;
        }
    }
    return nullptr;
}

// The master channel after channel (left then right), as music_reference.py stores it.
std::vector<double> flat(const voice::Cue& cue, int& channels) {
    std::vector<double> out{};
    if (cue.mono != nullptr) {
        out = cue.mono();
        channels = 1;
    } else {
        const voice::Stereo s = cue.stereo();
        out = s.left;
        out.insert(out.end(), s.right.begin(), s.right.end());
        channels = 2;
    }
    return out;
}

// The 16-bit samples the script wrote: `trunc` is (x * 32767).astype('<i2'); `dither`
// is synth.write_wav(bits=16) on frames x channels in C order.
std::vector<std::int16_t> quantise(const std::vector<double>& planar, int channels, const std::string& quantiser) {
    const std::size_t frames = planar.size() / static_cast<std::size_t>(channels);
    std::vector<std::int16_t> out(planar.size());
    voice::Generator first(1);
    voice::Generator second(2);
    for (std::size_t f = 0; f < frames; ++f) {
        for (int c = 0; c < channels; ++c) {
            const double x = planar[static_cast<std::size_t>(c) * frames + f];
            const std::size_t at = f * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c);
            if (quantiser == "dither") {
                const double tp = (first.random() - second.random()) / 32767;
                const double q = std::nearbyint((x + tp) * 32767);
                out[at] = static_cast<std::int16_t>(std::fmin(std::fmax(q, -32768.0), 32767.0));
            } else {
                out[at] = static_cast<std::int16_t>(x * 32767);
            }
        }
    }
    return out;
}

void self_tests() {
    require(zc::sound_cues().size() == std::size(expected), "every sound has a cue");
    for (const Expected& e : expected) {
        const voice::Cue* cue = cue_named(e.name);
        require(cue != nullptr, std::string(e.name) + " has a cue");
        if (cue == nullptr) {
            continue;
        }
        require(((*cue).mono != nullptr) == (e.channels == 1) && ((*cue).stereo != nullptr) == (e.channels == 2),
                std::string(e.name) + " has its master's channels");
        int channels = 0;
        const std::vector<double> y = flat(*cue, channels);
        bool finite = true;
        for (const double value : y) {
            finite = finite && std::isfinite(value);
        }
        const double level = std::pow(10.0, e.peak_db / 20);
        require(y.size() == e.frames * static_cast<std::size_t>(e.channels), std::string(e.name) + " has its master's length");
        require(finite && voice::peak(y) <= level * (1 + 1e-12), std::string(e.name) + " peaks at its master's level");
        require(finite && voice::peak(y) > 0.5 * level, std::string(e.name) + " is audible");
        if (e.frames < 500000) {
            int again = 0;
            require(flat(*cue, again) == y, std::string(e.name) + " renders the same every time");
        }
    }
    zc::register_sound_cues();
    require(voice::Bank::shared().find("zc_frog_2").has_value(), "the cues reach the shell's bank");
    const voice::Interleaved clip = voice::render(*voice::Bank::shared().find("zc_click"));
    require(clip.size() == 2 * 3840, "a mono clip is interleaved stereo");
}

void reference_tests(const std::string& directory, int& seen) {
    const std::vector<voice::reference::Case> cases = voice::reference::read_manifest(directory);
    require(!cases.empty(), "the reference " + directory + " holds cues");
    for (const voice::reference::Case& c : cases) {
        if (c.kind != "music" || c.args.size() < 4) {
            continue;
        }
        const std::string& name = c.args[0];
        const voice::Cue* cue = cue_named(name);
        require(cue != nullptr, "a cue for " + name);
        if (cue == nullptr) {
            continue;
        }
        ++seen;
        int channels = 0;
        const std::vector<double> ours = flat(*cue, channels);
        const std::vector<double> reference = voice::reference::read_array<double>(directory, c.file);
        require(channels == std::stoi(c.args[1]), name + " has the reference's channels");
        const voice::reference::Difference d = voice::reference::compare(ours, reference);
        const std::vector<std::int16_t> ours16 = quantise(ours, channels, c.args[3]);
        const std::vector<std::int16_t> reference16 = quantise(reference, channels, c.args[3]);
        std::size_t pcm_differences = ours16.size() == reference16.size() ? 0 : reference16.size();
        for (std::size_t i = 0; i < ours16.size() && i < reference16.size(); ++i) {
            pcm_differences += ours16[i] != reference16[i] ? 1 : 0;
        }
        const double share = 100.0 * static_cast<double>(d.identical) / static_cast<double>(d.reference_length);
        std::printf("%-12s %9zu %13.3g %10.1f %8.2f%% %9s\n", name.c_str(), d.reference_length, d.relative, d.error_db,
                    share, pcm_differences == 0 ? "same" : std::to_string(pcm_differences).c_str());
        require(d.same_length() && d.relative <= 1e-9, name + " equals its NumPy master");
        require(pcm_differences == 0, name + " quantises to the script's 16-bit file");
    }
}

void timing() {
    for (const char* name : {"zc_motor", "zc_brook", "zc_rush", "zc_reeds", "zc_music"}) {
        const voice::Cue* cue = cue_named(name);
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        const voice::Interleaved clip = voice::render(*cue);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const double megabytes = static_cast<double>(clip.size() * sizeof(float)) / 1e6;
        std::printf("%-10s %7.2f s of audio rendered in %6.2f s, %6.1f MB kept\n", name,
                    static_cast<double>(clip.size()) / 2 / voice::sample_rate, seconds, megabytes);
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> references{};
    bool time = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--reference" && i + 1 < argc) {
            references.emplace_back(argv[++i]);
        } else if (arg == "--time") {
            time = true;
        }
    }
    if (time) {
        timing();
    } else if (!references.empty()) {
        std::printf("%-12s %9s %13s %10s %9s %9s\n", "cue", "samples", "max err/peak", "error dB", "identical", "pcm16");
        int seen = 0;
        for (const std::string& directory : references) {
            reference_tests(directory, seen);
        }
        require(seen == static_cast<int>(std::size(expected)), "the references hold every cue");
    } else {
        self_tests();
    }
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
