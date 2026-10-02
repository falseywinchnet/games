// PlaySuite music synthesizer: instrument voices.
#pragma once
#include "dsp.hpp"
#include "score.hpp"

namespace ps {

// A single sounding voice. Polyphonic instruments create one per note; monophonic lines reuse one
// voice and call glideTo() for legato notes. tick() writes one stereo sample.
class Voice {
  public:
    Voice(const Patch& p, Rng& rng) : p_(p), rng_(rng) {}
    void noteOn(double midi, double vel, double durSec);
    void glideTo(double midi, double vel, double durSec);   // legato (mono)
    void retrigger(double midi, double vel, double durSec); // mono, re-articulated
    void noteOff();
    void setGlide(double seconds) {
        glideT_ = seconds;
    }
    void tick(double& l, double& r);
    bool finished() const;
    double maxTail() const; // seconds after note-off before forced stop

  private:
    void setupModal();
    void setupKS(double t60, double bright, int excitation);
    void setupPiano();
    double ksTick();

    Patch p_;
    Rng& rng_;
    double midi_ = 60, target_ = 60, f_ = 261.6, vel_ = 0.8, dur_ = 1;
    double t_ = 0;     // seconds since note-on
    double tOff_ = -1; // seconds since note-off (<0 while held)
    bool released_ = false;
    long n_ = 0;
    ADSR amp_, fenv_;
    Osc osc_[8];
    double det_[8]{};
    double opan_[8]{};
    SVF svfL_, svfR_, svf2_, svf3_, svf4_;
    Ladder lad_;
    OnePole op1_, op2_, op3_;
    // modal / additive
    static constexpr int MAXP = 48;
    int np_ = 0;
    Phasor ph_[MAXP];
    double pa1_[MAXP]{}, pa2_[MAXP]{}, pd1_[MAXP]{}, pd2_[MAXP]{};
    double ppan_[MAXP]{};
    double damp_ = 1.0, dampCoef_ = 1.0;
    // Karplus-Strong
    std::vector<double> ks_;
    std::size_t ksN_ = 0, ksW_ = 0;
    double ksFrac_ = 0, ksG_ = 0.99, ksLast_ = 0, ksApZ_ = 0, ksApC_ = 0, ksLp_ = 0.5,
           ksLpState_ = 0;
    double ksDampG_ = 1.0;
    double env_ = 0; // amplitude follower for termination
    double quietFor_ = 0;
    double fm1_ = 0, fm2_ = 0, fm3_ = 0; // FM phases
    double lfo_ = 0, lfo2_ = 0;
    double scoop_ = 0;
    double noiseLevel_ = 0;
    double aux_[8]{};
    double glideT_ = 0.05;
};

} // namespace ps
