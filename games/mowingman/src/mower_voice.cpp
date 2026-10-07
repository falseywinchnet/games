#include "mower_voice.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace mm {
namespace {

constexpr double sample_hz = MowerVoice::sample_rate;
constexpr double dt = 1.0 / sample_hz;
constexpr double pi = 3.14159265358979323846;
constexpr double tau = 2 * pi;
constexpr double rated_rpm = 3600;
constexpr double blade_ratio = 1.0125;   // spindle revolutions per crank revolution
constexpr double rated_blade = rated_rpm / 60 * blade_ratio;
constexpr double crank_seconds = 0.35;
constexpr std::size_t kernel_size = 8192;
constexpr std::size_t kernel_mask = kernel_size - 1;
constexpr std::size_t bounce_delay = 76;  // 1.6 ms: the ground between deck and ear
constexpr std::size_t bounce_mask = 127;

// The level each part is brought to before mixing. The first five are set so the
// part has unit power at rated speed with the deck off, drone with the deck
// spinning free, the next five so the filtered noise has unit power, and clunk
// so the deck's ring peaks at one. calibration_drift() re-measures them; build
// with MM_VOICE_PRINT_CALIBRATION to print replacements after changing a filter.
struct Gains {
    double exhaust{1};
    double air{1};
    double intake{1};
    double whine{1};
    double clatter{1};
    double drone{1};
    double wind{1};
    double squeal{1};
    double side{1};
    double rough{1};
    double rough2{1};
    double clunk{1};
};

constexpr Gains tuned{720.61, 4.8416, 14.649, 1.0137, 573.96, 0.63075, 9.5027, 7.0222, 2.1678, 27.239, 26.735, 160.67};

// The mix, as accepted by ear.
constexpr double mix_air = 0.22;
constexpr double mix_intake = 0.22;
constexpr double mix_whine = 0.09;
constexpr double mix_clatter = 0.05;
constexpr double mix_squeal = 0.08;
constexpr double mix_clunk = 1.5;
constexpr double mix_side = 0.04;
constexpr double drive = 0.9 / 6.5992;   // into the soft limiter
constexpr double ceiling = 0.7 / 0.9352;

class Random final {
  public:
    explicit Random(std::uint64_t seed) {
        // splitmix64, so neighbouring seeds give unrelated streams
        std::uint64_t z = seed + 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        state_ = (z ^ (z >> 31)) | 1ULL;
    }
    double unit() {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return static_cast<double>((state_ * 0x2545F4914F6CDD1DULL) >> 11) * (1.0 / 9007199254740992.0);
    }
    // Unit-power noise for the filters; its shape does not matter once filtered.
    double white() { return (unit() - 0.5) * 3.4641016151377544; }
    double gauss() { return (unit() + unit() + unit() + unit() - 2.0) * 1.7320508075688772; }

