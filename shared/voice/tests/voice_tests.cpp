// The live voice engine's tests (CTest voice_engine). Without arguments: the Fourier
// transforms against a direct DFT, NumPy's generators against numbers NumPy printed,
// every instrument and drum rendered and finite, the render caches and the Bank.
// With --reference DIR: every render tools/numpy_reference.py engine wrote into DIR is
// recomputed here and compared (random streams bit for bit, audio to 1e-9 of its peak).
#include "voice_bank.hpp"
#include "voice_fft.hpp"
#include "voice_random.hpp"
#include "voice_reference.hpp"
#include "voice_synth.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <optional>
#include <span>
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

double number(const std::string& text) {
    const double result = std::strtod(text.c_str(), nullptr);
    return result;
}

long long integer(const std::string& text) {
    const long long result = std::strtoll(text.c_str(), nullptr, 10);
    return result;
}

std::uint64_t unsigned_integer(const std::string& text) {
    const std::uint64_t result = std::strtoull(text.c_str(), nullptr, 10);
    return result;
}

std::optional<std::uint64_t> seed_argument(const std::string& text) {
    const long long value = integer(text);
    if (value < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(value);
}

voice::Wave wave_named(const std::string& name) {
    if (name == "flute") {
        return voice::Wave::flute;
    }
    if (name == "clar") {
        return voice::Wave::clar;
    }
    if (name == "reed") {
        return voice::Wave::reed;
    }
    return voice::Wave::saw;
}

void append(voice::Signal& out, const voice::Signal& part) {
    out.insert(out.end(), part.begin(), part.end());
}

void append(voice::Signal& out, const voice::Stereo& part) {
    append(out, part.left);
    append(out, part.right);
}

voice::Stereo plus(const voice::Stereo& a, const voice::Stereo& b) {
    voice::Stereo result = a;
    for (std::size_t i = 0; i < result.left.size(); ++i) {
        result.left[i] += b.left[i];
        result.right[i] += b.right[i];
    }
    return result;
}

// ---------------------------------------------------------------- scripted cases
// These replay numpy_reference.py's mixed_sequence, sequence_case and mix_case.
voice::Signal mixed_sequence(std::uint64_t seed) {
    voice::Generator g(seed);
    voice::Signal out{};
    const double weights[5] = {.25, .3, .25, .12, .08};
    const double notes[4] = {88, 91, 93, 95};
    for (int i = 0; i < 300; ++i) {
        out.push_back(g.random());
        out.push_back(static_cast<double>(g.integers(0, 10)));
        out.push_back(static_cast<double>(g.integers(0, 7)));
        out.push_back(g.standard_normal());
        out.push_back(static_cast<double>(g.integers(0, 1LL << 40)));
        out.push_back(g.uniform(-1, 1));
        out.push_back(g.exponential(1.5));
        out.push_back(static_cast<double>(g.integers(3, 4)));
        out.push_back(notes[g.choice(4)]);
        out.push_back(static_cast<double>(g.choice(weights)));
        out.push_back(g.normal(.4, .2));
        out.push_back(static_cast<double>(g.poisson(2.5)));
    }
    return out;
}

voice::Signal sequence_case() {
    voice::Synth s{};
    voice::Signal out{};
    append(out, s.render_note("pluck", 60, .5, .7));
    append(out, s.render_note("pad", 64, 1.0, .52));
    append(out, s.render_note("pluck", 60, .5, .7));
    append(out, s.render_note("flute", 72, .3, .62));
    append(out, s.noise(1000));
    append(out, s.render_drum("kick", .8, 5));
    append(out, s.render_drum("tap", .44, -3));
    append(out, s.render_note("slide", 67, .41, .9));
    append(out, s.kick(.7));
    append(out, s.render_drum("kick", .8, 1));
    append(out, s.render_note("marimba", 55, .2, .33));
    append(out, s.render_note("bass", 40, .6, .8));
    append(out, s.render_note("clarinet", 62, .7, .5));
    append(out, s.render_note("pluck", 60.0, .5, .7));
    return out;
}

voice::Signal mix_case() {
    voice::Synth s{};
    const std::size_t length = 48000;
    voice::LoopMix mix(length);
    mix.add("keys", s.render_note("ep", 60, .4, .7), 1000, -.4, .8);
    mix.add("keys", s.render_note("ep", 67, .9, .5), 40000, .3, .7);
    mix.add("drums", s.render_drum("hat", .5, 2), -500, .5, 1.0);
    voice::LoopMix straight(length, false);
    straight.add("a", s.render_note("celesta", 76, .3, .6), -2000, .2, .9);
    straight.add("a", s.render_note("vibes", 64, .5, .6), 30000, -.7, .5);
    const voice::Stereo ir = voice::make_ir(0.7, 2.0, 1.6, 0.8, 0.018, 7);
    const voice::Stereo wet = voice::conv_reverb(mix.bus("keys"), ir);
    const voice::Stereo tail = voice::conv_reverb(straight.bus("a"), ir, false);
    const voice::Stereo echo = voice::pingpong(plus(mix.bus("keys"), mix.bus("drums")), 7000, .4, 4, 3000);
    const voice::Stereo moved = voice::autopan(mix.bus("drums"), 2, .3);
    voice::Signal out{};
    append(out, ir);
    append(out, mix.bus("keys"));
    append(out, mix.bus("drums"));
    append(out, straight.bus("a"));
    append(out, wet);
    append(out, tail);
    append(out, echo);
    append(out, moved);
    return out;
}

// ---------------------------------------------------------------- self tests
void transform_tests() {
    const std::size_t sizes[] = {1, 2, 3, 4, 5, 7, 8, 12, 16, 30, 64, 67, 97, 128, 210, 1000, 4093};
    voice::Generator g(99);
    for (const std::size_t n : sizes) {
        std::vector<voice::Complex> x(n);
        for (voice::Complex& value : x) {
            value = voice::Complex(g.standard_normal(), g.standard_normal());
        }
        std::vector<voice::Complex> fast = x;
        voice::fft(fast, false);
        double worst = 0;
        double scale = 0;
        for (std::size_t k = 0; k < n; ++k) {
            voice::Complex direct(0, 0);
            for (std::size_t j = 0; j < n; ++j) {
                const double angle = -2 * voice::pi * static_cast<double>((j * k) % n) / static_cast<double>(n);
                direct += x[j] * voice::Complex(std::cos(angle), std::sin(angle));
            }
            worst = std::max(worst, std::abs(direct - fast[k]));
            scale = std::max(scale, std::abs(direct));
        }
        require(worst <= 1e-12 * scale, "fft of " + std::to_string(n) + " matches the direct DFT");
        std::vector<voice::Complex> back = fast;
        voice::fft(back, true);
        double round_trip = 0;
        for (std::size_t j = 0; j < n; ++j) {
            round_trip = std::max(round_trip, std::abs(back[j] / static_cast<double>(n) - x[j]));
        }
        require(round_trip < 1e-12, "inverse fft of " + std::to_string(n) + " restores it");
    }
    const std::size_t real_sizes[] = {1, 2, 3, 8, 9, 100, 101, 4093, 5096, 128896};
    for (const std::size_t n : real_sizes) {
        voice::Signal x(n);
        g.fill_standard_normal(x);
        std::vector<voice::Complex> spectrum{};
        voice::rfft(x, n, spectrum);
        std::vector<voice::Complex> full(n);
        for (std::size_t j = 0; j < n; ++j) {
            full[j] = voice::Complex(x[j], 0);
        }
        voice::fft(full, false);
        double worst = 0;
        for (std::size_t k = 0; k < spectrum.size(); ++k) {
            worst = std::max(worst, std::abs(spectrum[k] - full[k]));
        }
        require(worst < 1e-10 * std::sqrt(static_cast<double>(n)), "rfft of " + std::to_string(n) + " agrees with fft");
        voice::Signal back{};
        voice::irfft(spectrum, n, back);
        double round_trip = 0;
        for (std::size_t j = 0; j < n; ++j) {
            round_trip = std::max(round_trip, std::fabs(back[j] - x[j]));
        }
        require(round_trip < 1e-12, "irfft(rfft) of " + std::to_string(n) + " restores it");
    }
}

void random_tests() {
    voice::Generator g(12345);
    require(g.next_uint64() == 0x3a32b18db2ffc19dull && g.next_uint64() == 0x51171315c9e4c4deull &&
                g.next_uint64() == 0xcc2024823444efd9ull,
            "default_rng(12345) raw outputs");
    voice::Generator normal(12345);
    require(normal.standard_normal() == -0x1.6c7fcc2ecc744p+0 && normal.standard_normal() == 0x1.4383b54eb0649p+0 &&
                normal.standard_normal() == -0x1.bdc76014d359ep-1,
            "default_rng(12345).standard_normal(3)");
    voice::Generator zero(0);
    const double a = zero.random();
    const double b = zero.random();
    require(a == 0x1.461fd79fb3850p-1 && b == 0x1.1442f7e20b674p-2, "default_rng(0).random(2)");
    const std::int64_t expected[5] = {3, 0, 0, 0, 1};
    for (const std::int64_t value : expected) {
        require(zero.integers(0, 10) == value, "default_rng(0) integers after random()");
    }
    voice::Generator sheep(1977);
    require(sheep.uniform(0, .03) == 0x1.6f182b6482bd7p-12 && sheep.uniform(.6, 1) == 0x1.48dd87fceb70fp-1,
            "default_rng(1977).uniform");
    const voice::GeneratorState s = voice::Generator(12345).state();
    require(s.state.high == 0x1905e0335aae9634ull && s.state.low == 0x9199b0d09775add5ull &&
                s.increment.high == 0xc9c7353e6e2b1f28ull && s.increment.low == 0x7d761f2d4027fae7ull,
            "default_rng(12345) state matches bit_generator.state");
    voice::Generator stepped(7);
    for (int i = 0; i < 1000; ++i) {
        static_cast<void>(stepped.next_uint64());
    }
    voice::Generator jumped(7);
    jumped.advance(1000);
    require(stepped.next_uint64() == jumped.next_uint64(), "advance(1000) equals 1000 draws");
    voice::Generator copy(stepped.state());
    require(copy.standard_normal() == stepped.standard_normal(), "a generator restarts from its recorded state");
}

bool finite_and_sounding(const voice::Signal& y) {
    double top = 0;
    for (const double value : y) {
        if (!std::isfinite(value)) {
            return false;
        }
        top = std::max(top, std::fabs(value));
    }
    return !y.empty() && top > 1e-4;
}

voice::Signal test_cue() {
    const voice::Signal tone = voice::partials(440, 480, std::vector<double>{1}, std::vector<double>{1},
                                               std::vector<double>{.01});
    const voice::Signal result = voice::master(tone, -6);
    return result;
}

constexpr voice::Cue test_cues[] = {{.name = "voice_test_tone", .mono = test_cue}};

void engine_tests() {
    const char* const instruments[] = {"slide", "tuba", "ep", "pluck", "marimba", "vibes", "celesta", "harp",
                                       "pizz", "bass", "pad", "flute", "clarinet", "reed", "felt", "glass",
                                       "musicbox", "logdrum", "litho", "analog", "synthbass", "ping", "kalimba"};
    for (const char* name : instruments) {
        voice::Synth s{};
        require(voice::Synth::has_instrument(name), std::string(name) + " is an instrument");
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        const voice::Signal y = s.instrument(name, 64, .5, .7);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        require(finite_and_sounding(y), std::string(name) + " renders a finite, audible note");
        std::printf("  %-10s %6zu samples in %6.2f ms\n", name, y.size(), seconds * 1000);
    }
    const char* const drums[] = {"kick", "tap", "shaker", "hat", "rim", "clave", "wbhi", "wblo",
                                 "triangle", "clap", "stone", "rainstick", "tick", "logkick", "loghi"};
    for (const char* name : drums) {
        voice::Synth s{};
        require(voice::Synth::has_drum(name), std::string(name) + " is a drum");
        require(finite_and_sounding(s.render_drum(name, .7, 1)), std::string(name) + " renders a finite hit");
    }
    voice::Synth cached{};
    const voice::Signal& first = cached.render_note("pluck", 60, .5, .7);
    const std::uint64_t drawn = cached.rng().outputs();
    const voice::Signal& again = cached.render_note("pluck", 60, .503, .71);
    require(&first == &again && cached.rng().outputs() == drawn, "a quantised repeat note comes from the cache");
    voice::Synth primed{};
    const voice::GeneratorState before = primed.rng().state();
    voice::Synth later{};
    static_cast<void>(later.noise(10));
    later.prime_note("pluck", 60, .5, .7, before);
    require(later.render_note("pluck", 60, .5, .7) == primed.render_note("pluck", 60, .5, .7),
            "a primed note equals the note rendered at its recorded state");
    voice::Bank::shared().add(test_cues);
    voice::Bank::shared().add(test_cues);
    const std::optional<voice::Cue> cue = voice::Bank::shared().find("voice_test_tone");
    require(cue.has_value() && !voice::Bank::shared().find("no_such_cue").has_value(), "the bank finds its cues");
    require(voice::Bank::shared().names().size() == 1, "adding the same cues twice keeps one of each");
    const voice::Interleaved clip = cue.has_value() ? voice::render(*cue) : voice::Interleaved{};
    require(clip.size() == 960 && clip[100] == clip[101] && clip[100] != 0,
            "a mono cue becomes equal left and right channels");
}

// ---------------------------------------------------------------- reference comparison
struct Tally {
    int cases{};
    double worst_relative{};
    double worst_db{-400};
    std::size_t identical{};
    std::size_t samples{};
};

int compare_reference(const std::string& directory) {
    const std::vector<voice::reference::Case> cases = voice::reference::read_manifest(directory);
    require(!cases.empty(), "the reference directory has a manifest");
    std::map<std::string, Tally> tallies{};
    const double tolerance = 1e-9;
    for (const voice::reference::Case& c : cases) {
        const std::vector<std::string>& a = c.args;
        std::string label = c.kind;
        if (c.kind == "raw") {
            voice::Generator g(unsigned_integer(a[0]));
            const std::vector<std::uint64_t> expected = voice::reference::read_array<std::uint64_t>(directory, c.file);
            bool same = true;
            for (const std::uint64_t value : expected) {
                same = same && g.next_uint64() == value;
            }
            require(same, "raw stream of seed " + a[0]);
            tallies[label].cases += 1;
            continue;
        }
        if (c.kind == "integers" || c.kind == "poisson" || c.kind == "state") {
            voice::Generator g(unsigned_integer(a[0]));
            const bool words = c.kind == "state";
            std::vector<std::int64_t> ours{};
            if (c.kind == "integers") {
                for (long long i = 0; i < integer(a[3]); ++i) {
                    ours.push_back(g.integers(integer(a[1]), integer(a[2])));
                }
            } else if (c.kind == "poisson") {
                for (long long i = 0; i < integer(a[2]); ++i) {
                    ours.push_back(g.poisson(number(a[1])));
                }
            } else {
                voice::Signal skip(1001);
                g.fill_standard_normal(skip);
                static_cast<void>(g.integers(0, 10));
                const voice::GeneratorState s = g.state();
                const std::uint64_t state_words[6] = {s.state.high, s.state.low, s.increment.high, s.increment.low,
                                                      s.has_uint32 ? 1u : 0u, s.uinteger};
                for (const std::uint64_t w : state_words) {
                    ours.push_back(static_cast<std::int64_t>(w));
                }
            }
            const std::vector<std::int64_t> expected = voice::reference::read_array<std::int64_t>(directory, c.file);
            require(ours == expected, c.kind + " " + (words ? a[0] : a[0] + " " + a[1] + " " + a[2]) + " bit for bit");
            tallies[label].cases += 1;
            continue;
        }
        voice::Signal ours{};
        bool exact = false;
        if (c.kind == "normal" || c.kind == "random" || c.kind == "uniform" || c.kind == "exponential") {
            exact = true;
            voice::Generator g(unsigned_integer(a[0]));
            const long long count = integer(a[a.size() - 1]);
            for (long long i = 0; i < count; ++i) {
                if (c.kind == "normal") {
                    ours.push_back(g.standard_normal());
                } else if (c.kind == "random") {
                    ours.push_back(g.random());
                } else if (c.kind == "uniform") {
                    ours.push_back(g.uniform(number(a[1]), number(a[2])));
                } else {
                    ours.push_back(g.exponential(number(a[1])));
                }
            }
        } else if (c.kind == "mixed") {
            exact = true;
            ours = mixed_sequence(unsigned_integer(a[0]));
        } else if (c.kind == "sum") {
            exact = true;
            voice::Signal x(static_cast<std::size_t>(integer(a[1])));
            voice::Generator g(unsigned_integer(a[0]));
            g.fill_standard_normal(x);
            ours = {voice::sum(x), voice::standard_deviation(x), voice::sum(std::span<const double>(x).first(77)),
                    voice::standard_deviation(std::span<const double>(x).first(5000))};
        } else if (c.kind == "fftfilter") {
            const voice::Signal x = voice::seeded_noise(static_cast<std::size_t>(integer(a[1])), unsigned_integer(a[0]));
            ours = voice::fft_filter(x, number(a[2]), number(a[3]), static_cast<int>(integer(a[4])),
                                     static_cast<std::size_t>(integer(a[5])), integer(a[6]) != 0);
            label = "fft_filter";
        } else if (c.kind == "wt") {
            voice::Synth s{};
            const voice::Signal freq = voice::linspace(number(a[3]), number(a[4]), static_cast<std::size_t>(integer(a[5])));
            ours = s.wt(wave_named(a[0]), freq, static_cast<int>(integer(a[1])), number(a[2]));
            label = "wt " + a[0];
        } else if (c.kind == "instr") {
            voice::Synth s{};
            ours = s.instrument(a[0], number(a[1]), number(a[2]), number(a[3]));
            label = "instr " + a[0];
        } else if (c.kind == "drum") {
            voice::Synth s{};
            ours = s.drum(a[0], number(a[1]), seed_argument(a[2]));
            label = "drum " + a[0];
        } else if (c.kind == "renderdrum") {
            voice::Synth s{};
            ours = s.render_drum(a[0], number(a[1]), integer(a[2]));
            label = "drum " + a[0];
        } else if (c.kind == "swish") {
            voice::Synth s{};
            ours = s.brush_swish(number(a[0]), number(a[1]), seed_argument(a[2]));
            label = "drum brush_swish";
        } else if (c.kind == "sequence") {
            ours = sequence_case();
        } else if (c.kind == "mix") {
            ours = mix_case();
            label = "mix (LoopMix, make_ir, conv_reverb, pingpong, autopan)";
        } else {
            continue;
        }
        const std::vector<double> expected = voice::reference::read_array<double>(directory, c.file);
        const voice::reference::Difference d = voice::reference::compare(ours, expected);
        Tally& t = tallies[label];
        t.cases += 1;
        t.worst_relative = std::max(t.worst_relative, d.relative);
        t.worst_db = std::max(t.worst_db, d.error_db);
        t.identical += d.identical;
        t.samples += d.reference_length;
        std::string what = c.kind;
        for (const std::string& arg : a) {
            what += " " + arg;
        }
        require(d.same_length(), what + ": length " + std::to_string(d.ours_length) + " vs " +
                                     std::to_string(d.reference_length));
        if (exact) {
            require(d.identical == d.reference_length, what + " bit for bit");
        } else if (d.relative > tolerance) {
            require(false, what + ": max error " + std::to_string(d.relative) + " of peak");
        }
    }
    std::printf("%-58s %5s %12s %10s %9s\n", "reference", "cases", "max err/peak", "error dB", "identical");
    for (const std::pair<const std::string, Tally>& entry : tallies) {
        const Tally& t = entry.second;
        const double share = t.samples > 0 ? 100.0 * static_cast<double>(t.identical) / static_cast<double>(t.samples) : 100;
        std::printf("%-58s %5d %12.3g %10.1f %8.2f%%\n", entry.first.c_str(), t.cases, t.worst_relative, t.worst_db,
                    share);
    }
    return failures;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--reference") {
        compare_reference(argv[2]);
    } else {
        transform_tests();
        random_tests();
        engine_tests();
    }
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
