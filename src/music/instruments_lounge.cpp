// The lounge bands' instruments: the vibraphone, the pans, the pianos, the guitars, the
// basses, the organ, the winds, the steel guitar, the accordion and the hand percussion;
// and the written songs' synthesizers, cymbals and toms.
// Struck and plucked ones are sums of partials, each falling at its own rate (set at the
// note's first sample, then one multiply a sample); blown and bowed ones are oscillators
// through soft filters, held for the note's length and let go.
#include "instruments.hpp"

#include <algorithm>
#include <cmath>

namespace games::music {
namespace {

const SineTable sine_table{};

// The voice's oscillator `k` at `hz`.
double sine(Voice& v, int k, double hz, double dt) noexcept {
    v.phase[k] += hz * dt;
    v.phase[k] -= std::floor(v.phase[k]);
    return sine_table.at(v.phase[k]);
}
double noise(Random& random) noexcept { return random.uniform() * 2 - 1; }
// A one-pole low-pass's coefficient for `hz`, cheaply (no exp): good to a few kHz.
double pole(double hz) noexcept {
    const double w = 2 * pi * hz / sample_rate;
    return w / (1 + w);
}
// Partial `k`: `ratio` times the note, starting at `level` and falling by e in `seconds`.
void ring(Voice& v, int k, double ratio, double level, double seconds, double dt) noexcept {
    v.ratio[k] = ratio;
    v.amp[k] = level;
    v.fall[k] = seconds > 0 ? std::exp(-dt / seconds) : 1.0;
}
// The first `count` partials at the note's pitch `hz`.
double partials(Voice& v, int count, double hz, double dt) noexcept {
    double sum = 0;
    for (int k = 0; k < count; ++k) {
        sum += v.amp[k] * sine(v, k, hz * v.ratio[k], dt);
        v.amp[k] *= v.fall[k];
    }
    return sum;
}
// A struck or plucked note's damper: once its length is up it dies away over `seconds`.
double damper(const Voice& v, double seconds) noexcept {
    return v.length > 0 && v.age > v.length ? std::exp(-(v.age - v.length) / seconds) : 1.0;
}
// A held note's envelope: up over `attack` while the note lasts, down over `release`.
double held(Voice& v, double attack, double release, double dt) noexcept {
    const double target = v.age < v.length ? 1.0 : 0.0;
    v.env += (target - v.env) * std::min(1.0, dt / (target > v.env ? attack : release));
    return v.env;
}
// A plucked string's partials: struck at `where` along it, the upper partials falling
// sooner, a hair sharp as they go up (stiffness).
void pluck(Voice& v, int count, double where, double tilt, double ring_seconds, double spread, double dt) noexcept {
    for (int n = 1; n <= count; ++n) {
        const double level = std::fabs(std::sin(n * pi * where)) / std::pow(n, tilt);
        ring(v, n - 1, n * (1 + .00008 * n * n), level, ring_seconds / (1 + spread * (n - 1)), dt);
    }
}

constexpr double first_sample = 1.5;  // the first sample: age at most this many dt

const double noise_low_1k = pole(1000), noise_low_2k = pole(2000), noise_low_3k = pole(3200), noise_low_5k = pole(5000),
             noise_low_7k = pole(7000), tone_2k4 = pole(2400), tone_3k = pole(3000), tone_1k5 = pole(1500);

}  // namespace

double play_lounge(Voice& v, Random& random, double dt) noexcept {
    v.age += dt;
    const double a = v.age;
    const bool first = a <= dt * first_sample;
    double out = 0;
    switch (v.kind) {
    case Instrument::vibes: {
        if (first) {
            ring(v, 0, 1, 1, 2.6, dt);
            ring(v, 1, 4, .22, .32, dt);
            ring(v, 2, 10, .05, .05, dt);
            v.aux = .1;
        }
        const double motor = 1 - .3 * (.5 + .5 * sine_table.at(wrap(5.3 * a)));
        out = (partials(v, 3, v.hz, dt) + noise(random) * v.aux) * std::min(1.0, a / .0015) * motor * damper(v, .35);
        v.aux *= .985;
        if (a > 6 || damper(v, .35) < 1e-3) v.on = false;
        break;
    }
    case Instrument::glock: {
        if (first) {
            ring(v, 0, 1, 1, 1.0, dt);
            ring(v, 1, 2.76, .45, .35, dt);
            ring(v, 2, 5.4, .22, .12, dt);
            ring(v, 3, 8.93, .08, .05, dt);
            v.aux = .15;
        }
        out = (partials(v, 4, v.hz, dt) + noise(random) * v.aux) * std::min(1.0, a / .0008);
        v.aux *= .97;
        if (a > 3) v.on = false;
        break;
    }
    case Instrument::steel_pan: {
        if (first) {
            ring(v, 0, 1, 1, 1.1, dt);
            ring(v, 1, 2, .55, .65, dt);
            ring(v, 2, 3, .3, .32, dt);
            ring(v, 3, 4.02, .08, .15, dt);
            v.aux = .012;  // struck a little sharp, settling ("bwong")
            v.env = .25;
        }
        v.lp += (noise(random) - v.lp) * .2;
        out = (partials(v, 4, v.hz * (1 + v.aux), dt) * std::min(1.0, a / .001) + v.lp * v.env) * damper(v, .08);
        v.aux *= .9983;
        v.env *= .99;
        if (a > 3 || damper(v, .08) < 1e-3) v.on = false;
        break;
    }
    case Instrument::piano: {
        if (first) {
            // stretched partials, the hammer an eighth along the string; two strings a
            // breath apart beat slowly against each other
            const double stretch = std::min(.002, .0004 * std::pow(v.hz / 261.6, 1.5));
            const double sustain = std::clamp(3.2 * std::pow(261.6 / v.hz, .55), .5, 7.0);
            for (int n = 1; n <= 7; ++n) {
                const double level = std::fabs(std::sin(n * pi / 8)) / std::pow(n, .9) * (n > 2 ? .5 + .6 * v.vel : 1);
                ring(v, n - 1, n * std::sqrt(1 + stretch * n * n), level, sustain / (1 + .55 * (n - 1)), dt);
            }
            ring(v, 7, 1.0007, .45, sustain * 1.4, dt);
            v.aux = .25 * v.vel;
        }
        v.lp += (noise(random) - v.lp) * .12;
        out = (partials(v, 8, v.hz, dt) * .55 + v.lp * v.aux) * std::min(1.0, a / .0015) * damper(v, .12);
        v.aux *= .993;
        if (a > 8 || damper(v, .12) < 1e-3) v.on = false;
        break;
    }
    case Instrument::guitar: case Instrument::ukulele: {
        const bool uke = v.kind == Instrument::ukulele;
        if (first) {
            if (uke) pluck(v, 7, .25, 1.15, std::clamp(1.0 * std::pow(392 / v.hz, .35), .25, 1.6), .6, dt);
            else pluck(v, 8, .18, 1.05, std::clamp(2.2 * std::pow(196 / v.hz, .4), .4, 4.0), .5, dt);
            v.aux = .06;
        }
        out = (partials(v, uke ? 7 : 8, v.hz, dt) * .7 + noise(random) * v.aux) * std::min(1.0, a / .0012) * damper(v, uke ? .06 : .09);
        v.aux *= .986;
        if (a > 6 || damper(v, uke ? .06 : .09) < 1e-3) v.on = false;
        break;
    }
    case Instrument::upright: {
        if (first) {
            const double sustain = std::clamp(1.5 * std::pow(55 / v.hz, .2), .8, 2.0);
            const double levels[5] = {1, .55, .28, .14, .06};
            for (int n = 0; n < 5; ++n) ring(v, n, n + 1, levels[n], sustain / (1 + .9 * n), dt);
            v.aux = .01;  // the pluck pulls it a touch sharp
            v.env = .15;
        }
        v.lp += (noise(random) - v.lp) * .05;
        out = (partials(v, 5, v.hz * (1 + v.aux), dt) + v.lp * v.env) * std::min(1.0, a / .003) * damper(v, .08);
        v.aux *= .9985;
        v.env *= .993;
        if (a > 4 || damper(v, .08) < 1e-3) v.on = false;
        break;
    }
    case Instrument::electric_bass: {
        if (first) {
            const double levels[5] = {1, .6, .35, .2, .1};
            const double seconds[5] = {3.0, 1.8, 1.0, .6, .35};
            for (int n = 0; n < 5; ++n) ring(v, n, n + 1, levels[n], seconds[n], dt);
        }
        out = partials(v, 5, v.hz, dt) * std::min(1.0, a / .002) * damper(v, .06);
        if (a > 6 || damper(v, .06) < 1e-3) v.on = false;
        break;
    }
    case Instrument::electric_piano: {
        if (first) {
            v.amp[0] = 1;
            v.fall[0] = std::exp(-dt / std::clamp(2.2 * std::pow(261.6 / v.hz, .35), .8, 4.5));
            v.amp[1] = .08 + .28 * v.vel;  // the bark: the modulation's depth, in cycles
            v.fall[1] = std::exp(-dt / .45);
            v.amp[2] = .05;  // the tine's bell
            v.fall[2] = std::exp(-dt / .03);
        }
        const double m = sine(v, 1, v.hz, dt);
        v.phase[0] += v.hz * dt;
        v.phase[0] -= std::floor(v.phase[0]);
        const double c = sine_table.at(wrap(v.phase[0] + v.amp[1] * m));
        const double tine = v.hz * 14 < sample_rate * .45 ? sine(v, 2, v.hz * 14, dt) : 0;
        out = (c * v.amp[0] + tine * v.amp[2]) * std::min(1.0, a / .001) * damper(v, .2);
        v.amp[0] *= v.fall[0];
        v.amp[1] *= v.fall[1];
        v.amp[2] *= v.fall[2];
        if (a > 7 || damper(v, .2) < 1e-3) v.on = false;
        break;
    }
    case Instrument::organ: {
        if (first) {
            const double ratios[6] = {.5, 1, 1.5, 2, 3, 4};
            const double levels[6] = {.4, .85, .45, .4, .2, .15};
            for (int k = 0; k < 6; ++k) ring(v, k, ratios[k], levels[k], 0, dt);
            ring(v, 6, 3, .3, .22, dt);  // percussion: the third harmonic's ping
            v.aux = .12;                 // the key click
        }
        const double leslie = sine_table.at(wrap(6.4 * a + .25 * v.pan));
        const double env = held(v, .006, .03, dt);
        out = (partials(v, 7, v.hz * (1 + .0011 * leslie), dt) * .35 * (1 + .1 * leslie) + noise(random) * v.aux) * env;
        v.aux *= .99;
        if (a > v.length + .3) v.on = false;
        break;
    }
    case Instrument::brass: {
        if (first) {
            v.aux = -.018;  // the lips find the note from a little below
            v.amp[0] = 1;   // the bright edge as it speaks
            v.fall[0] = std::exp(-dt / .25);
        }
        const double hz = v.hz * (1 + v.aux);
        const double s = saw(v.phase[0], hz * .996 * dt) + saw(v.phase[1], hz * dt) + saw(v.phase[2], hz * 1.004 * dt);
        const double env = held(v, .02, .06, dt);
        const double c = pole(500 + 3800 * v.vel * (.45 + .55 * v.amp[0]) * env);
        v.lp += (s - v.lp) * c;
        v.lp2 += (v.lp - v.lp2) * c;
        out = v.lp2 * env * .33;
        v.aux *= .9975;
        v.amp[0] *= v.fall[0];
        if (a > v.length + .4) v.on = false;
        break;
    }
    case Instrument::sax: {
        if (first) v.aux = -.04;  // scooping up into the note
        const double vibrato = .004 * std::clamp((a - .3) / .4, 0.0, 1.0) * sine_table.at(wrap(5.2 * a));
        const double s = saw(v.phase[0], v.hz * (1 + v.aux + vibrato) * dt);
        const double env = held(v, .045, .08, dt);
        const double c = pole(700 + 2200 * env * (.5 + .6 * v.vel));
        v.lp += (s - v.lp) * c;
        v.lp2 += (v.lp - v.lp2) * c;
        v.lp3 += (noise(random) - v.lp3) * noise_low_3k;
        out = (v.lp2 + v.lp3 * .12) * env * .6;
        v.aux *= .9985;
        if (a > v.length + .5) v.on = false;
        break;
    }
    case Instrument::clarinet: {
        if (first) {
            const double ratios[5] = {1, 3, 5, 7, 2};
            const double levels[5] = {1, .32, .18, .1, .04};
            for (int k = 0; k < 5; ++k) ring(v, k, ratios[k], levels[k], 0, dt);
        }
        const double vibrato = .0025 * std::clamp((a - .4) / .4, 0.0, 1.0) * sine_table.at(wrap(5.5 * a));
        const double env = held(v, .03, .05, dt);
        const double s = partials(v, 5, v.hz * (1 + vibrato), dt);
        v.lp += (s - v.lp) * tone_3k;
        v.lp3 += (noise(random) - v.lp3) * noise_low_2k;
        out = (v.lp + v.lp3 * .05) * env * .55;
        if (a > v.length + .4) v.on = false;
        break;
    }
    case Instrument::lap_steel: {
        if (first) {
            const double levels[4] = {1, .45, .25, .12};
            const double seconds[4] = {4, 2.2, 1.2, .6};
            for (int k = 0; k < 4; ++k) ring(v, k, k + 1, levels[k], seconds[k], dt);
            v.aux = v.length > .35 ? -2 : -.5;  // semitones below: the bar slides up into the note
        }
        const double vibrato = .006 * std::clamp((a - .35) / .5, 0.0, 1.0) * sine_table.at(wrap(4.8 * a));
        out = partials(v, 4, v.hz * std::exp2(v.aux / 12) * (1 + vibrato), dt) * std::min(1.0, a / .006) * damper(v, .15);
        v.aux *= .99974;
        if (a > 6 || damper(v, .15) < 1e-3) v.on = false;
        break;
    }
    case Instrument::musette: {
        // three reeds: one true, one sharp and one flat, so they shimmer against each other
        const double s = saw(v.phase[0], v.hz * dt) + saw(v.phase[1], v.hz * 1.0075 * dt) + saw(v.phase[2], v.hz * .9942 * dt);
        const double env = held(v, .03, .07, dt);
        v.lp += (s - v.lp) * tone_2k4;
        v.lp2 += (v.lp - v.lp2) * tone_2k4;
        out = v.lp2 * env * .35;
        if (a > v.length + .4) v.on = false;
        break;
    }
    case Instrument::brush_sweep: {
        const double len = v.length > 0 ? v.length : .4;
        const double n = noise(random);
        v.lp += (n - v.lp) * noise_low_7k;
        v.lp2 += (n - v.lp2) * noise_low_2k;
        const double swell = std::sin(pi * std::min(1.0, a / len));
        out = (v.lp - v.lp2) * swell * .5;
        if (a > len) v.on = false;
        break;
    }
    case Instrument::brush_tap: {
        if (first) v.aux = 1;
        const double n = noise(random);
        v.lp += (n - v.lp) * noise_low_5k;
        v.lp2 += (n - v.lp2) * noise_low_1k;
        out = ((v.lp - v.lp2) + .3 * sine(v, 0, 200, dt) * v.aux * v.aux) * v.aux;
        v.aux *= .99958;
        if (a > .35) v.on = false;
        break;
    }
    case Instrument::snare: {
        if (first) {
            v.aux = 1;
            v.env = 1;
        }
        const double n = noise(random);
        v.lp += (n - v.lp) * tone_1k5;
        out = sine(v, 0, 185, dt) * v.env * .6 + (n - v.lp) * v.aux * .7;
        v.env *= .99958;
        v.aux *= .99985;
        if (a > .5) v.on = false;
        break;
    }
    case Instrument::ride: {
        if (first) {
            ring(v, 0, 465, .25, .35, dt);
            ring(v, 1, 643, .18, .3, dt);
            ring(v, 2, 912, .12, .25, dt);
            ring(v, 3, 1427, .08, .2, dt);
            v.aux = .4;
        }
        const double n = noise(random);
        v.lp += (n - v.lp) * noise_low_5k;
        out = partials(v, 4, 1, dt) + (n - v.lp) * v.aux;
        v.aux *= .99996;
        if (a > 1.5) v.on = false;
        break;
    }
    case Instrument::clave: {
        if (first) {
            ring(v, 0, 2450, 1, .02, dt);
            ring(v, 1, 4900, .2, .01, dt);
        }
        out = partials(v, 2, 1, dt);
        if (a > .15) v.on = false;
        break;
    }
    case Instrument::cowbell: {
        if (first) {
            ring(v, 0, 562, 1, .09, dt);
            ring(v, 1, 845, .8, .08, dt);
            ring(v, 2, 1686, .2, .05, dt);
            ring(v, 3, 2535, .15, .04, dt);
        }
        out = partials(v, 4, 1, dt) * .6;
        if (a > .5) v.on = false;
        break;
    }
    case Instrument::guiro: {
        const double len = v.length > 0 ? v.length : .14;
        const double n = noise(random);
        v.lp += (n - v.lp) * noise_low_5k;
        v.lp2 += (n - v.lp2) * noise_low_2k;
        const double tooth = wrap(38 * a) < .35 ? 1.0 : .15;  // the stick catching each ridge
        out = (v.lp - v.lp2) * tooth * std::min(1.0, (len - a) / .02) * .8;
        if (a > len) v.on = false;
        break;
    }
    case Instrument::timbale: {
        if (first) {
            ring(v, 0, 780, 1, .25, dt);
            ring(v, 1, 1210, .6, .12, dt);
            ring(v, 2, 1730, .25, .06, dt);
            v.aux = .5;
        }
        v.lp += (noise(random) - v.lp) * noise_low_3k;
        out = (partials(v, 3, 1, dt) * .6 + v.lp * v.aux) * .8;
        v.aux *= .998;
        if (a > .8) v.on = false;
        break;
    }
    case Instrument::clap: {
        const double n = noise(random);
        v.lp += (n - v.lp) * noise_low_3k;
        v.lp2 += (n - v.lp2) * noise_low_1k;
        // three hands a few milliseconds apart, then the room's short tail
        double hands = 0;
        for (int k = 0; k < 3; ++k) {
            const double t = a - k * .009;
            if (t > 0) hands += std::exp(-t / .006);
        }
        out = (v.lp - v.lp2) * (hands + .35 * std::exp(-a / .07)) * .9;
        if (a > .4) v.on = false;
        break;
    }
    case Instrument::synth_lead: case Instrument::synth_bass: {
        const bool bass = v.kind == Instrument::synth_bass;
        if (first) {
            v.phase[2] = .5;  // the square: two saws half a cycle apart, subtracted
            v.amp[0] = 1;     // the filter's opening as the note speaks
            v.fall[0] = std::exp(-dt / (bass ? .12 : .2));
        }
        const double vibrato = bass ? 0 : .003 * std::clamp((a - .3) / .3, 0.0, 1.0) * sine_table.at(wrap(5.6 * a));
        const double hz = v.hz * (1 + vibrato);
        const double s = saw(v.phase[0], hz * dt) + .5 * saw(v.phase[1], hz * 1.003 * dt) + .5 * (saw(v.phase[2], hz * dt) - saw(v.phase[3], hz * dt));
        const double env = held(v, bass ? .003 : .006, bass ? .04 : .07, dt);
        const double c = bass ? pole(180 + 1500 * v.amp[0] * (.4 + .6 * v.vel)) : pole(600 + 3600 * (.35 + .65 * v.amp[0]) * (.5 + .5 * v.vel));
        // four poles, a little resonance
        v.lp += (s - .4 * v.lp4 - v.lp) * c;
        v.lp2 += (v.lp - v.lp2) * c;
        v.lp3 += (v.lp2 - v.lp3) * c;
        v.lp4 += (v.lp3 - v.lp4) * c;
        out = v.lp4 * env * (bass ? .8 : .5);
        v.amp[0] *= v.fall[0];
        if (a > v.length + .3) v.on = false;
        break;
    }
    case Instrument::open_hat: {
        if (first) v.aux = 1;
        const double n = noise(random);
        v.lp += (n - v.lp) * noise_low_7k;
        out = (n - v.lp) * v.aux * damper(v, .02);
        v.aux *= .99992;
        if (a > 1.2 || damper(v, .02) < 1e-3) v.on = false;
        break;
    }
    case Instrument::crash: {
        if (first) {
            ring(v, 0, 3170, .1, 1.0, dt);
            ring(v, 1, 4890, .08, .8, dt);
            ring(v, 2, 6430, .06, .6, dt);
            v.aux = 1;
        }
        const double n = noise(random);
        v.lp += (n - v.lp) * noise_low_5k;
        out = ((n - v.lp) * .8 + partials(v, 3, 1, dt)) * v.aux * std::min(1.0, a / .002);
        v.aux *= .999985;
        if (a > 4) v.on = false;
        break;
    }
    case Instrument::tom: {
        if (first) {
            v.aux = 1;
            v.amp[0] = 1;
            v.fall[0] = std::exp(-dt / .22);
        }
        const double hz = v.hz * (1 + .25 * v.aux);
        v.lp += (noise(random) - v.lp) * noise_low_3k;
        out = (sine(v, 0, hz, dt) + .3 * sine(v, 1, hz * 1.5, dt)) * v.amp[0] + v.lp * v.aux * v.aux * .4;
        v.aux *= .99948;
        v.amp[0] *= v.fall[0];
        if (a > .6) v.on = false;
        break;
    }
    default: v.on = false; break;
    }
    return out * v.vel;
}

}  // namespace games::music