  private:
    std::uint64_t state_{};
};

struct OnePole {
    double a{};
    double z{};
    void set(double hz) { a = 1 - std::exp(-tau * hz * dt); }
    double low(double x) {
        z += a * (x - z);
        return z;
    }
    double high(double x) { return x - low(x); }
};

// A resonance with unit gain at its centre.
struct Band {
    double b0{};
    double a1{};
    double a2{};
    double s1{};
    double s2{};
    void set(double hz, double q) {
        const double w = tau * hz * dt;
        const double alpha = std::sin(w) / (2 * q);
        const double a0 = 1 + alpha;
        b0 = alpha / a0;
        a1 = -2 * std::cos(w) / a0;
        a2 = (1 - alpha) / a0;
    }
    double run(double x) {
        const double y = b0 * x + s1;
        s1 = -a1 * y + s2;
        s2 = -b0 * x - a2 * y;
        return y;
    }
};

// A smooth random walk: unit-variance knots at the given rate, joined by lines.
struct Wander {
    double step{};
    double phase{};
    double from{};
    double to{};
    void set(double hz, Random& random) {
        step = hz * dt;
        from = random.gauss();
        to = random.gauss();
    }
    double next(Random& random) {
        phase += step;
        if (phase >= 1) {
            phase -= 1;
            from = to;
            to = random.gauss();
        }
        return from + (to - from) * phase;
    }
};

// What one firing does to the air at the tailpipe: the measured exhaust curve
// (harmonic levels of the reference with the twin's firing comb divided out) as a
// zero-phase impulse response.
std::vector<float> build_exhaust_kernel() {
    {
        constexpr std::array<double, 18> hz{10, 29, 58, 87, 116, 145, 174, 203, 232, 261, 290, 319, 350, 460, 640, 1000, 2000, 6000};
        constexpr std::array<double, 18> db{-36, -14, -4, -6, -3, 0, -2, 0, -10, -9, -11, -17, -22, -22, -27, -36, -46, -60};
        const std::size_t half = kernel_size / 2;
        std::vector<double> magnitude(half + 1);
        for (std::size_t bin = 0; bin <= half; ++bin) {
            const double x = std::log(std::max(static_cast<double>(bin) * sample_hz / kernel_size, 1.0));
            double level = db.back();
            if (x <= std::log(hz.front())) {
                level = db.front();
            } else {
                for (std::size_t index = 1; index < hz.size(); ++index) {
                    const double x1 = std::log(hz[index]);
                    if (x <= x1) {
                        const double x0 = std::log(hz[index - 1]);
                        level = db[index - 1] + (db[index] - db[index - 1]) * (x - x0) / (x1 - x0);
                        break;
                    }
                }
            }
            magnitude[bin] = std::pow(10.0, level / 20);
        }
        std::vector<double> cosine(kernel_size);
        for (std::size_t index = 0; index < kernel_size; ++index)
            cosine[index] = std::cos(tau * static_cast<double>(index) / kernel_size);
        std::vector<float> result(kernel_size);
        for (std::size_t index = 0; index < kernel_size; ++index) {
            const std::size_t n = (index + half) & kernel_mask;  // centred: index half is time zero
            double sum = magnitude[0] + magnitude[half] * cosine[(half * n) & kernel_mask];
            for (std::size_t bin = 1; bin < half; ++bin)
                sum += 2 * magnitude[bin] * cosine[(bin * n) & kernel_mask];
            const double window = 0.5 - 0.5 * std::cos(tau * (static_cast<double>(index) + 0.5) / kernel_size);
            result[index] = static_cast<float>(sum / kernel_size * window);
        }
        return result;
    }
}

const std::vector<float>& exhaust_kernel() {
    static const std::vector<float> kernel = build_exhaust_kernel();
    return kernel;
}

// tanh to within a few percent, without the call: this is only the mix's soft limiter.
double soften(double x) {
    const double bounded = std::clamp(x, -3.0, 3.0);
    const double square = bounded * bounded;
    return bounded * (27 + square) / (27 + 9 * square);
}

struct Turn {
    double c{1};
    double s{};
    Turn times(const Turn& other) const { return Turn{c * other.c - s * other.s, s * other.c + c * other.s}; }
};

// Fixed phase offsets used by the engine's air and gear tones.
const double cos1 = std::cos(1.0);
const double sin1 = std::sin(1.0);
const double cos2 = std::cos(2.0);
const double sin2 = std::sin(2.0);
const double cos_second = std::cos(2.0 - tau * 0.375);
const double sin_second = std::sin(2.0 - tau * 0.375);

bool crossed(double before, double after, double mark) {
    return (before < mark && after >= mark) || (before < mark + 1 && after >= mark + 1);
}

} // namespace

struct MowerVoice::State {
    State(std::uint64_t seed, const Gains& levels) : random(seed), gains(levels), kernel(exhaust_kernel()) {
        air_high.set(80);
        air_low.set(900);
        air_top1.set(6000);
        air_top2.set(6000);
        intake_band.set(330, 1.1);
        intake_low1.set(900);
        intake_low2.set(900);
        valve1.set(500);
        valve2.set(500);
        clatter_high1.set(2500);
        clatter_high2.set(2500);
        clatter_band.set(3400, 2);
        formant1.set(260, 2);
        formant2.set(520, 2.5);
        rough_band.set(31, 1.5);
        rough2_band.set(26, 1.2);
        wind_high1.set(150);
        wind_high2.set(150);
        wind_low.set(420);
        wind_top1.set(2200);
        wind_top2.set(2200);
        squeal_band.set(2900, 9);
        clunk1.set(95, 6);
        clunk2.set(140, 7);
        clunk3.set(230, 8);
        clunk_low.set(800);
        bounce_low.set(3000);
        side_high.set(500);
        side_low.set(4000);
        load_glide.set(5);
        slow.set(0.6, random);
        medium.set(2.5, random);
        quick.set(7, random);
        bend.set(0.8, random);
        constexpr std::array<double, 8> level{1.0, 0.630957, 0.398107, 0.251189, 0.158489, 0.1, 0.0630957, 0.0398107};
        for (std::size_t spindle = 0; spindle < 3; ++spindle) {
            for (std::size_t harmonic = 0; harmonic < 8; ++harmonic) {
                const double offset = random.unit() * tau;
                blade_sine[spindle][harmonic] = level[harmonic] * std::cos(offset);
                blade_cosine[spindle][harmonic] = level[harmonic] * std::sin(offset);
            }
        }
    }

