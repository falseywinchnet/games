#include "instruments.hpp"

namespace games::music {
namespace {
// A sine at `hz` on the voice's oscillator `k`.
double osc(Voice& v, int k, double hz, double dt) noexcept {
    v.phase[k] += hz * dt;
    v.phase[k] -= std::floor(v.phase[k]);
    return std::sin(2 * pi * v.phase[k]);
}
double noise(Random& random) noexcept { return random.uniform() * 2 - 1; }
}  // namespace

double play(Voice& v, Random& random, double dt) noexcept {
    v.age += dt;
    const double a = v.age;
    double out = 0;
    // a short note is damped when its length is up
    const double damp = v.length > 0 && a > v.length ? std::exp(-(a - v.length) / .04) : 1;
    switch (v.kind) {
    case Instrument::marimba: case Instrument::comp_marimba: {
        out = (osc(v, 0, v.hz, dt) * std::exp(-a / .5) + .3 * osc(v, 1, v.hz * 3.99, dt) * std::exp(-a / .07) + .08 * osc(v, 2, v.hz * 9.9, dt) * std::exp(-a / .015) +
               noise(random) * std::exp(-a / .001) * .12) *
              std::min(1.0, a / .002) * damp;
        if (a > 1.6 || damp < 1e-3) v.on = false;
        break;
    }
    case Instrument::bass_marimba: {
        const double s = osc(v, 0, v.hz, dt) * std::exp(-a / .55) + .25 * osc(v, 1, v.hz * 4.0, dt) * std::exp(-a / .05) + .3 * osc(v, 2, v.hz * 2, dt) * std::exp(-a / .25);
        v.lp += (s - v.lp) * lowpass_coefficient(900);
        out = v.lp * std::min(1.0, a / .004) * damp;
        if (a > 2.5 || damp < 1e-3) v.on = false;
        break;
    }
    case Instrument::squeeze: {
        v.phase[0] += v.hz * dt;
        v.phase[1] += v.hz * 1.0045 * dt;
        double s = 0;
        for (int r = 0; r < 2; ++r) {
            v.phase[r] -= std::floor(v.phase[r]);
            const double p = 2 * pi * v.phase[r];
            s += std::sin(p) + .5 * std::sin(2 * p) + .35 * std::sin(3 * p) + .2 * std::sin(4 * p) + .12 * std::sin(5 * p);
        }
        const double target = a < v.length ? 1 : 0;
        v.env += (target - v.env) * (target > v.env ? dt / .02 : dt / .05) * 3;
        v.lp += (s - v.lp) * lowpass_coefficient(2800);
        out = v.lp * .5 * v.env * (1 + .04 * std::sin(2 * pi * 5.5 * a));
        if (a > v.length + .3) v.on = false;
        break;
    }
    case Instrument::kick: {
        const double hz = 45 + 55 * std::exp(-a / .03);
        out = osc(v, 0, hz, dt) * std::exp(-a / .18);
        if (a > .5) v.on = false;
        break;
    }
    case Instrument::rim: {
        out = (osc(v, 0, 1650, dt) * std::exp(-a / .012) + .6 * osc(v, 1, 820, dt) * std::exp(-a / .02) + noise(random) * std::exp(-a / .003) * .4);
        if (a > .12) v.on = false;
        break;
    }
    case Instrument::hat: {
        const double n = noise(random);
        v.lp += (n - v.lp) * .55;
        out = (n - v.lp) * std::exp(-a / .018);
        if (a > .1) v.on = false;
        break;
    }
    case Instrument::shaker: {
        const double n = noise(random);
        v.lp += (n - v.lp) * .35;
        out = (n - v.lp) * std::exp(-a / .03) * std::min(1.0, a / .005);
        if (a > .2) v.on = false;
        break;
    }
    case Instrument::conga: {
        const double hz = v.hz * (.92 + .08 * std::exp(-a / .02));
        out = (osc(v, 0, hz, dt) + .3 * osc(v, 1, hz * 1.52, dt)) * std::exp(-a / .14) + noise(random) * std::exp(-a / .003) * .35;
        if (a > .7) v.on = false;
        break;
    }
    case Instrument::bird: {
        const double len = .4;
        const double w = std::sin(pi * std::min(1.0, a / len));
        out = osc(v, 0, v.hz * (1 + .22 * std::sin(2 * pi * 9 * a) + .15 * a / len), dt) * w * w;
        if (a > len) v.on = false;
        break;
    }
    }
    return out * v.vel;
}

}  // namespace games::music
