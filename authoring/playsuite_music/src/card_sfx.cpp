// Card handling sounds for the card games: soft, close-miked paper on felt.
// Every sound is built from three physical parts: the dry rasp of card stock (band-limited
// noise), the air a card pushes as it turns (a swept band of noise), and the muffled thump of
// a card or stack landing on a baize table (a low, quickly damped body plus dull noise).
// Attacks are a few milliseconds long and the top end is rolled off, so nothing clicks.
#include "mix.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

namespace ps {
namespace {

struct Clip {
    Stereo audio;
    explicit Clip(double seconds) : audio(std::size_t(seconds * SR)) {}
    std::size_t frames() const {
        return audio.size();
    }
};

// Attack-decay envelope with a raised-cosine attack (no corners).
double envelope(double t, double attack, double decay) {
    if (t < 0)
        return 0;
    if (t < attack)
        return 0.5 - 0.5 * std::cos(PI * t / attack);
    return std::exp(-(t - attack) / decay);
}

// Card stock rubbing: noise through a band that can move from `f0` to `f1`.
void rasp(Clip& c, Rng& rng, double start, double length, double f0, double f1, double q,
          double gain, double attack, double decay, double pan) {
    SVF band;
    OnePole soften;
    soften.set(6500);
    double l = 0, r = 0;
    panGains(pan, l, r);
    const std::size_t a = std::size_t(start * SR), n = std::size_t(length * SR);
    for (std::size_t i = 0; i < n && a + i < c.frames(); ++i) {
        const double t = double(i) / SR, k = std::min(1.0, t / std::max(1e-3, length * 0.8));
        band.set(f0 * std::pow(f1 / f0, k), q);
        band.tick(rng.bip());
        const double x = soften.lp(band.bp) * gain * envelope(t, attack, decay);
        c.audio.add(a + i, x * l, x * r);
    }
}

// A card or stack landing on felt: a damped low body that drops in pitch, plus dull noise.
void thump(Clip& c, Rng& rng, double start, double pitch, double gain, double decay, double pan) {
    OnePole dull, dull2;
    dull.set(700);
    dull2.set(900);
    double l = 0, r = 0, phase = 0;
    panGains(pan, l, r);
    const std::size_t a = std::size_t(start * SR), n = std::size_t((decay * 7 + 0.01) * SR);
    for (std::size_t i = 0; i < n && a + i < c.frames(); ++i) {
        const double t = double(i) / SR;
        const double f = pitch * (0.72 + 0.28 * std::exp(-t / 0.018));
        phase += TAU * f / SR;
        const double body = std::sin(phase) * envelope(t, 0.0025, decay);
        const double cloth = dull2.lp(dull.lp(rng.bip())) * envelope(t, 0.0015, decay * 0.45);
        const double x = (body * 0.8 + cloth * 2.2) * gain;
        c.audio.add(a + i, x * l, x * r);
    }
}

// A single tiny flick of a card edge, as in a riffle.
void flick(Clip& c, Rng& rng, double start, double gain, double pan) {
    rasp(c, rng, start, 0.03, rng.range(2200, 3200), rng.range(3000, 4400), 1.4, gain, 0.0012,
         0.0045, pan);
}

struct Variation {
    Rng rng;
    double pitch, speed, pan;
    explicit Variation(int seed, int index)
        : rng(0xC0FFEEull * std::uint64_t(seed) + std::uint64_t(index) * 7919ull),
          pitch(1.0 + (index - 2.5) * 0.045), speed(1.0 + (index % 2 ? 0.06 : -0.05)),
          pan((index - 2.5) * 0.06) {}
};

Clip pickup(int index) {
    Variation v(11, index);
    Clip c(0.32);
    // The card slides off its neighbor and lifts: a short rising rasp, almost no impact.
    rasp(c, v.rng, 0.004, 0.16 * v.speed, 1300 * v.pitch, 2600 * v.pitch, 0.9, 0.55, 0.012, 0.045,
         v.pan);
    rasp(c, v.rng, 0.01, 0.07, 600 * v.pitch, 900 * v.pitch, 0.7, 0.25, 0.006, 0.02, v.pan);
    return c;
}

Clip place(int index) {
    Variation v(23, index);
    Clip c(0.38);
    // A short slide on arrival, the soft thump of the card meeting the cloth, and a settle.
    rasp(c, v.rng, 0.0, 0.06 * v.speed, 2400 * v.pitch, 1600 * v.pitch, 0.9, 0.32, 0.008, 0.02,
         v.pan);
    thump(c, v.rng, 0.038, 135 * v.pitch, 0.5, 0.026, v.pan);
    rasp(c, v.rng, 0.045, 0.09, 900 * v.pitch, 700 * v.pitch, 0.8, 0.14, 0.004, 0.03, v.pan);
    return c;
}

Clip flip(int index) {
    Variation v(37, index);
    Clip c(0.34);
    // The card turns over: air swept up through the band, then it lies down flat.
    rasp(c, v.rng, 0.0, 0.085 * v.speed, 700 * v.pitch, 3400 * v.pitch, 1.1, 0.5, 0.03, 0.03,
         v.pan - 0.05);
    rasp(c, v.rng, 0.07 * v.speed, 0.03, 3000 * v.pitch, 2200 * v.pitch, 1.3, 0.22, 0.002, 0.008,
         v.pan + 0.05);
    thump(c, v.rng, 0.09 * v.speed, 150 * v.pitch, 0.32, 0.02, v.pan);
    return c;
}

Clip foundation(int index) {
    Variation v(41, index);
    Clip c(0.42);
    // Squared neatly onto a pile: a crisper slide, a firmer tap and a little bounce.
    rasp(c, v.rng, 0.0, 0.05 * v.speed, 2800 * v.pitch, 2000 * v.pitch, 1.0, 0.36, 0.006, 0.016,
         v.pan);
    thump(c, v.rng, 0.034, 160 * v.pitch, 0.62, 0.03, v.pan);
    thump(c, v.rng, 0.085, 175 * v.pitch, 0.16, 0.018, v.pan);
    rasp(c, v.rng, 0.036, 0.02, 3600 * v.pitch, 3200 * v.pitch, 1.6, 0.12, 0.0015, 0.005, v.pan);
    return c;
}

Clip deal(int index) {
    Variation v(53, index);
    Clip c(0.3);
    // Dealt from the hand: a quick skim through the air and a light landing.
    rasp(c, v.rng, 0.0, 0.07 * v.speed, 1800 * v.pitch, 3000 * v.pitch, 1.0, 0.4, 0.01, 0.025,
         v.pan);
    thump(c, v.rng, 0.06 * v.speed, 145 * v.pitch, 0.36, 0.02, v.pan);
    return c;
}

Clip shuffle(int index) {
    Variation v(67, index);
    Clip c(1.45);
    // Riffle: two halves fall together edge by edge, quick in the middle, then the deck is
    // bridged and squared on the table.
    const int cards = 46;
    for (int i = 0; i < cards; ++i) {
        const double u = double(i) / (cards - 1);
        const double t = 0.03 + 0.62 * v.speed * (u - 0.18 * std::sin(PI * u) / PI);
        const double side = (i % 2 ? 1 : -1) * 0.22 + v.pan;
        flick(c, v.rng, t + v.rng.range(-0.002, 0.002), 0.28 * (0.75 + 0.5 * v.rng.uni()), side);
    }
    rasp(c, v.rng, 0.02, 0.66 * v.speed, 1500, 2100, 0.6, 0.06, 0.08, 0.4, v.pan);
    // Bridge: cards cascade back down in a soft purr.
    rasp(c, v.rng, 0.72 * v.speed, 0.28, 2600 * v.pitch, 1500 * v.pitch, 0.8, 0.28, 0.03, 0.09,
         v.pan);
    thump(c, v.rng, 1.06 * v.speed, 120 * v.pitch, 0.5, 0.032, v.pan);
    thump(c, v.rng, 1.16 * v.speed, 128 * v.pitch, 0.3, 0.022, v.pan);
    return c;
}

// Peak-normalise, remove DC, fade the last few milliseconds and trim trailing silence.
void finish(Clip& c, double peak_db) {
    Stereo& s = c.audio;
    double mean_l = 0, mean_r = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        mean_l += s.L[i];
        mean_r += s.R[i];
    }
    mean_l /= double(s.size());
    mean_r /= double(s.size());
    // Gentle high-pass to keep the thumps clear of rumble.
    OnePole hl, hr;
    hl.set(45);
    hr.set(45);
    double peak = 1e-9;
    for (std::size_t i = 0; i < s.size(); ++i) {
        s.L[i] = float(hl.hp(s.L[i] - mean_l));
        s.R[i] = float(hr.hp(s.R[i] - mean_r));
        peak = std::max({peak, std::fabs(double(s.L[i])), std::fabs(double(s.R[i]))});
    }
    const double gain = db(peak_db) / peak;
    const std::size_t fade = std::size_t(0.008 * SR);
    for (std::size_t i = 0; i < s.size(); ++i) {
        double g = gain;
        if (i + fade >= s.size())
            g *= 0.5 + 0.5 * std::cos(PI * double(i + fade - s.size() + 1) / double(fade));
        if (i == 0)
            g = 0;
        s.L[i] = float(s.L[i] * g);
        s.R[i] = float(s.R[i] * g);
    }
}

} // namespace