    void add_firing(double amplitude, double fraction) {
        const float early = static_cast<float>(amplitude * (1 - fraction));
        const float late = static_cast<float>(amplitude * fraction);
        float previous = 0;
        for (std::size_t index = 0; index < kernel_size; ++index) {
            const float now = kernel[index];
            ring[(ring_at + index) & kernel_mask] += early * now + late * previous;
            previous = now;
        }
    }

    void step(float& left, float& right);

    Random random;
    Gains gains;
    const std::vector<float>& kernel;
    MowerControls controls{};

    // the machine
    double rpm{};
    double throttle{};
    double blade{};          // spindle revolutions per second
    double target{1500};     // the governed speed, gliding to the lever
    double crank{crank_seconds};
    double cycle{};          // phase of the four-stroke cycle, 0..1
    bool clutch_was{};
    bool measuring{};        // calibration keeps every noise bed running
    std::size_t quiet{kernel_size * 2};

    // the sound
    std::array<float, kernel_size> ring{};
    std::size_t ring_at{};
    OnePole air_high, air_low, air_top1, air_top2;
    Band intake_band;
    OnePole intake_low1, intake_low2;
    OnePole valve1, valve2, clatter_high1, clatter_high2;
    Band clatter_band;
    std::array<double, 3> blade_phase{};
    // level x cos and sin of each harmonic's fixed phase offset
    std::array<std::array<double, 8>, 3> blade_sine{};
    std::array<std::array<double, 8>, 3> blade_cosine{};
    std::size_t knock_left{};
    Band formant1, formant2, rough_band, rough2_band;
    OnePole wind_high1, wind_high2, wind_low, wind_top1, wind_top2;
    Band squeal_band, clunk1, clunk2, clunk3;
    OnePole clunk_low, bounce_low, side_high, side_low, load_glide;
    std::array<double, bounce_mask + 1> bounce{};
    std::size_t bounce_at{};
    Wander slow, medium, quick, bend;
    double load{};

    // what calibration listens to
    struct Probe {
        double exhaust{}, air{}, intake{}, whine{}, clatter{}, drone{};
        double wind{}, squeal{}, side{}, rough{}, rough2{}, clunk{};
    } probe{};
};

