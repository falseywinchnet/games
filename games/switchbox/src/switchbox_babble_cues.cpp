#include "switchbox_babble_cues.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// make_sfx.py line for line, in numpy's order of operations. Its write() normalises to
// the peak, fades the last 4 ms out and has no fade-in: voice::master(x, peak, .004, 0).
namespace sbx {
namespace {

constexpr double sr = voice::sample_rate;

// onepole_lp(x, fc): s = (1 - a) v + a s
voice::Signal onepole_lp(const voice::Signal& x, double fc) {
    const double a = std::exp(-2 * voice::pi * fc / sr);
    voice::Signal y(x.size());
    double s = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        s = (1 - a) * x[i] + a * s;
        y[i] = s;
    }
    return y;
}

// clap(seed): three quick bursts make the slap of two palms, then a short body
voice::Signal clap(std::uint64_t seed) {
    voice::Generator r(seed);
    const std::size_t n = voice::samples(.16);
    const voice::Signal t = voice::tarr(n);
    voice::Signal x(n, 0.0);
    constexpr std::array<std::array<double, 2>, 3> bursts{{{0, .6}, {.006, .8}, {.013, 1.0}}};
    for (std::size_t k = 0; k < bursts.size(); ++k) {
        const std::size_t i = voice::samples(bursts[k][0]);
        const double g = bursts[k][1];
        const std::size_t m = n - i;
        const double tau = k < 2 ? .004 : .03;
        voice::Signal burst(m);
        r.fill_standard_normal(burst);
        for (std::size_t j = 0; j < m; ++j) {
            const double env = std::exp(-static_cast<double>(j) / sr / tau);
            x[i + j] += g * burst[j] * env;
        }
    }
    const voice::Signal bright = onepole_lp(x, 3200);
    const voice::Signal body = onepole_lp(x, 500);
    voice::Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = bright[i] - .6 * body[i];
        y[i] = y[i] * (1 - std::exp(-t[i] / .0006));
    }
    return y;
}

struct Formants {
    char vowel;
    int first;
    int second;
};
constexpr std::array<Formants, 6> formant_table{
    {{'a', 850, 1250}, {'i', 320, 2400}, {'u', 350, 900}, {'e', 500, 2000}, {'o', 520, 950}, {'y', 420, 1900}}};

// syllable(vowel, f0, glide, dur): harmonics of a gliding pitch weighted by two formants
voice::Signal syllable(char vowel, int f0, double glide, double dur) {
    Formants formants{};
    for (const Formants& entry : formant_table) {
        if (entry.vowel == vowel) {
            formants = entry;
        }
    }
    const std::size_t n = voice::samples(dur);
    const voice::Signal t = voice::tarr(n);
    voice::Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double u = t[i] / dur;
        f[i] = f0 * (1 + glide * u - .5 * glide * (u * u));
    }
    const voice::Signal cs = voice::cumulative_sum(f);
    voice::Signal ph(n);
    for (std::size_t i = 0; i < n; ++i) {
        ph[i] = 2 * voice::pi * cs[i] / sr;
    }
    voice::Signal x(n, 0.0);
    for (int h = 1; h < 14; ++h) {
        const int fh = f0 * h;
        if (fh > 5000) {
            break;
        }
        // sum(generator) starts from the integer 0, then .08 / h is added
        double a = 0;
        for (const int formant : {formants.first, formants.second}) {
            const double z = (fh - formant) / (formant * .35);
            a = a + std::exp(-(z * z));
        }
        a = a + .08 / h;
        for (std::size_t i = 0; i < n; ++i) {
            x[i] += a * std::sin(h * ph[i]);
        }
    }
    voice::Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double env = std::min(1.0, t[i] / .012) * std::exp(-std::max(0.0, t[i] - dur * .45) / (dur * .25));
        const double vib = 1 + .03 * std::sin(2 * voice::pi * 11 * t[i]);
        y[i] = x[i] * env * vib;
    }
    return y;
}

voice::Signal write(const voice::Signal& x, double peak_db) {
    const voice::Signal result = voice::master(x, peak_db, .004, 0);
    return result;
}

voice::Signal clap_cue() {
    return write(clap(20), -9);
}

struct Babble {
    char vowel;
    int f0;
    double glide;
};
constexpr std::array<Babble, 6> babbles{
    {{'a', 560, .25}, {'i', 640, -.15}, {'u', 520, .1}, {'e', 600, .3}, {'o', 540, -.2}, {'y', 680, .15}}};

template <std::size_t I>
voice::Signal babble() {
    const Babble& b = babbles[I];
    return write(syllable(b.vowel, b.f0, b.glide, .11 + .02 * static_cast<double>(I % 3)), -12);
}

constexpr voice::Cue cues[] = {
    {.name = "switchbox_clap", .mono = clap_cue},        {.name = "switchbox_babble_01", .mono = babble<0>},
    {.name = "switchbox_babble_02", .mono = babble<1>}, {.name = "switchbox_babble_03", .mono = babble<2>},
    {.name = "switchbox_babble_04", .mono = babble<3>}, {.name = "switchbox_babble_05", .mono = babble<4>},
    {.name = "switchbox_babble_06", .mono = babble<5>},
};

}  // namespace

std::span<const voice::Cue> babble_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

void register_babble_cues() {
    voice::Bank::shared().add(cues);
}

}  // namespace sbx