int renderCardSfx(const std::string& out, const std::set<std::string>& want) {
    const struct {
        const char* stem;
        const char* title;
        Clip (*make)(int);
        int count;
        double peak_db;
    } kinds[] = {{"card_pickup", "Card lifted from felt", pickup, 4, -18},
                 {"card_place", "Card laid on felt", place, 4, -20},
                 {"card_flip", "Card turned over", flip, 4, -20.5},
                 {"card_foundation", "Card squared onto a pile", foundation, 4, -15.5},
                 {"card_deal", "Card dealt to the table", deal, 4, -20},
                 {"card_shuffle", "Riffle shuffle and bridge", shuffle, 2, -19.5}};
    int rendered = 0;
    for (const auto& kind : kinds)
        for (int i = 1; i <= kind.count; ++i) {
            const std::string id =
                std::string(kind.stem) + (i < 10 ? "_0" : "_") + std::to_string(i);
            if (!want.empty() && !want.count(id))
                continue;
            Clip clip = kind.make(i);
            finish(clip, kind.peak_db);
            Report rep;
            rep.id = id;
            rep.title = kind.title;
            rep.loop = false;
            rep.samples = long(clip.frames());
            rep.seconds = double(clip.frames()) / SR;
            rep.lufs = integratedLufs(clip.audio);
            rep.true_peak_dbtp = truePeakDb(clip.audio);
            double peak = 0, dc = 0;
            for (std::size_t n = 0; n < clip.frames(); ++n)
                peak = std::max(
                    {peak, std::fabs(double(clip.audio.L[n])), std::fabs(double(clip.audio.R[n]))});
            rep.peak_dbfs = todb(peak);
            double sum_l = 0, sum_r = 0;
            for (std::size_t n = 0; n < clip.frames(); ++n) {
                sum_l += clip.audio.L[n];
                sum_r += clip.audio.R[n];
            }
            dc = std::max(std::fabs(sum_l), std::fabs(sum_r)) / double(clip.frames());
            rep.dc_max = dc;
            rep.start_abs = std::fabs(double(clip.audio.L.front()));
            rep.end_abs = std::fabs(double(clip.audio.L.back()));
            rep.file = id + ".wav";
            writeWav24(out + "/" + id + ".wav", clip.audio, 0);
            writeF32(out + "/" + id + ".f32", clip.audio);
            std::ofstream(out + "/" + id + ".json") << reportJson(rep) << "\n";
            std::cout << reportJson(rep) << "\n";
            ++rendered;
        }
    return rendered;
}

} // namespace ps