void MowerVoice::State::step(float& left, float& right) {
    // Grass is never even: the load the deck feels wanders round what the lawn reports.
    load = load_glide.low(std::clamp(controls.load, 0.0, 1.0));
    const double texture = 1 + 0.3 * slow.next(random) + 0.25 * medium.next(random) + 0.12 * quick.next(random);
    const double grass = std::clamp(load * texture, 0.0, 1.1);

    // The governor. The lever moves the governed speed no faster than a hand would.
    const bool cranking = controls.ignition && crank < crank_seconds;
    const bool firing = controls.ignition && !cranking;
    if (controls.ignition)
        crank += dt;
    const double lever = std::clamp(controls.governed_rpm, 1200.0, 3900.0);
    target += std::clamp(lever - target, -1235 * dt, 810 * dt);
    const double want = std::clamp(0.1 + 0.13 * target / 3400 + 0.0022 * (target - rpm), 0.05, 1.0);
    throttle += (want - throttle) * dt / 0.15;
    const bool clutch = controls.blades;
    const double belt = rpm / 60 * blade_ratio;
    const double pull = clutch ? (belt - blade) / 0.45 : -blade / 0.9 - 4;
    const double slip = clutch ? std::max(belt - blade, 0.0) : 0.0;
    blade = std::max(blade + pull * dt, 0.0);
    double torque = firing ? 5800 * throttle : (cranking ? 0.0 : -400.0);
    if (!cranking)
        torque -= (1500 + 3600 * grass) * rpm / 3400;
    if (clutch)
        torque -= 1200 * (blade / 50) * (blade / 50) + 22 * std::max(pull, 0.0);
    rpm = std::max(rpm + torque * dt, controls.ignition ? 1.0 : 0.0);
    const double spin = rpm / rated_rpm;
    const double deck = blade / rated_blade;

    // Two cylinders 270 and 450 crank degrees apart; valves open and shut round them.
    const double advance = rpm / 120 * dt;
    const double before = cycle;
    const double after = before + advance;
    if (controls.ignition && rpm >= 150 && advance > 0) {
        const double strength = 0.45 + 0.55 * throttle;
        if (after >= 1)
            add_firing(strength * (1 + 0.05 * random.gauss()), (1 - before) / advance);
        if (crossed(before, after, 0.375))
            add_firing(0.95 * strength * (1 + 0.05 * random.gauss()), (0.375 - before) / advance);
    }
    double tick = 0;
    constexpr std::array<double, 4> valve_marks{0.1, 0.47, 0.62, 0.99};
    for (const double mark : valve_marks) {
        if (crossed(before, after, mark))
            tick += spin;
    }
    cycle = after >= 1 ? after - 1 : after;
    const double angle = tau * cycle;

    const double exhaust = ring[ring_at];
    ring[ring_at] = 0;
    ring_at = (ring_at + 1) & kernel_mask;

    // Cooling fan and shroud: steady air, barely stirred by the engine.
    // One sine and cosine of the cycle; every engine-locked tone is a power of that turn.
    const Turn once{std::cos(angle), std::sin(angle)};
    const Turn twice = once.times(once);
    const Turn thrice = twice.times(once);
    const Turn fourth = twice.times(twice);
    const Turn eighth = fourth.times(fourth);
    const Turn sixteenth = eighth.times(eighth);
    const Turn nineteenth = sixteenth.times(thrice);
    const Turn twenty_second = nineteenth.times(thrice);
    const double air = air_top2.low(air_top1.low(air_low.low(air_high.high(random.white())))) * spin * spin *
                       (1 + 0.06 * twice.c + 0.06 * (thrice.c * cos1 - thrice.s * sin1));
    // Intake: a soft gulp through the air filter for each cylinder.
    const double gulp = 1 + 0.35 * (once.c * cos2 - once.s * sin2) + 0.35 * (once.c * cos_second - once.s * sin_second);
    const double intake = intake_low2.low(intake_low1.low(intake_band.run(random.white()))) * gulp * spin *
                          std::sqrt(spin) * (0.5 + 0.5 * throttle);
    const double whine = (sixteenth.s + 0.7 * (nineteenth.s * cos1 + nineteenth.c * sin1) +
                          0.7 * (twenty_second.s * cos2 + twenty_second.c * sin2)) * spin * spin;
    const double valves = std::max(valve2.low(valve1.low(tick)), 0.0);
    const double rattle = clatter_high2.high(clatter_high1.high(random.white()));
    const double clatter = (0.3 * rattle + clatter_band.run(rattle)) * valves;

    // The deck: three propellers a hair out of tune, so they beat like a
    // multi-engine aircraft, through the hollow of the deck and a throaty flutter.
    const double rough = std::max(1 + 0.45 * gains.rough * rough_band.run(random.white()), 0.1);
    const double rough2 = std::max(1 + 0.4 * gains.rough2 * rough2_band.run(random.white()), 0.1);
    const double sway = 1 + 0.012 * bend.next(random);
    const double rough_raw = (rough - 1) / 0.45;
    const double rough2_raw = (rough2 - 1) / 0.4;
    double drone = 0;
    double wind_noise = 0;
    if (blade > 0 || measuring) {
        constexpr std::array<double, 3> ratio{1.0, 0.9955, 1.006};
        double blades = 0;
        for (std::size_t spindle = 0; spindle < 3; ++spindle) {
            double& phase = blade_phase[spindle];
            phase += tau * 2 * blade * ratio[spindle] * sway * dt;
            if (phase >= tau)
                phase -= tau;
            const Turn pass{std::cos(phase), std::sin(phase)};
            Turn harmonic = pass;
            for (std::size_t index = 0; index < 8; ++index) {
                blades += harmonic.s * blade_sine[spindle][index] + harmonic.c * blade_cosine[spindle][index];
                harmonic = harmonic.times(pass);
            }
        }
        blades *= deck * deck;
        drone = (0.5 * blades + formant1.run(blades) + 0.7 * formant2.run(blades)) * rough;
        wind_noise = wind_top2.low(wind_top1.low(wind_low.low(wind_high2.high(wind_high1.high(random.white())))));
    }
    const double wind = gains.wind * wind_noise * deck * deck * std::sqrt(deck);
    const double grip = std::clamp(slip / 30, 0.0, 1.0);
    const double squeal_noise = grip > 0 || measuring ? squeal_band.run(random.white()) : 0.0;
    const double squeal = gains.squeal * squeal_noise * grip * std::sqrt(grip);
    const double knock = clutch == clutch_was ? 0.0 : (clutch ? 1.0 : 0.5);
    clutch_was = clutch;
    if (knock != 0)
        knock_left = MowerVoice::sample_rate;  // the deck rings for well under a second
    double clunk = 0;
    if (knock_left > 0) {
        --knock_left;
        clunk = clunk_low.low(clunk1.run(knock) + clunk2.run(knock) + 0.8 * clunk3.run(knock));
    }

    const double mono = gains.exhaust * exhaust + mix_air * gains.air * air + mix_intake * gains.intake * intake +
                        mix_whine * gains.whine * whine + mix_clatter * gains.clatter * clatter +
                        gains.drone * drone * (0.25 + 1.7 * grass) + wind * rough2 * (0.2 + 1.2 * grass) +
                        mix_squeal * squeal + mix_clunk * gains.clunk * clunk;
    bounce[bounce_at] = bounce_low.low(mono);
    const double heard = mono + 0.25 * bounce[(bounce_at + bounce_mask + 1 - bounce_delay) & bounce_mask];
    bounce_at = (bounce_at + 1) & bounce_mask;
    const double side_noise = side_low.low(side_high.high(random.white()));
    const double side = mix_side * gains.side * side_noise * (spin * spin + grass);
    left = static_cast<float>(soften((heard + side) * drive) * ceiling);
    right = static_cast<float>(soften((heard - side) * drive) * ceiling);

    if (!controls.ignition && rpm < 1.5)
        rpm = 0;
    quiet = (!controls.ignition && rpm == 0 && blade == 0) ? quiet + 1 : 0;
    probe = Probe{exhaust, air, intake, whine, clatter, drone, wind_noise, squeal_noise, side_noise, rough_raw, rough2_raw, clunk};
}

