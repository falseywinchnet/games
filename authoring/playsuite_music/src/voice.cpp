#include "voice.hpp"
#include <cmath>

namespace ps {

namespace {
inline double expDecay(double t, double tau) {
    return std::exp(-t / std::max(tau, 1e-4));
}
inline double ratio(double cents) {
    return std::pow(2.0, cents / 1200.0);
}
inline double vcurve(double v, double e) {
    return std::pow(clamp(v, 0.0, 1.0), e);
}
inline double sel(double v, double def) {
    return v >= 0 ? v : def;
}
struct ModalPartial {
    double ratio, amp, t60;
};
} // namespace

bool Voice::finished() const {
    switch (p_.inst) {
    case Inst::Brass:
    case Inst::Clav:
    case Inst::Organ:
    case Inst::Strings:
    case Inst::Pad:
    case Inst::Choir:
    case Inst::FunkBass:
    case Inst::SlapBass:
    case Inst::SubBass:
    case Inst::SynthBass:
    case Inst::PluckSynth:
    case Inst::Lead:
    case Inst::Flute:
    case Inst::Clarinet:
    case Inst::MutedTrumpet:
    case Inst::Whistle:
    case Inst::GlideBass:
    case Inst::Water:
    case Inst::Air:
        return amp_.done();
    case Inst::EP:
    case Inst::FMBell:
    case Inst::BellLead:
    case Inst::Glock:
    case Inst::Celesta:
    case Inst::MusicBox:
    case Inst::Vibes:
    case Inst::Marimba:
    case Inst::Timpani:
    case Inst::Kalimba:
    case Inst::Guitar:
    case Inst::MutedGuitar:
    case Inst::Harp:
    case Inst::Pizz:
    case Inst::UprightBass:
    case Inst::Piano:
        return t_ > 0.05 && quietFor_ > 0.03;
    default:
        return t_ > aux_[7];
    }
}

double Voice::maxTail() const {
    switch (p_.inst) {
    case Inst::Harp:
    case Inst::Timpani:
    case Inst::Vibes:
    case Inst::Glock:
    case Inst::FMBell:
        return 9.0;
    case Inst::Piano:
    case Inst::EP:
        return 6.0;
    default:
        return 7.0;
    }
}

void Voice::setupModal() {
    std::vector<ModalPartial> table;
    double T = 1.0, ramp = 0.001, noise = 0.0;
    switch (p_.inst) {
    case Inst::Glock:
        table = {{1, 1, 2.8}, {2.76, .32, .9}, {5.40, .16, .45}, {8.93, .07, .25}};
        ramp = .0006;
        noise = .04;
        break;
    case Inst::Celesta:
        table = {{1, 1, 1.7}, {1.0008, .35, 2.3}, {4.0, .10, .35}, {2.0, .05, .5}};
        ramp = .002;
        noise = .02;
        break;
    case Inst::MusicBox:
        table = {{1, 1, 1.9}, {1.0013, .3, 1.5}, {6.27, .16, .25}, {17.5, .04, .06}};
        ramp = .0005;
        noise = .04;
        break;
    case Inst::Vibes:
        table = {{1, 1, 4.5}, {4.0, .22, 1.3}, {10.0, .05, .3}};
        ramp = .0028;
        noise = .012;
        break;
    case Inst::Kalimba:
        table = {{1, 1, 2.2}, {5.9, .14, .18}, {11.3, .05, .06}};
        ramp = .0006;
        noise = .03;
        break;
    case Inst::Marimba: {
        T = clamp(1.7 * std::pow(130.0 / f_, 0.5), 0.3, 2.2);
        table = {{1, 1, T}, {3.93, .28, T * .22}, {9.2, .07, T * .07}};
        ramp = .0016;
        noise = .045;
        T = 1.0;
        break;
    }
    case Inst::Timpani:
        table = {{1, 1, 3.2},      {1.5, .6, 2.4},   {1.98, .4, 1.9},
                 {2.44, .28, 1.5}, {2.96, .16, 1.0}, {3.5, .09, .7}};
        ramp = .003;
        noise = .12;
        break;
    default:
        break;
    }
    np_ = 0;
    double v = vcurve(vel_, 1.3);
    double hardness = 0.6 + 0.8 * vel_ * p_.bright; // harder mallets excite upper partials
    for (std::size_t i = 0; i < table.size() && np_ < MAXP; ++i) {
        double fr = f_ * table[i].ratio;
        if (fr > 19000)
            continue;
        ph_[np_].set(fr, rng_.uni() * TAU * (i ? 1.0 : 0.0));
        double a = table[i].amp * (i ? std::min(hardness, 1.4) : 1.0);
        pa1_[np_] = a * v;
        pd1_[np_] = std::exp(-6.9 / (table[i].t60 * T * p_.decay * SR));
        ppan_[np_] = (i ? rng_.bip() * 0.3 * p_.width : 0.0);
        ++np_;
    }
    aux_[0] = ramp;
    aux_[1] = noise * v * (0.5 + p_.bright);
    op1_.set(p_.inst == Inst::Timpani ? 700 : 3500);
}

void Voice::setupKS(double t60, double bright, int excitation) {
    double P = SR / f_;
    long N = long(std::floor(P - 1.0 - 0.1));
    if (N < 2)
        N = 2;
    double frac = P - 1.0 - double(N);
    ksN_ = std::size_t(N);
    ksApC_ = (1.0 - frac) / (1.0 + frac);
    ksApZ_ = 0;
    ksLast_ = 0;
    ksLp_ = lerp(0.245, 0.03, clamp(bright, 0.0, 1.0));
    // loop filter magnitude at f0 for the 3-tap filter: (1-2a) + 2a cos(w)
    double w = TAU * f_ / SR;
    double hmag = (1.0 - 2.0 * ksLp_) + 2.0 * ksLp_ * std::cos(w);
    double g = std::pow(10.0, -3.0 / (t60 * f_));
    ksG_ = std::min(g / std::max(hmag, 0.5), 0.99995);
    std::size_t L = ksN_ + 3;
    ks_.assign(L, 0.0);
    ksW_ = 0;
    // excitation fills the samples about to be read
    std::vector<double> ex(L, 0.0);
    OnePole lp;
    lp.set(400 + bright * 9000);
    double prev = 0;
    for (std::size_t i = 0; i < L; ++i) {
        double x = rng_.bip();
        if (excitation == 1) { // soft pluck: half-sine + noise
            double pos = double(i) / double(L);
            x = std::sin(PI * clamp(pos / 0.45, 0.0, 1.0)) * 1.4 + 0.25 * x;
        }
        x = lp.lp(x);
        ex[i] = x;
        prev = x;
    }
    (void)prev;
    // pick-position comb
    std::size_t pp = std::max<std::size_t>(1, std::size_t(double(L) * 0.13));
    for (std::size_t i = L - 1; i >= pp; --i)
        ex[i] -= 0.6 * ex[i - pp];
    double mean = 0;
    for (double x : ex)
        mean += x;
    mean /= double(L);
    double peak = 1e-9;
    for (double& x : ex) {
        x -= mean;
        peak = std::max(peak, std::fabs(x));
    }
    for (std::size_t i = 0; i < L; ++i)
        ks_[i] = ex[i] / peak;
    ksW_ = 0; // reading position (w - N) wraps around into the excitation
    ksDampG_ = 1.0;
}

double Voice::ksTick() {
    std::size_t L = ks_.size();
    std::size_t i0 = (ksW_ + L - ksN_) % L, i1 = (i0 + L - 1) % L, i2 = (i1 + L - 1) % L;
    double lp = ksLp_ * ks_[i0] + (1.0 - 2.0 * ksLp_) * ks_[i1] + ksLp_ * ks_[i2];
    double y = ksApC_ * lp + ksApZ_ - ksApC_ * ksLast_;
    ksApZ_ = lp;
    ksLast_ = undenorm(y);
    double out = undenorm(y * ksG_ * ksDampG_);
    ks_[ksW_] = out;
    ksW_ = (ksW_ + 1) % L;
    return out;
}

void Voice::setupPiano() {
    double v = clamp(vel_, 0.05, 1.0);
    double T = clamp(13.0 * std::pow(110.0 / f_, 0.55), 1.2, 16.0) * p_.decay;
    double B = 6e-5 * (1.0 + f_ / 400.0);
    double tilt = 1.75 - 0.95 * v * (0.4 + 0.6 * p_.bright);
    np_ = 0;
    double norm = 0;
    int maxk = 22;
    double detune = (0.6 + rng_.uni() * 0.5) * 0.0006;
    for (int s = 0; s < 2; ++s) {
        for (int k = 1; k <= maxk && np_ < MAXP; ++k) {
            double fk =
                k * f_ * std::sqrt(1.0 + B * k * k) * (s ? 1.0 + detune : 1.0 - detune * 0.3);
            if (fk > 16000)
                break;
            double hammer = 0.3 + 0.7 * std::fabs(std::sin(PI * k / 7.6));
            double a = std::pow(double(k), -tilt) * hammer * (s ? 0.85 : 1.0);
            double Tk = T / (1.0 + 0.09 * (k - 1) * std::sqrt(double(k)));
            ph_[np_].set(fk, rng_.uni() * 0.3);
            pa1_[np_] = a * 0.55; // prompt sound
            pa2_[np_] = a * 0.45; // aftersound
            pd1_[np_] = std::exp(-6.9 / (Tk * 0.18 * SR));
            pd2_[np_] = std::exp(-6.9 / (Tk * SR));
            ppan_[np_] = 0;
            norm += a * a;
            ++np_;
        }
    }
    double g = 0.3 * std::pow(v, 1.5) / std::sqrt(norm);
    for (int i = 0; i < np_; ++i) {
        pa1_[i] *= g;
        pa2_[i] *= g;
    }
    aux_[1] = 0.05 * v * (0.4 + p_.bright); // hammer noise
    op1_.set(1500 + 3000 * v);
    dampCoef_ = std::exp(-6.9 / (0.14 * SR));
    damp_ = 1.0;
}

void Voice::noteOn(double midi, double vel, double dur) {
    midi_ = target_ = midi;
    f_ = mtof(midi);
    vel_ = vel;
    dur_ = dur;
    t_ = 0;
    tOff_ = -1;
    released_ = false;
    n_ = 0;
    env_ = 0;
    quietFor_ = 0;
    for (int i = 0; i < 8; ++i) {
        osc_[i].ph = rng_.uni();
        det_[i] = 1.0;
        opan_[i] = 0;
        aux_[i] = 0;
    }
    fm1_ = fm2_ = fm3_ = 0;
    lfo_ = rng_.uni();
    lfo2_ = rng_.uni();
    scoop_ = 0;
    damp_ = 1.0;
    dampCoef_ = 1.0;
    const Patch& p = p_;
    switch (p.inst) {
    case Inst::Brass: {
        const double c[4] = {-9, 0.5, 8, -3};
        for (int i = 0; i < 4; ++i) {
            det_[i] = ratio((c[i] + rng_.bip() * 1.5) * p.detune);
            opan_[i] = (i == 0 ? -0.7 : i == 2 ? 0.7 : i == 3 ? -0.2 : 0.2) * p.width;
        }
        amp_.start(sel(p.attack, 0.014), 0.35, 0.8, sel(p.release, 0.11));
        fenv_.start(sel(p.attack, 0.014) * 2.5 + 0.02, 0.45, 0.5, 0.15);
        scoop_ = -0.32;
        break;
    }
    case Inst::EP: {
        double T = clamp(5.5 * std::pow(220.0 / f_, 0.5), 0.9, 8.0) * p.decay;
        pd1_[0] = std::exp(-6.9 / (T * SR));
        pa1_[0] = 1.0;
        dampCoef_ = std::exp(-6.9 / (sel(p.release, 0.3) * SR));
        break;
    }
    case Inst::Clav: {
        ks_.assign(4096, 0.0);
        ksW_ = 0;
        amp_.start(0.001, 0.6 * p.decay, 0.35, sel(p.release, 0.035));
        break;
    }
    case Inst::Organ: {
        amp_.start(sel(p.attack, 0.004), 0.1, 1.0, sel(p.release, 0.035));
        static const double ratios[9] = {0.5, 1.5, 1, 2, 3, 4, 5, 6, 8};
        static const double regs[3][9] = {{0.8, 0.3, 1, 0.5, 0, 0, 0, 0, 0},
                                          {0.7, 0.5, 1, 0.8, 0.4, 0.6, 0, 0.2, 0.5},
                                          {0, 0, 1, 0.45, 0, 0.15, 0, 0, 0}};
        const double* reg = regs[int(clamp(p.param, 0, 2))];
        np_ = 0;
        double norm = 0;
        for (int i = 0; i < 9; ++i) {
            if (reg[i] <= 0 || f_ * ratios[i] > 16000)
                continue;
            ph_[np_].set(f_ * ratios[i], rng_.uni() * TAU);
            pa1_[np_] = reg[i];
            norm += reg[i] * reg[i];
            ++np_;
        }
        for (int i = 0; i < np_; ++i)
            pa1_[i] *= 0.16 / std::sqrt(norm);
        break;
    }
    case Inst::Strings:
    case Inst::Pad: {
        const int nv = 6;
        for (int i = 0; i < nv; ++i) {
            double spread = (double(i) / (nv - 1)) * 2.0 - 1.0;
            det_[i] =
                ratio((spread * (p.inst == Inst::Pad ? 9.0 : 12.0) + rng_.bip() * 2.0) * p.detune);
            opan_[i] = spread * p.width * (i % 2 ? -1 : 1);
            aux_[i] = rng_.uni() * TAU;
        }
        amp_.start(sel(p.attack, p.inst == Inst::Pad ? 0.7 : 0.28), 0.6, 0.9,
                   sel(p.release, p.inst == Inst::Pad ? 1.2 : 0.5));
        break;
    }
    case Inst::Choir: {
        for (int i = 0; i < 4; ++i) {
            det_[i] = ratio((i - 1.5) * 6.0 * p.detune + rng_.bip() * 2);
            aux_[i] = rng_.uni() * TAU;
        }
        amp_.start(sel(p.attack, 0.45), 0.5, 0.9, sel(p.release, 0.9));
        double v = clamp(p.param, 0, 1);
        svf2_.set(lerp(730, 320, v), 7);
        svf3_.set(lerp(1090, 870, v), 9);
        svf4_.set(lerp(2440, 2240, v), 11);
        aux_[5] = lerp(0.5, 0.3, v);
        aux_[6] = lerp(0.28, 0.1, v);
        break;
    }
    case Inst::FMBell:
    case Inst::BellLead: {
        double T = (p.inst == Inst::FMBell ? 4.0 : 1.8) * p.decay *
                   clamp(std::pow(523.0 / f_, 0.3), 0.5, 1.6);
        pd1_[0] = std::exp(-6.9 / (T * SR));
        pa1_[0] = 1.0;
        dampCoef_ = std::exp(-6.9 / (sel(p.release, 0.6) * SR));
        break;
    }
    case Inst::Glock:
    case Inst::Celesta:
    case Inst::MusicBox:
    case Inst::Vibes:
    case Inst::Marimba:
    case Inst::Timpani:
    case Inst::Kalimba:
        setupModal();
        dampCoef_ = std::exp(-6.9 / (sel(p.release, 0.35) * SR));
        break;
    case Inst::Guitar:
        setupKS(3.0 * std::pow(196.0 / f_, 0.4) * p.decay, p.bright, 0);
        dampCoef_ = std::exp(-6.9 / (sel(p.release, 0.15) * SR));
        break;
    case Inst::MutedGuitar:
        setupKS(0.16 * p.decay, 0.7 + 0.3 * p.bright, 0);
        break;
    case Inst::Harp:
        setupKS(clamp(5.0 * std::pow(200.0 / f_, 0.5), 1.0, 8.0) * p.decay, p.bright * 0.7, 1);
        break;
    case Inst::Pizz:
        setupKS(clamp(0.8 * std::pow(200.0 / f_, 0.4), 0.18, 1.2) * p.decay, p.bright * 0.8, 1);
        svfL_.set(480, 1.2);
        op1_.set(5000);
        break;
    case Inst::UprightBass:
        setupKS(2.0 * p.decay, p.bright * 0.5, 1);
        op1_.set(1300 + 1200 * p.bright);
        dampCoef_ = std::exp(-6.9 / (sel(p.release, 0.09) * SR));
        break;
    case Inst::Piano:
        setupPiano();
        break;
    case Inst::PluckSynth:
        det_[0] = ratio(-7 * p.detune);
        det_[1] = ratio(7 * p.detune);
        opan_[0] = -p.width;
        opan_[1] = p.width;
        amp_.start(0.0015, 0.45 * p.decay, 0.0, sel(p.release, 0.12));
        break;
    case Inst::FunkBass:
    case Inst::SlapBass:
        amp_.start(0.0015, p.inst == Inst::SlapBass ? 0.45 : 0.9,
                   p.inst == Inst::SlapBass ? 0.45 : 0.6, sel(p.release, 0.05));
        break;
    case Inst::SynthBass:
        det_[0] = ratio(-5);
        det_[1] = ratio(5);
        amp_.start(0.003, 0.5, 0.8, sel(p.release, 0.08));
        break;
    case Inst::SubBass:
        amp_.start(sel(p.attack, 0.006), 0.4, 0.9, sel(p.release, 0.08));
        break;
    case Inst::Lead:
    case Inst::Flute:
    case Inst::Clarinet:
    case Inst::MutedTrumpet:
    case Inst::Whistle:
    case Inst::GlideBass:
        det_[0] = 1.0;
        det_[1] = ratio(7 * p.detune);
        amp_.level = 0;
        amp_.stage = 0;
        retrigger(midi, vel, dur);
        break;
    // ---------------- percussion ----------------
    case Inst::Kick:
        aux_[7] = 0.9 * p.decay;
        op1_.set(2500);
        break;
    case Inst::Snare:
        aux_[7] = 0.8 * p.decay;
        op1_.set(1400);
        op2_.set(9500);
        break;
    case Inst::Clap:
        aux_[7] = 0.7;
        svfL_.set(1150, 1.4);
        svfR_.set(1250, 1.4);
        aux_[0] = rng_.uni() * 0.003;
        break;
    case Inst::HatC:
    case Inst::HatO:
    case Inst::Ride:
    case Inst::Crash: {
        static const double fr[6] = {205.3, 304.4, 369.6, 522.7, 540.0, 800.0};
        double sc = p.inst == Inst::Ride ? 1.25 : p.inst == Inst::Crash ? 1.1 : 1.0;
        for (int i = 0; i < 6; ++i)
            det_[i] = fr[i] * sc * std::pow(2.0, (midi - 60) / 12.0) / SR;
        double tau = p.inst == Inst::HatC   ? 0.022
                     : p.inst == Inst::HatO ? 0.2
                     : p.inst == Inst::Ride ? 0.55
                                            : 0.9;
        aux_[0] = tau * p.decay;
        aux_[7] = aux_[0] * 7.5 + 0.05;
        svfL_.set(p.inst == Inst::Ride ? 7000 : 10000, 0.9);
        op1_.set(p.inst == Inst::HatC || p.inst == Inst::HatO ? 6500 + 2000 * p.bright : 3500);
        op2_.set(p.inst == Inst::HatC || p.inst == Inst::HatO ? 6500 + 2000 * p.bright : 3500);
        if (p.inst == Inst::Ride) {
            ph_[0].set(2350, 0);
            ph_[1].set(3540, 0);
            np_ = 2;
        }
        op3_.set(5000);
        break;
    }
    case Inst::Swell:
        aux_[7] = dur + 0.12;
        op1_.set(2500 + 3000 * p.bright);
        op2_.set(2500 + 3000 * p.bright);
        break;
    case Inst::Tom:
        aux_[7] = 1.2 * p.decay;
        break;
    case Inst::Shaker:
        aux_[7] = 0.3 * p.decay;
        svfL_.set(6500, 1.3);
        break;
    case Inst::Tamb: {
        aux_[7] = 0.5 * p.decay;
        op1_.set(7000);
        const double jf[5] = {5300, 6900, 8200, 9700, 11800};
        for (int i = 0; i < 5; ++i)
            ph_[i].set(jf[i] * (0.98 + 0.04 * rng_.uni()), rng_.uni() * TAU);
        np_ = 5;
        break;
    }
    case Inst::Cowbell:
        aux_[7] = 1.2;
        det_[0] = 540.0 * std::pow(2.0, (midi - 60) / 12.0) / SR;
        det_[1] = 800.0 * std::pow(2.0, (midi - 60) / 12.0) / SR;
        svfL_.set(2100, 1.4);
        break;
    case Inst::Triangle: {
        aux_[7] = 4.0 * p.decay;
        const double r[5] = {1, 2.76, 4.18, 5.4, 7.0}, a[5] = {1, .5, .4, .3, .2};
        double base = 1300 * std::pow(2.0, (midi - 60) / 12.0);
        np_ = 0;
        for (int i = 0; i < 5; ++i)
            if (base * r[i] < 19000) {
                ph_[np_].set(base * r[i], rng_.uni() * TAU);
                pa1_[np_] = a[i];
                pd1_[np_] = std::exp(-6.9 / ((2.2 - 0.25 * i) * p.decay * SR));
                ++np_;
            }
        break;
    }
    case Inst::Rim:
        aux_[7] = 0.2;
        svfL_.set(3000, 2);
        break;
    case Inst::WoodBlock:
        aux_[7] = 0.3;
        break;
    case Inst::Brush:
        aux_[7] = 0.6 * p.decay;
        op1_.set(700);
        svfL_.set(4500 + 2000 * p.bright, 0.6);
        break;
    case Inst::BrushSweep:
        aux_[7] = dur;
        svfL_.set(2400, 0.7);
        op1_.set(5000);
        break;
    case Inst::Snap:
        aux_[7] = 0.25;
        svfL_.set(2300, 3);
        break;
    case Inst::Clave:
        aux_[7] = 0.3;
        break;
    case Inst::Water:
    case Inst::Air:
        amp_.start(sel(p.attack, 1.5), 0.1, 1.0, sel(p.release, 1.5));
        op1_.set(p.inst == Inst::Water ? 700 : 1400);
        op2_.set(0.35);
        op3_.set(0.2);
        svfL_.set(450, 0.8);
        svfR_.set(520, 0.8);
        break;
    case Inst::Bird: {
        int k = 2 + int(rng_.uni() * 3);
        double t = 0;
        // aux_[0..5]: chirp start times; aux_[6]: chirp length; sweep sign in scoop_
        for (int i = 0; i < 6; ++i)
            aux_[i] = 1e9;
        double len = 0.06 + rng_.uni() * 0.05;
        for (int i = 0; i < k; ++i) {
            aux_[i] = t;
            t += len + 0.03 + rng_.uni() * 0.05;
        }
        aux_[6] = len;
        scoop_ = rng_.uni() < 0.5 ? 1.0 : -1.0;
        aux_[7] = t + 0.05;
        break;
    }
    default:
        break;
    }
}

void Voice::retrigger(double midi, double vel, double dur) {
    midi_ = target_ = midi;
    f_ = mtof(midi);
    vel_ = vel;
    dur_ = dur;
    released_ = false;
    tOff_ = -1;
    aux_[0] = 0; // time since articulation
    const Patch& p = p_;
    switch (p.inst) {
    case Inst::Lead:
        amp_.start(sel(p.attack, 0.004), 0.35, 0.82, sel(p.release, 0.12));
        fenv_.start(0.004, 0.45, 0.35, 0.2);
        break;
    case Inst::Flute:
        amp_.start(sel(p.attack, 0.05), 0.25, 0.85, sel(p.release, 0.11));
        svfL_.set(f_ * 2.0, 1.5);
        break;
    case Inst::Clarinet:
        amp_.start(sel(p.attack, 0.03), 0.2, 0.9, sel(p.release, 0.06));
        break;
    case Inst::MutedTrumpet:
        amp_.start(sel(p.attack, 0.018), 0.25, 0.8, sel(p.release, 0.07));
        fenv_.start(0.03, 0.2, 0.45, 0.1);
        scoop_ = -0.28;
        break;
    case Inst::Whistle:
        amp_.start(sel(p.attack, 0.03), 0.1, 0.9, sel(p.release, 0.08));
        svfL_.set(f_ * 1.5, 1.2);
        break;
    case Inst::GlideBass:
        amp_.start(0.003, 0.3, 0.85, sel(p.release, 0.06));
        fenv_.start(0.003, 0.22, 0.3, 0.1);
        break;
    default:
        break;
    }
}

void Voice::glideTo(double midi, double vel, double dur) {
    target_ = midi;
    vel_ = 0.7 * vel_ + 0.3 * vel;
    dur_ = dur;
    released_ = false;
    tOff_ = -1;
    if (amp_.stage >= 4) { // re-articulate but keep sliding from the current pitch
        double old = midi_;
        retrigger(midi, vel, dur);
        midi_ = old;
        target_ = midi;
        f_ = mtof(old);
    }
}

void Voice::noteOff() {
    if (released_)
        return;
    released_ = true;
    tOff_ = 0;
    amp_.release();
    fenv_.release();
    switch (p_.inst) {
    case Inst::EP:
    case Inst::Vibes:
    case Inst::Guitar:
    case Inst::UprightBass:
    case Inst::FMBell:
    case Inst::BellLead:
        damp_ = dampCoef_;
        break;
    case Inst::Piano:
        damp_ = dampCoef_;
        break;
    case Inst::MutedGuitar:
        break;
    default:
        break;
    }
}

void Voice::tick(double& outL, double& outR) {
    const Patch& p = p_;
    double t = t_;
    double l = 0, r = 0;
    const double dt = 1.0 / SR;
    // smooth portamento for mono lines
    if (midi_ != target_) {
        double k = 1.0 - std::exp(-dt / std::max(glideT_, 0.002));
        midi_ += (target_ - midi_) * k;
        if (std::fabs(target_ - midi_) < 1e-4)
            midi_ = target_;
        f_ = mtof(midi_);
    }
    switch (p.inst) {
    case Inst::Brass: {
        double vib =
            p.vib * 0.11 * std::sin(TAU * 5.3 * t + lfo_ * TAU) * clamp((t - 0.28) / 0.3, 0.0, 1.0);
        double fm = f_ * std::exp2((scoop_ * expDecay(t, 0.035) + vib) / 12.0);
        double sl = 0, sr = 0;
        for (int i = 0; i < 4; ++i) {
            double s = osc_[i].saw(fm * det_[i] / SR) * (i == 3 ? 0.5 : 1.0);
            sl += s * (1.0 - opan_[i]) * 0.5;
            sr += s * (1.0 + opan_[i]) * 0.5;
        }
        double a = amp_.tick(), fe = fenv_.tick();
        double cut = f_ * 1.15 + (700 + 6500 * p.bright) * vel_ * fe * (0.6 + 0.4 * a);
        if ((n_ & 3) == 0) {
            svfL_.set(cut, 0.75);
            svfR_.set(cut, 0.75);
        }
        double gl = svfL_.tick(sl), gr = svfR_.tick(sr);
        double g = 0.24 * vcurve(vel_, 1.1) * a;
        l = std::tanh(gl * g * 2.2) / 2.2;
        r = std::tanh(gr * g * 2.2) / 2.2;
        break;
    }
    case Inst::EP: {
        double idx = (0.35 + 2.1 * vel_ * p.bright) * expDecay(t, 0.45) + 0.25 * p.bright;
        double mod = std::sin(TAU * fm1_);
        double car = std::sin(TAU * fm2_ + idx * mod);
        double tine =
            f_ * 14 < 18000 ? std::sin(TAU * fm3_) * 0.16 * vel_ * expDecay(t, 0.025) : 0.0;
        fm1_ += f_ / SR;
        fm2_ += f_ / SR;
        fm3_ += f_ * 14 / SR;
        if (fm1_ > 1)
            fm1_ -= 1;
        if (fm2_ > 1)
            fm2_ -= 1;
        if (fm3_ > 1)
            fm3_ -= 1;
        pa1_[0] *= pd1_[0] * (released_ ? damp_ : 1.0);
        double att = clamp(t / 0.002, 0.0, 1.0);
        double y = (car + tine) * pa1_[0] * att * 0.28 * vcurve(vel_, 1.3);
        double trem = p.param * std::sin(TAU * 4.6 * t + lfo_ * TAU);
        l = y * (1.0 - 0.5 * trem);
        r = y * (1.0 + 0.5 * trem);
        break;
    }
    case Inst::Clav: {
        double raw = osc_[0].pulse(f_ / SR, 0.28) * 0.75 + osc_[1].saw(f_ / SR) * 0.25;
        std::size_t L = ks_.size();
        ks_[ksW_] = raw;
        std::size_t d = std::size_t(std::max(2.0, SR / f_ * 0.12));
        double y = raw - ks_[(ksW_ + L - d) % L];
        ksW_ = (ksW_ + 1) % L;
        double cut = f_ * 2.0 + (2500 + 6500 * p.bright) * vel_ * expDecay(t, 0.11);
        if ((n_ & 3) == 0)
            svfL_.set(cut, 1.8);
        double a = amp_.tick();
        double z = svfL_.tick(y) * a * 0.2 * vcurve(vel_, 1.1);
        l = r = z;
        break;
    }
    case Inst::Organ: {
        double s = 0;
        for (int i = 0; i < np_; ++i)
            s += ph_[i].tick() * pa1_[i];
        if ((n_ & 1023) == 0)
            for (int i = 0; i < np_; ++i)
                ph_[i].renorm();
        double a = amp_.tick();
        double click = rng_.bip() * expDecay(t, 0.0018) * 0.05 * p.bright;
        double rot = std::sin(TAU * (p.vib > 1.5 ? 6.2 : 0.85) * t + lfo_ * TAU);
        double y = s * a + click;
        l = y * (1.0 + 0.18 * rot);
        r = y * (1.0 - 0.18 * rot);
        break;
    }
    case Inst::Strings:
    case Inst::Pad: {
        bool pad = p.inst == Inst::Pad;
        double vib = p.vib * (pad ? 0.0 : 0.07) * std::sin(TAU * 5.1 * t + lfo_ * TAU) *
                     clamp((t - 0.3) / 0.4, 0.0, 1.0);
        double base = f_ * std::exp2(vib / 12.0);
        double sl = 0, sr = 0;
        for (int i = 0; i < 6; ++i) {
            double drift =
                std::exp2(2.5 * std::sin(TAU * (0.21 + 0.07 * i) * t + aux_[i]) / 1200.0);
            double inc = base * det_[i] * drift / SR;
            double s = pad && (i & 1)
                           ? osc_[i].pulse(inc, 0.5 + 0.2 * std::sin(TAU * 0.3 * t + aux_[i])) * 0.7
                           : osc_[i].saw(inc);
            sl += s * (1.0 - opan_[i]) * 0.5;
            sr += s * (1.0 + opan_[i]) * 0.5;
        }
        double a = amp_.tick();
        if ((n_ & 7) == 0) {
            double cut =
                pad ? (600 + 2600 * p.bright) * (1.0 + 0.3 * std::sin(TAU * 0.11 * t + lfo2_ * TAU))
                    : std::min(f_ * 7.0, 1500 + 4500 * p.bright) * (0.55 + 0.45 * a);
            svfL_.set(cut, 0.6);
            svfR_.set(cut, 0.6);
        }
        double g = (pad ? 0.24 : 0.19) * vcurve(vel_, 1.0) * a;
        l = svfL_.tick(sl) * g;
        r = svfR_.tick(sr) * g;
        break;
    }
    case Inst::Choir: {
        double vib =
            p.vib * 0.1 * std::sin(TAU * 5.4 * t + lfo_ * TAU) * clamp((t - 0.2) / 0.5, 0.0, 1.0);
        double s = 0;
        for (int i = 0; i < 4; ++i) {
            double jit = std::exp2(3.0 * std::sin(TAU * (0.3 + 0.11 * i) * t + aux_[i]) / 1200.0);
            s += osc_[i].saw(f_ * det_[i] * jit * std::exp2(vib / 12.0) / SR);
        }
        svf2_.tick(s);
        svf3_.tick(s);
        svf4_.tick(s);
        double y = svf2_.bp * svf2_.k + svf3_.bp * svf3_.k * aux_[5] + svf4_.bp * svf4_.k * aux_[6];
        double a = amp_.tick();
        y *= a * 0.45 * vcurve(vel_, 1.0);
        l = r = y;
        break;
    }
    case Inst::FMBell:
    case Inst::BellLead: {
        bool bell = p.inst == Inst::FMBell;
        double mr = p.param > 0 ? p.param : (bell ? 3.5 : 3.0);
        double idx = (bell ? 1.4 + 2.2 * p.bright : 0.8 + 1.8 * p.bright) * vel_ *
                         expDecay(t, bell ? 0.7 : 0.22) +
                     0.25;
        double mod = std::sin(TAU * fm1_);
        double car = std::sin(TAU * fm2_ + idx * mod);
        double oct = std::sin(TAU * fm3_) * (bell ? 0.12 : 0.25) * expDecay(t, 0.4);
        fm1_ += f_ * mr / SR;
        fm2_ += f_ / SR;
        fm3_ += f_ * 2.0 / SR;
        if (fm1_ > 1)
            fm1_ -= 1;
        if (fm2_ > 1)
            fm2_ -= 1;
        if (fm3_ > 1)
            fm3_ -= 1;
        pa1_[0] *= pd1_[0] * (released_ ? damp_ : 1.0);
        double att = clamp(t / 0.0015, 0.0, 1.0);
        double y = (car + oct) * pa1_[0] * att * 0.24 * vcurve(vel_, 1.2);
        l = r = y;
        break;
    }
    case Inst::Glock:
    case Inst::Celesta:
    case Inst::MusicBox:
    case Inst::Vibes:
    case Inst::Marimba:
    case Inst::Timpani:
    case Inst::Kalimba: {
        double sl = 0, sr = 0;
        double dmp = (released_ && p.inst == Inst::Vibes) ? damp_ : 1.0;
        for (int i = 0; i < np_; ++i) {
            double s = ph_[i].tick() * pa1_[i];
            pa1_[i] *= pd1_[i] * dmp;
            sl += s * (1.0 - ppan_[i]);
            sr += s * (1.0 + ppan_[i]);
        }
        if ((n_ & 1023) == 0)
            for (int i = 0; i < np_; ++i)
                ph_[i].renorm();
        double att = clamp(t / aux_[0], 0.0, 1.0);
        double nz = op1_.lp(rng_.bip()) * aux_[1] * expDecay(t, 0.004);
        double trem = 1.0;
        if (p.inst == Inst::Vibes && p.param > 0)
            trem = 1.0 - p.param * 0.5 * (1.0 + std::sin(TAU * 5.2 * t + lfo_ * TAU));
        double g = 0.27 * trem * att * (p.inst == Inst::Marimba ? 1.4 : 1.0);
        l = (sl * g + nz);
        r = (sr * g + nz);
        break;
    }
    case Inst::Guitar:
    case Inst::MutedGuitar:
    case Inst::Harp:
    case Inst::Pizz:
    case Inst::UprightBass: {
        double y = ksTick();
        double gain = 0.33 * vcurve(vel_, 1.3) *
                      (p.inst == Inst::Guitar ? 2.2
                       : p.inst == Inst::Pizz ? 1.8
                                              : 1.0);
        if (released_ && (p.inst == Inst::Guitar || p.inst == Inst::UprightBass)) {
            aux_[2] = (aux_[2] == 0 ? 1.0 : aux_[2]) * damp_;
            gain *= aux_[2];
        }
        if (p.inst == Inst::Pizz) {
            svfL_.tick(y);
            y = op1_.lp(y + svfL_.bp * 0.5);
            y *= clamp(t / 0.003, 0.0, 1.0);
        } else if (p.inst == Inst::UprightBass) {
            y = op1_.lp(y) + std::sin(TAU * f_ * t) * 0.35 * expDecay(t, 0.3);
            y *= clamp(t / 0.004, 0.0, 1.0);
        } else if (p.inst == Inst::Harp) {
            y *= clamp(t / 0.002, 0.0, 1.0);
        } else if (p.inst == Inst::MutedGuitar) {
            y *= clamp(t / 0.001, 0.0, 1.0) * 0.8;
        }
        l = r = y * gain;
        break;
    }
    case Inst::Piano: {
        double s = 0;
        double dmp = released_ ? damp_ : 1.0;
        for (int i = 0; i < np_; ++i) {
            double v = ph_[i].tick();
            s += v * (pa1_[i] + pa2_[i]);
            pa1_[i] *= pd1_[i] * dmp;
            pa2_[i] *= pd2_[i] * dmp;
        }
        if ((n_ & 1023) == 0)
            for (int i = 0; i < np_; ++i)
                ph_[i].renorm();
        double hammer = op1_.lp(rng_.bip()) * aux_[1] * expDecay(t, 0.003);
        double att = clamp(t / 0.0012, 0.0, 1.0);
        double y = s * att + hammer;
        l = r = y;
        break;
    }
    case Inst::PluckSynth: {
        double s0 = osc_[0].saw(f_ * det_[0] / SR), s1 = osc_[1].saw(f_ * det_[1] / SR);
        double sub = osc_[2].pulse(f_ * 0.5 / SR, 0.5) * 0.25;
        double cut = f_ * 1.3 + (800 + 8000 * p.bright) * vel_ * expDecay(t, 0.085 * p.decay);
        if ((n_ & 3) == 0) {
            svfL_.set(cut, 1.1);
            svfR_.set(cut, 1.1);
        }
        double a = amp_.tick();
        double g = 0.27 * vcurve(vel_, 1.1) * a;
        l = svfL_.tick(s0 * (1.0 - opan_[0] * 0.5) + s1 * (1.0 - opan_[1] * 0.5) * 0.8 + sub) * g;
        r = svfR_.tick(s0 * (1.0 + opan_[0] * 0.5) * 0.8 + s1 * (1.0 + opan_[1] * 0.5) + sub) * g;
        break;
    }
    case Inst::FunkBass:
    case Inst::SlapBass: {
        bool slap = p.inst == Inst::SlapBass;
        double s = osc_[0].saw(f_ / SR) * 0.6 + osc_[1].pulse(f_ / SR, 0.5) * 0.4;
        double cut =
            f_ * (slap ? 1.6 : 1.1) +
            (slap ? 4200 : 2200) * (0.3 + 0.7 * p.bright) * vel_ * expDecay(t, slap ? 0.05 : 0.13) +
            120;
        if ((n_ & 3) == 0)
            lad_.set(cut, slap ? 0.22 : 0.12);
        double a = amp_.tick();
        double y = lad_.tick(s * 0.8) * a;
        double tr = rng_.bip() * expDecay(t, 0.0015) * (slap ? 0.25 : 0.08);
        if (slap)
            tr += std::sin(TAU * 2.0 * f_ * t) * 0.25 * expDecay(t, 0.02);
        y = (y + tr) * (slap ? 0.66 : 0.52) * vcurve(vel_, 1.2);
        l = r = y;
        break;
    }
    case Inst::SynthBass: {
        double s = osc_[0].saw(f_ * det_[0] / SR) * 0.5 + osc_[1].saw(f_ * det_[1] / SR) * 0.5 +
                   osc_[2].sine(f_ * 0.5 / SR) * 0.4;
        double cut = f_ * 2.0 + 1800 * p.bright * vel_ * expDecay(t, 0.22) + 100;
        if ((n_ & 3) == 0)
            lad_.set(cut, 0.25);
        double a = amp_.tick();
        l = r = lad_.tick(s * 0.7) * a * 0.53 * vcurve(vel_, 1.1);
        break;
    }
    case Inst::SubBass: {
        double ph = osc_[0].ph;
        double s = std::sin(TAU * ph) + 0.12 * std::sin(2 * TAU * ph);
        osc_[0].ph += f_ / SR;
        if (osc_[0].ph >= 1)
            osc_[0].ph -= 1;
        double a = amp_.tick();
        l = r = s * a * 0.18 * vcurve(vel_, 1.0);
        break;
    }
    case Inst::Lead: {
        double ta = aux_[0];
        aux_[0] += dt;
        double vib = p.vib * 0.17 * std::sin(TAU * 5.6 * t) * clamp((ta - 0.22) / 0.35, 0.0, 1.0);
        double fr = f_ * std::exp2(vib / 12.0);
        double s = osc_[0].saw(fr / SR) * 0.55 + osc_[1].saw(fr * det_[1] / SR) * 0.45 +
                   osc_[2].pulse(fr * 0.5 / SR, 0.5) * 0.3;
        double a = amp_.tick(), fe = fenv_.tick();
        double cut = 300 + f_ * 1.6 + (1200 + 5500 * p.bright) * fe * (0.5 + 0.5 * vel_);
        if ((n_ & 1) == 0)
            lad_.set(cut, 0.32);
        double y = lad_.tick(s * 0.7);
        y = std::tanh(y * 1.5) / 1.5 * a * 0.54 * vcurve(vel_, 0.8);
        l = r = y;
        break;
    }
    case Inst::Flute: {
        double ta = aux_[0];
        aux_[0] += dt;
        double vib = p.vib * 0.16 * std::sin(TAU * 5.0 * t) * clamp((ta - 0.2) / 0.4, 0.0, 1.0);
        double fr = f_ * std::exp2(vib / 12.0);
        double ph = osc_[0].ph;
        double s =
            std::sin(TAU * ph) + 0.2 * std::sin(2 * TAU * ph) + 0.05 * std::sin(3 * TAU * ph);
        osc_[0].ph += fr / SR;
        if (osc_[0].ph >= 1)
            osc_[0].ph -= 1;
        double a = amp_.tick();
        if ((n_ & 255) == 0)
            svfL_.set(f_ * 2.0, 1.5);
        double breath = svfL_.tick(rng_.bip());
        breath = svfL_.bp * svfL_.k * 0.06;
        double chiff = rng_.bip() * 0.1 * expDecay(ta, 0.018) * p.bright;
        double y = (s * (0.9 + 0.1 * a) + breath + chiff) * a * 0.24 * vcurve(vel_, 0.9);
        l = r = y;
        break;
    }
    case Inst::Clarinet: {
        double ta = aux_[0];
        aux_[0] += dt;
        double vib = p.vib * 0.05 * std::sin(TAU * 5.0 * t) * clamp((ta - 0.3) / 0.4, 0.0, 1.0);
        double fr = f_ * std::exp2(vib / 12.0);
        double s = osc_[0].pulse(fr / SR, 0.5) * 0.85 + osc_[1].saw(fr / SR) * 0.1;
        double a = amp_.tick();
        if ((n_ & 3) == 0)
            svfL_.set(f_ * 3.0 + 700 + 1600 * p.bright * a * vel_, 0.75);
        double y = svfL_.tick(s + rng_.bip() * 0.03);
        l = r = y * a * 0.22 * vcurve(vel_, 0.9);
        break;
    }
    case Inst::MutedTrumpet: {
        double ta = aux_[0];
        aux_[0] += dt;
        double vib = p.vib * 0.12 * std::sin(TAU * 5.5 * t) * clamp((ta - 0.25) / 0.3, 0.0, 1.0);
        double fr = f_ * std::exp2((scoop_ * expDecay(ta, 0.03) + vib) / 12.0);
        double s = osc_[0].saw(fr / SR);
        double a = amp_.tick(), fe = fenv_.tick();
        if ((n_ & 3) == 0) {
            svfL_.set(1100 + 700 * fe, 3.0);
            svfR_.set(f_ * 5.0, 0.7);
        }
        svfL_.tick(s);
        double y = svfL_.bp * svfL_.k * 0.7 + svfR_.tick(s) * 0.25;
        l = r = y * a * 0.66 * vcurve(vel_, 1.0);
        break;
    }
    case Inst::Whistle: {
        double ta = aux_[0];
        aux_[0] += dt;
        double vib = p.vib * 0.2 * std::sin(TAU * 6.0 * t) * clamp((ta - 0.15) / 0.3, 0.0, 1.0);
        double fr = f_ * std::exp2(vib / 12.0);
        double ph = osc_[0].ph;
        double s = std::sin(TAU * ph) + 0.06 * std::sin(2 * TAU * ph);
        osc_[0].ph += fr / SR;
        if (osc_[0].ph >= 1)
            osc_[0].ph -= 1;
        double a = amp_.tick();
        if ((n_ & 255) == 0)
            svfL_.set(f_ * 1.5, 1.2);
        svfL_.tick(rng_.bip());
        double y = (s + svfL_.bp * svfL_.k * 0.05) * a * 0.24 * vcurve(vel_, 0.9);
        l = r = y;
        break;
    }
    case Inst::GlideBass: {
        double s = osc_[0].saw(f_ / SR) * 0.6 + osc_[2].pulse(f_ * 0.5 / SR, 0.5) * 0.35;
        double a = amp_.tick(), fe = fenv_.tick();
        double cut = 160 + f_ * 2.2 + 1600 * p.bright * fe * vel_;
        if ((n_ & 1) == 0)
            lad_.set(cut, 0.34);
        double y = lad_.tick(s * 0.8);
        l = r = std::tanh(y * 1.3) / 1.3 * a * 0.5 * vcurve(vel_, 1.0);
        break;
    }
    // ---------------- percussion ----------------
    case Inst::Kick: {
        double pv = std::pow(2.0, (midi_ - 60) / 12.0);
        double fe = 47.0 * pv, fs = fe * 3.4;
        double fr = fe + (fs - fe) * expDecay(t, 0.032);
        fm1_ += fr / SR;
        if (fm1_ > 1)
            fm1_ -= 1;
        double body =
            std::sin(TAU * fm1_) * expDecay(t, 0.24 * p.decay) * clamp(t / 0.0006, 0.0, 1.0);
        double click = op1_.hp(rng_.bip()) * expDecay(t, 0.0016) * (0.15 + 0.35 * p.bright);
        double y = (std::tanh(body * 1.6) / std::tanh(1.6) * 0.62 + click) * vcurve(vel_, 1.2);
        l = r = y;
        break;
    }
    case Inst::Snare: {
        double pv = std::pow(2.0, (midi_ - 60) / 12.0);
        double tone = std::sin(TAU * 185 * pv * t) * 0.5 * expDecay(t, 0.055) +
                      std::sin(TAU * 330 * pv * t) * 0.3 * expDecay(t, 0.035);
        double nz =
            op2_.lp(op1_.hp(rng_.bip())) * (0.55 + 0.3 * p.bright) * expDecay(t, 0.13 * p.decay);
        double y = (tone + nz) * clamp(t / 0.0005, 0.0, 1.0) * 0.9 * vcurve(vel_, 1.3);
        l = r = y;
        break;
    }
    case Inst::Clap: {
        double e = 0;
        for (int i = 0; i < 3; ++i) {
            double ti = t - i * 0.0105 - aux_[0];
            if (ti >= 0)
                e += expDecay(ti, 0.0055);
        }
        double tt = t - 0.032;
        if (tt >= 0)
            e += 0.85 * expDecay(tt, 0.1 * p.decay);
        double nl = rng_.bip(), nr = rng_.bip();
        svfL_.tick(nl);
        svfR_.tick(nr);
        double g = 1.9 * e * vcurve(vel_, 1.2);
        l = svfL_.bp * svfL_.k * g;
        r = svfR_.bp * svfR_.k * g;
        break;
    }
    case Inst::HatC:
    case Inst::HatO:
    case Inst::Ride:
    case Inst::Crash: {
        double m = 0;
        for (int i = 0; i < 6; ++i)
            m += osc_[i].pulse(det_[i], 0.5);
        double nz = rng_.bip();
        bool cym = p.inst == Inst::Crash;
        double src = cym ? m * 0.35 + nz * 0.9 : m * 0.6 + nz * 0.35;
        svfL_.tick(src);
        double y = svfL_.bp * svfL_.k * 0.5 + (cym ? src * 0.25 : 0.0);
        y = op2_.hp(op1_.hp(y));
        double e = expDecay(t, aux_[0]) * clamp(t / (cym ? 0.002 : 0.0005), 0.0, 1.0);
        double bell = 0;
        if (p.inst == Inst::Ride)
            bell = (ph_[0].tick() + 0.6 * ph_[1].tick()) * 0.12 * expDecay(t, 0.35);
        double g = (p.inst == Inst::HatC   ? 2.0
                    : p.inst == Inst::HatO ? 1.5
                    : p.inst == Inst::Ride ? 0.9
                                           : 0.4) *
                   vcurve(vel_, 1.2);
        double out = (y * e + bell) * g;
        l = out;
        r = out;
        if (cym) {
            double nz2 = op3_.hp(rng_.bip()) * 0.15 * e * g;
            l += nz2;
            r -= nz2;
        }
        break;
    }
    case Inst::Swell: {
        double d = dur_;
        double e = t < d ? std::pow(t / d, 3.0) : expDecay(t - d, 0.025);
        double nz1 = op1_.hp(rng_.bip()), nz2 = op2_.hp(rng_.bip());
        double g = 0.3 * e * vcurve(vel_, 1.0);
        l = nz1 * g;
        r = nz2 * g;
        break;
    }
    case Inst::Tom: {
        double fr = f_ * (1.0 + 0.5 * expDecay(t, 0.035));
        fm1_ += fr / SR;
        if (fm1_ > 1)
            fm1_ -= 1;
        double y = std::sin(TAU * fm1_) * expDecay(t, 0.2 * p.decay) * clamp(t / 0.0008, 0.0, 1.0) +
                   rng_.bip() * 0.06 * expDecay(t, 0.008);
        l = r = y * 0.55 * vcurve(vel_, 1.2);
        break;
    }
    case Inst::Shaker: {
        svfL_.tick(rng_.bip());
        double e = t < 0.012 ? t / 0.012 : expDecay(t - 0.012, 0.03 * p.decay);
        l = r = svfL_.bp * svfL_.k * e * 0.7 * vcurve(vel_, 1.3);
        break;
    }
    case Inst::Tamb: {
        double j = 0;
        for (int i = 0; i < np_; ++i)
            j += ph_[i].tick();
        double e = expDecay(t, 0.07 * p.decay) * clamp(t / 0.001, 0.0, 1.0);
        double y = (op1_.hp(rng_.bip()) * 0.35 + j * 0.12 * (0.6 + 0.4 * rng_.uni())) * e * 1.1 *
                   vcurve(vel_, 1.2);
        l = r = y;
        break;
    }
    case Inst::Cowbell: {
        double s = osc_[0].pulse(det_[0], 0.5) + osc_[1].pulse(det_[1], 0.5);
        svfL_.tick(s);
        double e = 0.6 * expDecay(t, 0.045) + 0.4 * expDecay(t, 0.22);
        l = r = (svfL_.bp * svfL_.k * 0.8 + s * 0.08) * e * clamp(t / 0.0006, 0.0, 1.0) * 0.55 *
                vcurve(vel_, 1.2);
        break;
    }
    case Inst::Triangle: {
        double s = 0;
        for (int i = 0; i < np_; ++i) {
            s += ph_[i].tick() * pa1_[i];
            pa1_[i] *= pd1_[i];
        }
        l = r = s * 0.1 * clamp(t / 0.0008, 0.0, 1.0) * vcurve(vel_, 1.2);
        break;
    }
    case Inst::Rim: {
        svfL_.tick(rng_.bip());
        double y = std::sin(TAU * 1750 * t) * expDecay(t, 0.007) * 0.5 +
                   svfL_.bp * svfL_.k * 0.35 * expDecay(t, 0.009) +
                   std::sin(TAU * 420 * t) * 0.3 * expDecay(t, 0.02);
        l = r = y * clamp(t / 0.0004, 0.0, 1.0) * 1.0 * vcurve(vel_, 1.2);
        break;
    }
    case Inst::WoodBlock: {
        double fr = f_;
        double y = std::sin(TAU * fr * t) * expDecay(t, 0.035) +
                   0.3 * std::sin(TAU * 2.7 * fr * t) * expDecay(t, 0.014) +
                   rng_.bip() * 0.12 * expDecay(t, 0.003);
        l = r = y * clamp(t / 0.0004, 0.0, 1.0) * 0.45 * vcurve(vel_, 1.2);
        break;
    }
    case Inst::Brush: {
        double e = t < 0.004 ? t / 0.004 : expDecay(t - 0.004, 0.09 * p.decay);
        double y = svfL_.tick(op1_.hp(rng_.bip())) * e * 0.56 * vcurve(vel_, 1.1);
        l = r = y;
        break;
    }
    case Inst::BrushSweep: {
        double d = std::max(dur_, 0.05);
        double ph = clamp(t / d, 0.0, 1.0);
        double e = std::sin(PI * ph);
        e *= e;
        svfL_.tick(rng_.bip());
        double y = op1_.lp(svfL_.bp * svfL_.k) * e * 0.3 * vcurve(vel_, 1.0);
        l = y;
        r = y;
        break;
    }
    case Inst::Snap: {
        svfL_.tick(rng_.bip());
        double y = svfL_.bp * svfL_.k * expDecay(t, 0.011) * 1.5 +
                   std::sin(TAU * 1800 * t) * 0.45 * expDecay(t, 0.006);
        l = r = y * clamp(t / 0.0004, 0.0, 1.0) * vcurve(vel_, 1.2);
        break;
    }
    case Inst::Clave: {
        double pv = std::pow(2.0, (midi_ - 60) / 12.0);
        double y = std::sin(TAU * 2500 * pv * t) * expDecay(t, 0.03) +
                   0.15 * std::sin(TAU * 5900 * pv * t) * expDecay(t, 0.01);
        l = r = y * clamp(t / 0.0004, 0.0, 1.0) * 0.4 * vcurve(vel_, 1.2);
        break;
    }
    case Inst::Water:
    case Inst::Air: {
        bool water = p.inst == Inst::Water;
        double a = amp_.tick();
        // brownish noise per channel with slow random swells
        aux_[0] = undenorm(aux_[0] * 0.997 + rng_.bip() * 0.06);
        aux_[1] = undenorm(aux_[1] * 0.997 + rng_.bip() * 0.06);
        double swell = 0.55 + 0.45 * std::sin(TAU * 0.13 * t + lfo_ * TAU) *
                                  std::sin(TAU * 0.071 * t + lfo2_ * TAU);
        double lap = water ? 0.6 + 0.4 * std::max(0.0, std::sin(TAU * 0.42 * t + lfo_ * 3.0)) : 1.0;
        if ((n_ & 63) == 0) {
            double c = water ? 380 + 260 * swell : 700 + 600 * swell;
            svfL_.set(c, 0.7);
            svfR_.set(c * 1.13, 0.7);
        }
        double nl = water ? aux_[0] : rng_.bip() * 0.3 + aux_[0] * 0.5;
        double nr = water ? aux_[1] : rng_.bip() * 0.3 + aux_[1] * 0.5;
        svfL_.tick(nl);
        svfR_.tick(nr);
        double g = a * swell * lap * (water ? 0.5 : 0.75) * vcurve(vel_, 1.0);
        l = (svfL_.bp * svfL_.k * 0.7 + svfL_.lp * 0.3) * g;
        r = (svfR_.bp * svfR_.k * 0.7 + svfR_.lp * 0.3) * g;
        break;
    }
    case Inst::Bird: {
        double y = 0;
        double len = aux_[6];
        for (int i = 0; i < 6; ++i) {
            double tt = t - aux_[i];
            if (tt < 0 || tt > len)
                continue;
            double w = std::sin(PI * tt / len);
            double sweep = 1.0 + scoop_ * 0.28 * (tt / len) + 0.05 * std::sin(TAU * 28 * tt);
            fm1_ += f_ * sweep / SR;
            if (fm1_ > 1)
                fm1_ -= 1;
            y = std::sin(TAU * fm1_) * w * w * 0.12;
        }
        l = r = y * vcurve(vel_, 1.0);
        break;
    }
    default:
        break;
    }
    // amplitude follower for modal/KS termination
    double a = std::fabs(l) + std::fabs(r);
    env_ = std::max(a, env_ * 0.9995);
    if (env_ < 2e-5)
        quietFor_ += dt;
    else
        quietFor_ = 0;
    outL = l;
    outR = r;
    t_ += dt;
    ++n_;
    if (released_)
        tOff_ += dt;
}

} // namespace ps