namespace {

double level(double sum, std::size_t count) {
    return 1 / std::sqrt(sum / static_cast<double>(count) + 1e-30);
}

} // namespace

double MowerVoice::calibration_drift() {
    constexpr std::size_t second = MowerVoice::sample_rate;
    float left = 0;
    float right = 0;
    Gains found{};

    // The noise beds, with the machine at rest.
    {
        State state(0x6d6f77, Gains{});
        state.measuring = true;
        state.controls.blades = true;  // with the engine stopped the belt holds the deck still, but its air runs
        state.clutch_was = true;
        double wind = 0, squeal = 0, side = 0, rough = 0, rough2 = 0;
        const std::size_t count = 12 * second;
        for (std::size_t index = 0; index < count + second; ++index) {
            state.step(left, right);
            if (index < second)
                continue;
            wind += state.probe.wind * state.probe.wind;
            squeal += state.probe.squeal * state.probe.squeal;
            side += state.probe.side * state.probe.side;
            rough += state.probe.rough * state.probe.rough;
            rough2 += state.probe.rough2 * state.probe.rough2;
        }
        found.wind = level(wind, count);
        found.squeal = level(squeal, count);
        found.side = level(side, count);
        found.rough = level(rough, count);
        found.rough2 = level(rough2, count);
    }
    // The engine at rated speed, then the deck spinning free.
    {
        State state(0x6d6f77, found);
        state.controls = MowerControls{true, rated_rpm, false, 0};
        state.rpm = 3550;
        state.throttle = 0.33;
        state.target = rated_rpm;
        for (std::size_t index = 0; index < second; ++index)
            state.step(left, right);
        double exhaust = 0, air = 0, intake = 0, whine = 0, clatter = 0, drone = 0;
        const std::size_t count = 3 * second;
        for (std::size_t index = 0; index < count; ++index) {
            state.step(left, right);
            exhaust += state.probe.exhaust * state.probe.exhaust;
            air += state.probe.air * state.probe.air;
            intake += state.probe.intake * state.probe.intake;
            whine += state.probe.whine * state.probe.whine;
            clatter += state.probe.clatter * state.probe.clatter;
        }
        found.exhaust = level(exhaust, count);
        found.air = level(air, count);
        found.intake = level(intake, count);
        found.whine = level(whine, count);
        found.clatter = level(clatter, count);
        state.controls.blades = true;
        state.clutch_was = true;
        state.blade = state.rpm / 60 * blade_ratio;
        for (std::size_t index = 0; index < 2 * second; ++index)
            state.step(left, right);
        const std::size_t long_count = 12 * second;
        for (std::size_t index = 0; index < long_count; ++index) {
            state.step(left, right);
            drone += state.probe.drone * state.probe.drone;
        }
        found.drone = level(drone, long_count);
    }
    // The clutch knock.
    {
        State state(0x6d6f77, Gains{});
        state.controls.blades = true;
        double peak = 0;
        for (std::size_t index = 0; index < second; ++index) {
            state.step(left, right);
            peak = std::max(peak, std::abs(state.probe.clunk));
        }
        found.clunk = 1 / peak;
    }
#ifdef MM_VOICE_PRINT_CALIBRATION
    std::printf("constexpr Gains tuned{%.5g, %.5g, %.5g, %.5g, %.5g, %.5g, %.5g, %.5g, %.5g, %.5g, %.5g, %.5g};\n",
                found.exhaust, found.air, found.intake, found.whine, found.clatter, found.drone, found.wind,
                found.squeal, found.side, found.rough, found.rough2, found.clunk);
#endif
    const std::array<double, 12> ratios{found.exhaust / tuned.exhaust, found.air / tuned.air,
                                        found.intake / tuned.intake, found.whine / tuned.whine,
                                        found.clatter / tuned.clatter, found.drone / tuned.drone,
                                        found.wind / tuned.wind,       found.squeal / tuned.squeal,
                                        found.side / tuned.side,       found.rough / tuned.rough,
                                        found.rough2 / tuned.rough2,   found.clunk / tuned.clunk};
    double worst = 0;
    for (const double ratio : ratios)
        worst = std::max(worst, std::abs(ratio - 1));
    return worst;
}

MowerVoice::MowerVoice(std::uint64_t seed) : state_(std::make_unique<State>(seed, tuned)) {}

MowerVoice::~MowerVoice() = default;

void MowerVoice::control(const MowerControls& controls) {
    State& state = *state_;
    if (controls.ignition && !state.controls.ignition) {
        // The engine catches: it is turning over, and the lever's speed is still to come.
        if (state.rpm < 350) {
            state.rpm = 350;
            state.throttle = 0.6;
            state.crank = 0;
        }
        state.target = std::min(state.target, 1500.0);
        state.quiet = 0;
    }
    state.controls = controls;
}

void MowerVoice::render(std::span<float> stereo) {
    State& state = *state_;
    const std::size_t frames = stereo.size() / 2;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (state.quiet > kernel_size + 4800) {
            stereo[2 * frame] = 0;
            stereo[2 * frame + 1] = 0;
            state.clutch_was = state.controls.blades;
            continue;
        }
        state.step(stereo[2 * frame], stereo[2 * frame + 1]);
    }
}

double MowerVoice::rpm() const {
    return (*state_).rpm;
}

double MowerVoice::blade_revs_per_second() const {
    return (*state_).blade;
}

bool MowerVoice::silent() const {
    return (*state_).quiet > kernel_size + 4800;
}

} // namespace mm
