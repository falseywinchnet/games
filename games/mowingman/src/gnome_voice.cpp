#include "gnome_voice.hpp"

#include "gnome_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// audio_src/gnome_voice.mjs and the monk choir's engine (engine/hdr.js, singer.js,
// tract.js, glottis.js, anatomy.js, vowels.js), ported expression by expression: every
// operation in JavaScript's order, every Math call through mm::js (V8's own functions),
// every Float32Array store through float. Only the paths the five performances reach
// are here: the drawn glottal pulse (not the self-oscillating folds), no formant
// tuning, no posture, ornament, melody or vowel oscillator.
namespace mm {
namespace {

constexpr double js_pi = 3.141592653589793;     // Math.PI
constexpr double sqrt1_2 = 0.7071067811865476;  // Math.SQRT1_2

// ------------------------------------------------------------------ anatomy.js
constexpr int sections = 40;
constexpr double speed_of_sound = 35000.0;
constexpr double s_velum = 0.5;
constexpr double s_lips = 0.93;
constexpr int max_nasal_sections = 64;
constexpr double tongue_root_half = 0.24;
constexpr double tongue_dorsum_half = 0.17;

double clamp(double x, double lo, double hi) {
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

double smoothstep(double edge0, double edge1, double x) {
    const double t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

struct Articulation {
    double tonguePos = 0.55;
    double tongueHeight = 0.25;
    double jaw = 0.5;
    double lipAperture = 0.6;
    double lipProtrusion = 0.0;
    double tipPos = 0.88;
    double tipClose = 0.0;
    double velum = 0.0;
    double larynx = 0.0;
    double epilarynx = 0.3;
};

// the articulators in INERTIA's key order (hdr.js)
constexpr std::array<double Articulation::*, 10> inertia_keys{
    &Articulation::lipAperture, &Articulation::lipProtrusion, &Articulation::tipClose, &Articulation::tipPos,
    &Articulation::tonguePos,   &Articulation::tongueHeight,  &Articulation::jaw,      &Articulation::velum,
    &Articulation::larynx,      &Articulation::epilarynx};
constexpr std::array<double, 10> inertia_tau{0.008, 0.02, 0.007, 0.015, 0.018, 0.016, 0.02, 0.015, 0.04, 0.03};

struct Anatomy {
    double pharynx;
    double oral;
    double areaScale;
    double nasal;
    double f0Low;
    double f0High;
};

// makeAnatomy("tenor", s)
Anatomy tenor(double s) {
    return {8.2 * s, 7.4 * s, 0.95 * s * s, 11.0 * s, 130.0, 523.0};
}

struct Segments {
    double pharynx = 0;
    double oral = 0;
    double lips = 0;
    double total = 0;
};

void segment_lengths(const Anatomy& anatomy, const Articulation& art, Segments& out) {
    double pharynx = anatomy.pharynx;
    if (art.larynx < 0.0) {
        pharynx = pharynx - 1.5 * art.larynx;
    } else {
        pharynx = pharynx - 1.0 * art.larynx;
    }
    const double lengthScale = std::sqrt(anatomy.areaScale);
    const double lips = (0.8 + 1.2 * art.lipProtrusion) * lengthScale;
    out.pharynx = pharynx;
    out.oral = anatomy.oral;
    out.lips = lips;
    out.total = pharynx + anatomy.oral + lips;
}

double physical_to_normalized(double x, const Segments& seg) {
    if (x <= seg.pharynx) {
        return s_velum * x / seg.pharynx;
    }
    const double xo = x - seg.pharynx;
    if (xo <= seg.oral) {
        return s_velum + (s_lips - s_velum) * xo / seg.oral;
    }
    const double xl = xo - seg.oral;
    return s_lips + (1.0 - s_lips) * std::min(1.0, xl / seg.lips);
}

double neutral_diameter(double s, const Articulation& art) {
    double d = 0.0;
    if (s < 0.07) {
        d = 1.05 * (1.0 - 0.5 * art.epilarynx);
    } else if (s < 0.15) {
        const double e = 1.05 * (1.0 - 0.5 * art.epilarynx);
        d = e + (2.25 - e) * smoothstep(0.07, 0.15, s);
    } else if (s < s_velum) {
        d = 2.25;
    } else {
        d = 2.25 + 0.2 * smoothstep(s_velum, 0.6, s);
    }
    if (s > s_velum) {
        const double jawFactor = 0.6 + 0.62 * art.jaw;
        const double weight = smoothstep(s_velum, 0.75, s);
        d = d * (1.0 + (jawFactor - 1.0) * weight);
    }
    return d;
}

double articulated_diameter(double s, const Articulation& art) {
    double d = neutral_diameter(s, art);
    const double fronting = clamp((art.tonguePos - 0.5) / 0.3, -1.0, 1.0);
    if (s >= 0.15 && s < art.tonguePos - 0.1) {
        d = d * (1.0 + 0.35 * fronting * (0.4 + 0.6 * art.tongueHeight));
    }
    if (s > art.tonguePos + 0.1 && s < s_lips) {
        double scale = 0.25;
        if (fronting < 0.0) {
            scale = 0.3;
        }
        d = d * (1.0 - scale * fronting * (0.4 + 0.6 * art.tongueHeight));
    }
    const double halfWidth =
        tongue_root_half + (tongue_dorsum_half - tongue_root_half) * smoothstep(0.3, 0.6, art.tonguePos);
    const double ds = s - art.tonguePos;
    if (ds > -halfWidth && ds < halfWidth) {
        const double centre = neutral_diameter(art.tonguePos, art) * (1.0 - 0.93 * art.tongueHeight);
        const double c = js::cos(0.5 * js_pi * ds / halfWidth);
        const double target = d - (d - centre) * c * c;
        if (target < d) {
            d = target;
        }
    }
    const double tipHalf = 0.05;
    const double dt = s - art.tipPos;
    if (art.tipClose > 0.0 && dt > -tipHalf && dt < tipHalf) {
        const double c = js::cos(0.5 * js_pi * dt / tipHalf);
        d = d * (1.0 - 0.97 * art.tipClose * c * c);
    }
    if (s >= s_lips) {
        const double lipD = 2.9 * art.lipAperture * (0.55 + 0.45 * art.jaw);
        const double w = smoothstep(s_lips - 0.02, s_lips + 0.03, s);
        d = d * (1.0 - w) + lipD * w;
    }
    if (d < 0.0) {
        d = 0.0;
    }
    return d;
}

using Areas = std::array<double, sections>;
using NasalAreas = std::array<double, max_nasal_sections>;

double area_function(const Anatomy& anatomy, const Articulation& art, Areas& areas, Segments& seg) {
    segment_lengths(anatomy, art, seg);
    const double dx = seg.total / sections;
    for (int i = 0; i < sections; i = i + 1) {
        const double x = (i + 0.5) * dx;
        const double s = physical_to_normalized(x, seg);
        const double d = articulated_diameter(s, art);
        areas[i] = 0.25 * js_pi * d * d * anatomy.areaScale;
    }
    return seg.total;
}

int velum_junction(const Segments& seg) {
    const double dx = seg.total / sections;
    int j = static_cast<int>(js::round(seg.pharynx / dx));
    if (j < 2) {
        j = 2;
    }
    if (j > sections - 2) {
        j = sections - 2;
    }
    return j;
}

int nasal_areas(const Anatomy& anatomy, const Segments& seg, NasalAreas& out) {
    const double dx = seg.total / sections;
    int n = static_cast<int>(js::round(anatomy.nasal / dx));
    if (n > max_nasal_sections) {
        n = max_nasal_sections;
    }
    if (n < 4) {
        n = 4;
    }
    for (int i = 0; i < n; i = i + 1) {
        const double u = (i + 0.5) / n;
        double d = 0.9 + 1.0 * js::sin(js_pi * std::min(1.0, u * 1.25));
        if (u > 0.85) {
            d = 0.95;
        }
        out[i] = 0.25 * js_pi * d * d * anatomy.areaScale;
    }
    return n;
}

double velum_area(const Anatomy& anatomy, const Articulation& art) {
    return 0.45 * art.velum * anatomy.areaScale;
}

double lip_radius(const Areas& areas) {
    const double a = areas[sections - 1];
    return std::sqrt(std::max(a, 1e-6) / js_pi);
}

// ------------------------------------------------------------------ vowels.js
struct VowelShape {
    double tonguePos, tongueHeight, jaw, lipAperture, lipProtrusion;
};
constexpr std::array<VowelShape, 5> vowel_shapes{{
    {0.2946, 0.95, 0.2367, 0.2439, 0.8753},   // ooh
    {0.3789, 0.8134, 0.514, 0.4109, 0.5999},  // ow
    {0.3049, 0.7458, 0.9983, 1, 0},           // ah
    {0.7506, 0.6033, 0.445, 0.582, 0.0677},   // ayh
    {0.7282, 0.7715, 0.3, 0.7612, 0.0007},    // eeh
}};

double catmull_rom(double p0, double p1, double p2, double p3, double t) {
    const double t2 = t * t;
    const double t3 = t2 * t;
    return 0.5 * (2.0 * p1 + (-p0 + p2) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                  (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

void vowel_articulation(double vowel, Articulation& out) {
    const double pos = clamp(vowel, 0.0, 1.0) * 4.0;
    int seg = static_cast<int>(std::floor(pos));
    if (seg > 3) {
        seg = 3;
    }
    const double t = pos - seg;
    const VowelShape& a = vowel_shapes[std::max(seg - 1, 0)];
    const VowelShape& b = vowel_shapes[seg];
    const VowelShape& c = vowel_shapes[seg + 1];
    const VowelShape& d = vowel_shapes[std::min(seg + 2, 4)];
    out.tonguePos = catmull_rom(a.tonguePos, b.tonguePos, c.tonguePos, d.tonguePos, t);
    out.tongueHeight = catmull_rom(a.tongueHeight, b.tongueHeight, c.tongueHeight, d.tongueHeight, t);
    out.jaw = catmull_rom(a.jaw, b.jaw, c.jaw, d.jaw, t);
    out.lipAperture = catmull_rom(a.lipAperture, b.lipAperture, c.lipAperture, d.lipAperture, t);
    out.lipProtrusion = catmull_rom(a.lipProtrusion, b.lipProtrusion, c.lipProtrusion, d.lipProtrusion, t);
    out.tonguePos = clamp(out.tonguePos, 0.18, 0.86);
    out.tongueHeight = clamp(out.tongueHeight, 0.0, 0.97);
    out.jaw = clamp(out.jaw, 0.0, 1.0);
    out.lipAperture = clamp(out.lipAperture, 0.03, 1.0);
    out.lipProtrusion = clamp(out.lipProtrusion, 0.0, 1.0);
}

// ------------------------------------------------------------------ tract.js
constexpr double section_loss = 0.9993;
constexpr double lip_reflection = 0.97;
constexpr double nose_reflection = 0.95;

double port_loss(double velumArea) {
    const double r = std::sqrt(std::max(velumArea, 0.0) / js_pi);
    if (r <= 0.02) {
        return 0.0;
    }
    return 1.0 - 0.02 / r;
}

double junction_coefficient(double a1, double a2) {
    const double sum = a1 + a2;
    if (sum <= 1e-12) {
        return 0.0;
    }
    return (a1 - a2) / sum;
}

double radiation_pole(double radius, double tickRate, double speedOfSound) {
    double corner = 0.6 * speedOfSound / (2.0 * js_pi * std::max(radius, 0.05));
    if (corner < 1200.0) {
        corner = 1200.0;
    }
    if (corner > 0.45 * tickRate) {
        corner = 0.45 * tickRate;
    }
    return js::exp(-2.0 * js_pi * corner / tickRate);
}

class Tract {
  public:
    std::array<double, sections> R{}, L{}, outR{};
    std::array<double, sections + 1> outL{};
    std::array<double, sections> areas{};
    std::array<double, sections + 1> rho{}, rhoStart{}, rhoTarget{}, cc{}, ccStart{}, ccTarget{};
    std::array<double, max_nasal_sections> nR{}, nL{}, nOutR{};
    std::array<double, max_nasal_sections + 1> nOutL{};
    std::array<double, max_nasal_sections> nAreas{};
    std::array<double, max_nasal_sections + 1> nRho{};
    int nCount = 24;
    int velumJ = 20;
    double velumArea = 0.0, velumStart = 0.0, velumTarget = 0.0;
    double areaLeftOfVelum = 1.0, areaRightOfVelum = 1.0;
    double leftStart = 1.0, leftTarget = 1.0, rightStart = 1.0, rightTarget = 1.0;
    double lipArea = 1.0, lipAreaStart = 1.0, lipAreaTarget = 1.0;
    double inputScaleStart = 1.0, inputScaleTarget = 1.0;
    double lipPole = 0.5, lipState = 0.0, nosePole = 0.5, noseState = 0.0;
    double loss = section_loss;
    int rampTicks = 0;
    int rampPos = 0;
    double nasalEnergy = 0.0;
    double inputScale = 1.0;
    bool hasShape = false;
    int injIndex = -1;
    double injValue = 0.0;

    double glottalPressure() const {
        return (R[0] + L[0]) * std::sqrt(inputScale);
    }

    double pressureAt(int i) const {
        return (R[i] + L[i]) / std::sqrt(std::max(areas[i], 1e-9));
    }

    void setShape(const Areas& a, int nasalCount, const NasalAreas& nasal, int vj, double va, double lp, double np,
                  int ramp) {
        const int n = sections;
        for (int i = 0; i < n; i = i + 1) {
            areas[i] = a[i];
        }
        for (int i = 1; i < n; i = i + 1) {
            const double r = junction_coefficient(a[i - 1], a[i]);
            rhoTarget[i] = r;
            ccTarget[i] = std::sqrt(std::max(0.0, 1.0 - r * r));
        }
        const bool noseSounding = va > 0.0 || velumArea > 0.0 || nasalEnergy > 1e-6;
        if (!noseSounding || !hasShape) {
            nCount = nasalCount;
            velumJ = vj;
            for (int i = 0; i < nasalCount; i = i + 1) {
                nAreas[i] = nasal[i];
            }
            for (int i = 1; i < nasalCount; i = i + 1) {
                nRho[i] = junction_coefficient(nasal[i - 1], nasal[i]);
            }
        }
        vj = velumJ;
        leftTarget = a[vj - 1];
        rightTarget = a[vj];
        lipAreaTarget = a[n - 1];
        inputScaleTarget = 1.0 / std::max(a[0], 0.05);
        lipPole = lp;
        nosePole = np;
        if (!hasShape || ramp <= 0) {
            lipArea = lipAreaTarget;
            lipAreaStart = lipAreaTarget;
            areaLeftOfVelum = leftTarget;
            leftStart = leftTarget;
            areaRightOfVelum = rightTarget;
            rightStart = rightTarget;
            inputScale = inputScaleTarget;
            inputScaleStart = inputScaleTarget;
            for (int i = 1; i < n; i = i + 1) {
                rho[i] = rhoTarget[i];
                rhoStart[i] = rhoTarget[i];
                cc[i] = ccTarget[i];
                ccStart[i] = ccTarget[i];
            }
            velumArea = va;
            velumStart = va;
            velumTarget = va;
            rampTicks = 0;
            rampPos = 0;
            hasShape = true;
            return;
        }
        for (int i = 1; i < n; i = i + 1) {
            rhoStart[i] = rho[i];
            ccStart[i] = cc[i];
        }
        velumStart = velumArea;
        velumTarget = va;
        lipAreaStart = lipArea;
        inputScaleStart = inputScale;
        leftStart = areaLeftOfVelum;
        rightStart = areaRightOfVelum;
        rampTicks = ramp;
        rampPos = 0;
    }

    void advanceRamp() {
        rampPos = rampPos + 1;
        const double t = static_cast<double>(rampPos) / rampTicks;
        const int n = sections;
        for (int i = 1; i < n; i = i + 1) {
            rho[i] = rhoStart[i] + (rhoTarget[i] - rhoStart[i]) * t;
            cc[i] = ccStart[i] + (ccTarget[i] - ccStart[i]) * t;
        }
        velumArea = velumStart + (velumTarget - velumStart) * t;
        lipArea = lipAreaStart + (lipAreaTarget - lipAreaStart) * t;
        areaLeftOfVelum = leftStart + (leftTarget - leftStart) * t;
        areaRightOfVelum = rightStart + (rightTarget - rightStart) * t;
        inputScale = inputScaleStart + (inputScaleTarget - inputScaleStart) * t;
        if (rampPos >= rampTicks) {
            rampTicks = 0;
        }
    }

    double tick(double glottalFlow, double glottalReflection) {
        if (rampTicks > 0) {
            advanceRamp();
        }
        const int n = sections;
        const int vj = velumJ;
        outR[0] = glottalReflection * L[0] + glottalFlow * std::sqrt(inputScale);
        const double sLip = std::sqrt(std::max(lipArea, 1e-9));
        const double endWave = R[n - 1] / sLip;
        lipState = (1.0 - lipPole) * endWave + lipPole * lipState;
        const double lipReflected = -lip_reflection * lipState;
        outL[n] = lipReflected * sLip;
        double radiated = lipArea * (endWave - lipReflected);
        for (int i = 1; i < n; i = i + 1) {
            if (i == vj) {
                continue;
            }
            const double k = rho[i];
            const double c = cc[i];
            outR[i] = c * R[i - 1] - k * L[i];
            outL[i] = k * R[i - 1] + c * L[i];
        }
        const bool useNose = velumArea > 0.0 || nasalEnergy > 1e-10;
        const double aL = areaLeftOfVelum;
        const double aR = areaRightOfVelum;
        if (useNose) {
            const int nc = nCount;
            const double aN = velumArea;
            const double sL = std::sqrt(std::max(aL, 1e-9));
            const double sR = std::sqrt(std::max(aR, 1e-9));
            const double pLeft = R[vj - 1] / sL;
            const double pRight = L[vj] / sR;
            const double pJ = 2.0 * (aL * pLeft + aR * pRight + aN * nL[0]) / (aL + aR + aN);
            outR[vj] = (pJ - pRight) * sR;
            outL[vj] = (pJ - pLeft) * sL;
            nOutR[0] = pJ - nL[0];
            const double port = junction_coefficient(aN, nAreas[1]);
            for (int k = 1; k < nc; k = k + 1) {
                double r = nRho[k];
                if (k == 1) {
                    r = port;
                }
                const double w = r * (nR[k - 1] - nL[k]);
                nOutR[k] = nR[k - 1] + w;
                nOutL[k] = nL[k] + w;
            }
            const double noseEnd = nR[nc - 1];
            noseState = (1.0 - nosePole) * noseEnd + nosePole * noseState;
            const double noseReflected = -nose_reflection * noseState;
            nOutL[nc] = noseReflected;
            radiated = radiated + nAreas[nc - 1] * (noseEnd - noseReflected);
            double energy = 0.0;
            const double lossPort = loss * port_loss(aN);
            for (int k = 0; k < nc; k = k + 1) {
                if (k == 0) {
                    nR[0] = nOutR[0] * lossPort;
                    nL[0] = nOutL[1] * lossPort;
                    continue;
                }
                nR[k] = nOutR[k] * loss;
                nL[k] = nOutL[k + 1] * loss;
                energy = energy + nR[k] * nR[k] + nL[k] * nL[k];
            }
            nasalEnergy = energy;
            if (energy <= 1e-10 && aN <= 0.0) {
                nR.fill(0.0);
                nL.fill(0.0);
                nasalEnergy = 0.0;
            }
        } else {
            const double k = rho[vj];
            const double c = cc[vj];
            outR[vj] = c * R[vj - 1] - k * L[vj];
            outL[vj] = k * R[vj - 1] + c * L[vj];
        }
        if (injIndex > 0) {
            const int ii = injIndex;
            outR[ii] = outR[ii] + 0.5 * injValue * std::sqrt(std::max(areas[ii], 1e-9));
            outL[ii] = outL[ii] - 0.5 * injValue * std::sqrt(std::max(areas[ii - 1], 1e-9));
        }
        for (int i = 0; i < n; i = i + 1) {
            R[i] = outR[i] * loss;
            L[i] = outL[i + 1] * loss;
        }
        return radiated;
    }
};

// ------------------------------------------------------------------ glottis.js
struct Source {
    double oq, skew, qa, leak, breath, jitter, shimmer, vent;
    int ventRatio = 2;
    double direction = 1.0;
    double voicing = 1.0;
};

// The register fields a performance sets (a JavaScript object spread over a register).
struct Register {
    double oq, skew, qa, leak, breath, jitter, shimmer, vent;
};
constexpr Register reg_chest{0.5, 0.72, 0.06, 0.02, 0.025, 0.004, 0.03, 0.0};
constexpr Register reg_head{0.62, 0.68, 0.12, 0.05, 0.045, 0.004, 0.03, 0.0};
constexpr Register reg_falsetto{0.85, 0.58, 0.3, 0.16, 0.09, 0.005, 0.035, 0.0};
constexpr Register reg_breathy{0.78, 0.62, 0.25, 0.28, 0.2, 0.006, 0.04, 0.0};
constexpr double unvoiced_breath = 0.35;

Source make_source(const Register& r) {
    Source s{r.oq, r.skew, r.qa, r.leak, r.breath, r.jitter, r.shimmer, r.vent};
    return s;
}

std::uint32_t xorshift(std::uint32_t x) {
    x = x ^ (x << 13);
    x = x ^ (x >> 17);
    x = x ^ (x << 5);
    return x;
}

// (seed * constant) >>> 0 for the small integer seeds used here (the product is exact)
std::uint32_t to_uint32(double product) {
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) & 0xffffffffull);
}

class Glottis {
  public:
    explicit Glottis(int seed) : noiseState(static_cast<std::uint32_t>(seed) != 0 ? static_cast<std::uint32_t>(seed) : 1u) {}

    double phase = 0.0;
    int parity = 0;
    int cycleCount = 0;
    double cycleRate = 1.0;
    double cycleAmp = 1.0;
    double returnState = 0.0;
    std::uint32_t noiseState;
    double noiseLow = 0.0;
    double noiseHigh = 0.0;
    double flow = 0.0;
    double reflection = 0.97;
    double opening = 0.0;

    double random() {
        noiseState = xorshift(noiseState);
        return noiseState / 4294967296.0;
    }

    double gaussianish() {
        const double a = random();
        const double b = random();
        const double c = random();
        return (a + b + c - 1.5) * 2.0;
    }

    void startCycle(const Source& src) {
        parity = 1 - parity;
        const int ratio = src.ventRatio >= 1 ? src.ventRatio : 2;
        cycleCount = (cycleCount + 1) % 6;
        double jitter = src.jitter;
        double shimmer = src.shimmer;
        if (src.direction < 0.0) {
            jitter = jitter * 2.5;
            shimmer = shimmer * 1.8;
        }
        cycleRate = 1.0 + jitter * gaussianish();
        cycleAmp = 1.0 + shimmer * gaussianish();
        if (src.vent > 0.0 && ratio > 1) {
            const int k = cycleCount % ratio;
            cycleRate = cycleRate * (1.0 + 0.06 * src.vent * js::cos(2.0 * js_pi * (k + 1) / ratio));
        }
    }

    void tick(double f0, double tickRate, const Source& src, double effort, double amp) {
        phase = phase + f0 * cycleRate / tickRate;
        if (phase >= 1.0) {
            phase = phase - std::floor(phase);
            startCycle(src);
        }
        const double p = phase;
        const double oq = src.oq;
        double u = 0.0;
        if (src.direction >= 0.0) {
            const double tOpen = oq * src.skew;
            if (p < tOpen) {
                u = 0.5 * (1.0 - js::cos(js_pi * p / tOpen));
            } else if (p < oq) {
                u = js::cos(0.5 * js_pi * (p - tOpen) / (oq - tOpen));
            }
        } else {
            const double tSnap = oq * (1.0 - src.skew);
            if (p < tSnap) {
                u = js::sin(0.5 * js_pi * p / tSnap);
            } else if (p < oq) {
                u = 0.5 * (1.0 + js::cos(js_pi * (p - tSnap) / (oq - tSnap)));
            }
        }
        if (src.vent > 0.0) {
            const int ratio = src.ventRatio >= 1 ? src.ventRatio : 2;
            const double position = ((cycleCount % ratio) + p) / ratio;
            const double s = js::sin(js_pi * position);
            u = u * (1.0 - src.vent * s * s);
        }
        u = u * src.voicing;
        const double qa = src.qa * (1.45 - 0.9 * effort);
        const double ta = std::max(qa / f0, 1.0 / tickRate);
        const double a = js::exp(-1.0 / (ta * tickRate));
        returnState = (1.0 - a) * u + a * returnState;
        const double open = returnState + src.leak + 0.35 * (1.0 - src.voicing);
        opening = open;
        const double white = random() * 2.0 - 1.0;
        noiseLow = noiseLow + (white - noiseLow) * (1.0 - js::exp(-2.0 * js_pi * 2500.0 / tickRate));
        double breath = src.breath + unvoiced_breath * (1.0 - src.voicing);
        if (src.direction < 0.0) {
            breath = breath * 1.8;
        }
        const double noise = noiseLow * breath * 0.2 * (0.2 + open);
        flow = src.direction * amp * cycleAmp * (open + noise);
        double r = 0.985 - 0.6 * open;
        if (r < 0.35) {
            r = 0.35;
        }
        reflection = r;
    }
};

// ------------------------------------------------------------------ singer.js
struct Lowpass {
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

void design_lowpass(double cutoff, double rate, Lowpass& c) {
    const double k = js::tan(js_pi * std::min(cutoff, 0.49 * rate) / rate);
    const double q = sqrt1_2;
    const double norm = 1.0 / (1.0 + k / q + k * k);
    c.b0 = k * k * norm;
    c.b1 = 2.0 * c.b0;
    c.b2 = c.b0;
    c.a1 = 2.0 * (k * k - 1.0) * norm;
    c.a2 = (1.0 - k / q + k * k) * norm;
}

double approach_asym(double x, double target, double dt, double riseTau, double fallTau) {
    double tau = fallTau;
    if (target > x) {
        tau = riseTau;
    }
    return x + (target - x) * (1.0 - js::exp(-dt / tau));
}

double settle(double x, double target) {
    if (std::fabs(x - target) < 1e-4) {
        return target;
    }
    return x;
}

double note_to_hz(double note) {
    return 440.0 * js::pow(2.0, (note - 69.0) / 12.0);
}

// ------------------------------------------------------------------ hdr.js
constexpr int max_sub = 64;

class SubglottalTube {
  public:
    std::array<double, max_sub> R{}, L{}, outR{};
    std::array<double, max_sub + 1> outL{}, rho{};
    int n = 0;
    double topArea = 2.5;
    double lungReflection = -0.6;
    double loss = 0.9985;
    double length = 0.0;

    void configure(double len, double dx, double areaScale) {
        int count = static_cast<int>(js::round(len / dx));
        if (count < 8) {
            count = 8;
        }
        if (count > max_sub) {
            count = max_sub;
        }
        if (count == n) {
            return;
        }
        n = count;
        length = n * dx;
        double prev = 0.0;
        for (int i = 0; i < n; i = i + 1) {
            const double u = (i + 0.5) / n;
            double a = 2.5;
            if (u > 0.55) {
                a = 2.5 + 12.5 * (u - 0.55) / 0.45;
            }
            a = a * areaScale;
            if (i == 0) {
                topArea = a;
            } else {
                rho[i] = (prev - a) / (prev + a);
            }
            prev = a;
        }
        R.fill(0.0);
        L.fill(0.0);
    }

    double pressureTop() const {
        return R[0] + L[0];
    }

    void tick(double inflow, double topReflection) {
        outR[0] = topReflection * L[0] + inflow / topArea;
        outL[n] = lungReflection * R[n - 1];
        for (int i = 1; i < n; i = i + 1) {
            const double w = rho[i] * (R[i - 1] - L[i]);
            outR[i] = R[i - 1] + w;
            outL[i] = L[i] + w;
        }
        for (int i = 0; i < n; i = i + 1) {
            R[i] = outR[i] * loss;
            L[i] = outL[i + 1] * loss;
        }
    }
};

class Resonator {
  public:
    double c1 = 0.0, c2 = 0.0, y1 = 0.0, y2 = 0.0, g = 0.0;

    void tune(double freq, double q, double rate) {
        const double f = std::min(freq, 0.45 * rate);
        const double r = js::exp(-js_pi * (f / q) / rate);
        c1 = 2.0 * r * js::cos(2.0 * js_pi * f / rate);
        c2 = -r * r;
        g = 1.0 - r;
    }

    double tick(double x) {
        const double y = c1 * y1 + c2 * y2 + g * x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

class Band {
  public:
    double b0 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    void tune(double f, double q, double rate) {
        const double w = 2.0 * js_pi * std::min(f, 0.45 * rate) / rate;
        const double alpha = js::sin(w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = alpha / a0;
        b2 = -alpha / a0;
        a1 = -2.0 * js::cos(w) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    double tick(double x) {
        const double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

struct Bus {
    double prevFlow = 0.0, x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0, prev = 0.0, cur = 0.0;
};

constexpr int control_block = 64;

// HdrVoice (and the Singer it extends), as the gnome's performances drive it.
class HdrVoice {
  public:
    HdrVoice(double rate, const Anatomy& body, const Register& registerKey, int seed_)
        : sampleRate(rate),
          anatomy(body),
          glottis(seed_),
          source(make_source(registerKey)),
          sourceTarget(make_source(registerKey)),
          seed(seed_),
          randomState(to_uint32(seed_ * 2654435761.0)),
          noiseSeed(to_uint32(seed_ * 2246822519.0) != 0 ? to_uint32(seed_ * 2246822519.0) : 7u) {
        vibratoPhase = (seed % 997) / 997.0;
        tickRate = sections * speed_of_sound / 17.0;
        calibrate();
        subLength = 20.0 * std::sqrt(anatomy.areaScale);
        tremorPhase = (seed % 101) / 101.0;
        heartPhase = (seed % 37) / 37.0;
        calibrateInteraction();
    }

    // ---- Singer fields
    double sampleRate;
    Anatomy anatomy;
    Glottis glottis;
    Tract tract{};
    Source source;
    Source sourceTarget;
    double effort = 0.6;
    double level = 1.0;
    double pan = 0.0;
    int seed;
    double vowel = 0.5, vowelTarget = 0.5, vowelGlide = 0.06;
    double stopTip = 0.0, stopTipTarget = 0.0, velumOpen = 0.0, velumOpenTarget = 0.0;
    double epilarynx = 0.3;
    double larynx = 0.0;
    Articulation base{}, art{};
    double note = 48.0, noteTarget = 48.0, glideRate = 40.0, bend = 0.0;
    double vibratoRate = 5.2, vibratoDepth = 0.25, vibratoPhase = 0.0, vibratoOnset = 0.0;
    double drift = 0.0, driftCents = 6.0, detune = 0.0, noteOffset = 0.0;
    std::uint32_t randomState;
    bool gate = false;
    double velocity = 1.0, envelope = 0.0, attack = 0.08, release = 0.25;
    Areas areas{};
    NasalAreas nasal{};
    int nasalCount = 24;
    Segments seg{};
    double length = 17.0;
    double tickRate;
    double tickAcc = 0.0;
    Lowpass lp{};
    double lpRate = 0.0;
    double calibration = 1.0;
    double f0 = 110.0;

    // ---- HdrVoice fields
    Tract shadowTract{}, highTract{};
    SubglottalTube sub{};
    double subLength = 0.0;
    double trapped = 0.0, sealPrev = 0.0, burst = 0.0;
    int burstIndex = -1;
    Band burstBand{};
    double wallLow = 0.0;
    std::array<double, 10> dynPos{}, dynVel{};
    bool dynReady = false;
    double amountNoise = 1.0, amountImpact = 1.0, amountCrossmodes = 1.0, amountLiving = 1.0;
    double kappa = 0.0, flowU = 0.0, inertiaTau = 0.00025, prevOpening = 0.0;
    Band band{};
    int constrictIndex = -1;
    double noiseGain = 0.0, noiseCentre = 0.0, flowAverage = 0.0, openingAverage = 0.3, flowThrough = 1.0;
    double aspLow = 0.0, aspirationLevel = 0.2;
    std::uint32_t noiseSeed;
    double clickPending = 0.0;
    std::array<Resonator, 3> cross{};
    std::array<int, 3> crossIndex{0, 0, 0};
    double ps = 1.0, psDrift = 0.0, tremorPhase = 0.0, tremorAmp = 0.0, heartPhase = 0.0, breathSag = 0.0;
    double f0Living = 1.0, oqLiving = 0.0;
    std::array<Bus, 3> bus{};
    double p0Ratio = 0.0;
    // gnome_voice.mjs: the glottal settings the folds move toward
    Register goal{};

    double rand() {
        randomState = xorshift(randomState);
        return randomState / 4294967296.0;
    }

    double randomUniform() {
        noiseSeed = xorshift(noiseSeed);
        return noiseSeed / 4294967296.0;
    }

    double gaussian() {
        const double a = randomUniform();
        const double b = randomUniform();
        const double c = randomUniform();
        const double d = randomUniform();
        return (a + b + c + d - 2.0) * 1.2247;
    }

    void calibrate() {
        Articulation a{};
        vowel_articulation(0.5, a);
        const double len = area_function(anatomy, a, areas, seg);
        const double rate = sections * speed_of_sound / len;
        const int nc = nasal_areas(anatomy, seg, nasal);
        nasalCount = nc;
        Tract t{};
        Glottis g(12345);
        const Source src = make_source(reg_chest);
        const double pole = radiation_pole(lip_radius(areas), rate, speed_of_sound);
        t.setShape(areas, nc, nasal, velum_junction(seg), 0.0, pole, 0.5, 0);
        Lowpass l{};
        design_lowpass(0.45 * sampleRate, rate, l);
        const double f = std::sqrt(anatomy.f0Low * anatomy.f0High);
        const int ticks = static_cast<int>(js::round(0.2 * rate));
        const int skip = static_cast<int>(js::round(0.05 * rate));
        double prev = 0.0;
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
        double sum = 0.0;
        for (int i = 0; i < ticks; i = i + 1) {
            g.tick(f, rate, src, 0.6, 1.0);
            const double flow = t.tick(g.flow, g.reflection);
            const double radiated = flow - prev;
            prev = flow;
            const double y = l.b0 * radiated + l.b1 * x1 + l.b2 * x2 - l.a1 * y1 - l.a2 * y2;
            x2 = x1;
            x1 = radiated;
            y2 = y1;
            y1 = y;
            if (i >= skip) {
                sum = sum + y * y;
            }
        }
        const double rms = std::sqrt(sum / (ticks - skip));
        calibration = 0.25 / std::max(rms, 1e-9);
    }

    void calibrateInteraction() {
        Tract t{};
        const double rate = tickRate;
        updateArticulation(0.0);
        updateShape(0);
        t.setShape(areas, nasalCount, nasal, 20, 0.0, 0.5, 0.5, 0);
        double peak = 1e-9;
        double phase = 0.0;
        const double f = 130.0;
        const int count = static_cast<int>(js::round(0.1 * rate));
        for (int i = 0; i < count; i = i + 1) {
            phase = phase + f / rate;
            if (phase >= 1.0) {
                phase = phase - 1.0;
            }
            const double u = phase < 0.5 ? 0.5 - 0.5 * js::cos(2.0 * js_pi * phase / 0.5 * 0.5) : 0.0;
            t.tick(u, 0.9);
            peak = std::max(peak, std::fabs(t.glottalPressure()));
        }
        kappa = 0.25 / peak;
    }

    void noteOn(double n, double vel) {
        const bool wasSounding = gate || envelope > 0.01;
        noteTarget = n;
        if (!wasSounding) {
            note = n - 0.6 * rand();
            vibratoOnset = 0.0;
        }
        velocity = vel;
        gate = true;
    }

    void noteOff() {
        gate = false;
    }

    void updateEnvelope(double dt) {
        double target = 0.0;
        double tau = release;
        if (gate) {
            target = 1.0;
            tau = attack;
        }
        envelope = envelope + (target - envelope) * (1.0 - js::exp(-dt / std::max(tau, 0.002)));
    }

    void updatePhysiology(double dt) {
        const double a = amountLiving;
        if (gate) {
            breathSag = breathSag + dt * 0.012;
        } else {
            breathSag = breathSag * js::exp(-dt / 0.6);
        }
        const double sag = 0.06 * js::tanh(breathSag / 0.06);
        psDrift = psDrift - psDrift * dt / 0.7 + 0.018 * std::sqrt(2.0 * dt / 0.7) * gaussian();
        heartPhase = heartPhase + 1.15 * dt;
        const double heart = 0.006 * js::sin(2.0 * js_pi * heartPhase);
        ps = 1.0 + a * (psDrift + heart - sag);
        tremorAmp = tremorAmp - tremorAmp * dt / 1.5 + 0.002 * std::sqrt(2.0 * dt / 1.5) * gaussian();
        tremorPhase = tremorPhase + (5.6 + 0.6 * psDrift * 20.0) * dt;
        const double tremor = (0.0025 + std::fabs(tremorAmp)) * js::sin(2.0 * js_pi * tremorPhase);
        f0Living = 1.0 + a * (0.3 * (ps - 1.0) + tremor);
        oqLiving = -a * 0.6 * (ps - 1.0);
    }

    void updatePitch(double dt) {
        const double diff = noteTarget - note;
        const double step = glideRate * dt;
        if (std::fabs(diff) <= step) {
            note = noteTarget;
        } else if (diff > 0.0) {
            note = note + step;
        } else {
            note = note - step;
        }
        vibratoOnset = std::min(1.0, vibratoOnset + dt / 0.6);
        const double wander = 1.0 + 0.08 * (rand() - 0.5);
        vibratoPhase = vibratoPhase + vibratoRate * wander * dt;
        if (vibratoPhase > 1.0) {
            vibratoPhase = vibratoPhase - 1.0;
        }
        const double vib = vibratoDepth * vibratoOnset * js::sin(2.0 * js_pi * vibratoPhase);
        const double tau = 1.5;
        const double r1 = rand();
        const double r2 = rand();
        const double r3 = rand();
        const double g = (r1 + r2 + r3 - 1.5) * 2.0;
        drift = drift - drift * dt / tau + driftCents * std::sqrt(2.0 * dt / tau) * g;
        const double cents = detune + drift;
        f0 = note_to_hz(note + noteOffset + bend + vib + cents / 100.0);
    }

    void updateSource(double dt) {
        const double k = 1.0 - js::exp(-dt / 0.08);
        Source& s = source;
        const Source& t = sourceTarget;
        s.oq = s.oq + (t.oq - s.oq) * k;
        s.skew = s.skew + (t.skew - s.skew) * k;
        s.qa = s.qa + (t.qa - s.qa) * k;
        s.leak = s.leak + (t.leak - s.leak) * k;
        s.breath = s.breath + (t.breath - s.breath) * k;
        s.jitter = t.jitter;
        s.shimmer = t.shimmer;
        s.vent = s.vent + (t.vent - s.vent) * k;
        s.ventRatio = t.ventRatio;
    }

    // updateArticulation with no tuning, posture, oscillator, hum, lip stop or ornament
    // (each of those terms is exactly zero in these performances).
    void updateArticulation(double dt) {
        const double vg = 1.0 - js::exp(-dt / std::max(vowelGlide, 0.002));
        vowel = vowel + (vowelTarget - vowel) * vg;
        stopTip = approach_asym(stopTip, stopTipTarget, dt, 0.01, 0.03);
        velumOpen = approach_asym(velumOpen, velumOpenTarget, dt, 0.015, 0.04);
        stopTip = settle(stopTip, stopTipTarget);
        velumOpen = settle(velumOpen, velumOpenTarget);
        const double vowelEff = clamp(vowel + 0.0 * 0.0, 0.0, 1.0);
        vowel_articulation(vowelEff, base);
        base.tipPos = 0.88;
        base.tipClose = 0.0;
        base.velum = 0.0;
        base.epilarynx = epilarynx;
        base.larynx = larynx;
        art = base;
        if (stopTip > 0.001) {
            art.tipPos = 0.9;
            art.tipClose = std::max(art.tipClose, stopTip);
        }
        if (velumOpen > 0.001) {
            art.velum = std::max(art.velum, 0.9 * velumOpen);
        }
    }

    void updateShape(int blockTicks) {
        const double len = area_function(anatomy, art, areas, seg);
        length = len;
        tickRate = sections * speed_of_sound / len;
        const int nc = nasal_areas(anatomy, seg, nasal);
        nasalCount = nc;
        const int vj = velum_junction(seg);
        const double va = velum_area(anatomy, art);
        const double lipPole = radiation_pole(lip_radius(areas), tickRate, speed_of_sound);
        const double noseRadius = std::sqrt(nasal[nc - 1] / js_pi);
        const double nosePole = radiation_pole(noseRadius, tickRate, speed_of_sound);
        tract.setShape(areas, nc, nasal, vj, va, lipPole, nosePole, blockTicks);
        if (std::fabs(tickRate - lpRate) > 1.0) {
            design_lowpass(0.45 * sampleRate, tickRate, lp);
            lpRate = tickRate;
        }
    }

    void updateGeometry() {
        const int n = sections;
        const int start = static_cast<int>(js::round(n * 0.45));
        double minArea = 1e9;
        int minIndex = -1;
        for (int i = start; i < n; i = i + 1) {
            if (areas[i] < minArea) {
                minArea = areas[i];
                minIndex = i;
            }
        }
        constrictIndex = std::min(n - 1, minIndex + 1);
        const double ac = std::max(minArea, 1e-5);
        const double ag = 0.12 * openingAverage + 0.002;
        const double vRel = ag / std::sqrt(ag * ag + ac * ac);
        flowThrough = ac / std::sqrt(ag * ag + ac * ac);
        const double width = 2.0 * std::sqrt(ac / js_pi);
        const double excess = std::max(0.0, vRel - 0.25);
        noiseGain = amountNoise * excess * excess * 6.0;
        noiseCentre = std::min(8000.0, std::max(1200.0, 0.2 * 3600.0 * vRel / std::max(width, 0.05)));
        band.tune(noiseCentre, 1.1, tickRate);
        int widest = start;
        for (int i = start; i < n; i = i + 1) {
            if (areas[i] > areas[widest]) {
                widest = i;
            }
        }
        const double d = 2.0 * std::sqrt(areas[widest] / js_pi);
        constexpr std::array<double, 3> zeros{1.841, 3.054, 3.832};
        for (int k = 0; k < 3; k = k + 1) {
            const double f = zeros[k] * speed_of_sound / (js_pi * std::max(d, 0.5));
            crossIndex[k] = widest;
            cross[k].tune(f, 14.0, tickRate);
        }
    }

    void applyInertia(double dt) {
        vowelGlide = std::min(vowelGlide, 0.015);
        if (!dynReady) {
            for (std::size_t i = 0; i < inertia_keys.size(); i = i + 1) {
                dynPos[i] = art.*inertia_keys[i];
                dynVel[i] = 0.0;
            }
            dynReady = true;
            return;
        }
        const double h = dt * 0.5;
        for (std::size_t i = 0; i < inertia_keys.size(); i = i + 1) {
            const double w = 1.0 / inertia_tau[i];
            for (int step = 0; step < 2; step = step + 1) {
                const double acc = w * w * (art.*inertia_keys[i] - dynPos[i]) - 2.0 * w * dynVel[i];
                dynVel[i] = dynVel[i] + acc * h;
                dynPos[i] = dynPos[i] + dynVel[i] * h;
            }
            art.*inertia_keys[i] = dynPos[i];
        }
        art.lipAperture = std::max(0.0, art.lipAperture);
        art.tipClose = std::min(1.0, std::max(0.0, art.tipClose));
        art.velum = std::min(1.0, std::max(0.0, art.velum));
    }

    void busTick(Bus& b, double flow) const {
        const double radiated = flow - b.prevFlow;
        b.prevFlow = flow;
        const double y = lp.b0 * radiated + lp.b1 * b.x1 + lp.b2 * b.x2 - lp.a1 * b.y1 - lp.a2 * b.y2;
        b.x2 = b.x1;
        b.x1 = radiated;
        b.y2 = b.y1;
        b.y1 = y;
        b.prev = b.cur;
        b.cur = y;
    }

    static double busSample(const Bus& b, double frac) {
        return b.prev + (b.cur - b.prev) * frac;
    }

    // render(outL, outR, offset, n) into Float32Arrays (each store rounds to float)
    void render(std::array<float, control_block>& outL, std::array<float, control_block>& outR, int n) {
        const double dt = n / sampleRate;
        updateEnvelope(dt);
        updatePhysiology(dt);
        updatePitch(dt);
        f0 = f0 * f0Living;
        updateSource(dt);
        source.oq = std::min(0.95, std::max(0.2, source.oq + oqLiving * 0.1));
        updateArticulation(dt);
        applyInertia(dt);
        const double ratio = tickRate / sampleRate;
        const int ticksThisBlock = std::max(1, static_cast<int>(js::round(n * ratio)));
        updateShape(ticksThisBlock);
        shadowTract.setShape(areas, nasalCount, nasal, tract.velumJ, tract.velumTarget, tract.lipPole, tract.nosePole,
                             ticksThisBlock);
        highTract.setShape(areas, nasalCount, nasal, tract.velumJ, tract.velumTarget, tract.lipPole, tract.nosePole,
                           ticksThisBlock);
        updateGeometry();
        double minOral = 1e9;
        int sealIndex = sections - 1;
        for (int i = static_cast<int>(js::round(sections * 0.6)); i < sections; i = i + 1) {
            if (areas[i] < minOral) {
                minOral = areas[i];
                sealIndex = i;
            }
        }
        const double seal = std::max(0.0, std::min(1.0, (0.06 - minOral) / 0.05)) * (1.0 - art.velum);
        const double occlusion = 0.0;  // with the walls layer the trapped pressure does the stopping
        const double amp = envelope * (0.35 + 0.65 * velocity) * (0.4 + 0.9 * effort) * (1.0 - 0.9 * occlusion);
        sub.configure(subLength, length / sections, anatomy.areaScale);
        if (sealPrev > 0.5 && seal < 0.3 && trapped > 0.05) {
            burst = trapped;
            burstIndex = std::min(sections - 1, sealIndex + 1);
            burstBand.tune(sealIndex >= sections - 2 ? 1400.0 : 3800.0, 0.9, tickRate);
        }
        sealPrev = seal;
        const double sealDecay = js::exp(-1.0 / (0.004 * tickRate));
        const double burstDecay = js::exp(-1.0 / (0.012 * tickRate));
        const double wallCoef = 1.0 - js::exp(-2.0 * js_pi * 350.0 / tickRate);
        const int mouthIndex = static_cast<int>(js::round(sections * 0.75));
        const double wallGain = 0.075 * std::max(areas[static_cast<int>(js::round(sections * 0.7))], 0.5);
        const double loss = section_loss * (1.0 - 0.006 * std::max(occlusion, seal));
        tract.loss = loss;
        shadowTract.loss = loss;
        highTract.loss = loss;
        Tract& mid = tract;
        Tract& shadow = shadowTract;
        Tract& high = highTract;
        const Source& src = source;
        const double f = f0;
        const double rate = tickRate;
        const double gain = calibration * level;
        const double panL = js::cos(0.25 * js_pi * (pan + 1.0));
        const double panR = js::sin(0.25 * js_pi * (pan + 1.0));
        const double impact = amountImpact;
        const double crossOn = amountCrossmodes;
        const double lungs = amp * amp * ps;
        const double inertia = 1.0 - js::exp(-1.0 / (inertiaTau * rate));
        const double flowSmooth = 1.0 - js::exp(-1.0 / (0.01 * rate));
        const int ci = constrictIndex;
        const double aspCoef = 1.0 - js::exp(-2.0 * js_pi * 2500.0 / rate);
        for (int i = 0; i < n; i = i + 1) {
            tickAcc = tickAcc + ratio;
            while (tickAcc >= 1.0) {
                tickAcc = tickAcc - 1.0;
                glottis.tick(f, rate, src, effort, 1.0);
                const double opening = glottis.opening;
                const double p0 =
                    kappa * (mid.glottalPressure() + shadow.glottalPressure() + high.glottalPressure());
                const double pSub = kappa * sub.pressureTop();
                const double drive = std::max(0.05 * lungs, lungs * (1.0 - trapped) + pSub - p0);
                const double target = opening * std::sqrt(drive);
                flowU = flowU + (target - flowU) * inertia;
                const double u = flowU;
                p0Ratio = p0Ratio + (std::fabs(p0) / std::max(lungs, 1e-6) - p0Ratio) * 0.001;
                sub.tick(-u, glottis.reflection);
                if (seal > 0.0) {
                    trapped = std::min(1.0, trapped + (std::fabs(u) / std::max(amp, 0.05)) * seal * 12.0 / rate -
                                                trapped * 4.0 / rate);
                } else {
                    trapped = trapped * sealDecay;
                }
                flowAverage = flowAverage + (std::fabs(u) / std::max(amp, 0.05) - flowAverage) * flowSmooth;
                openingAverage = openingAverage + (opening - openingAverage) * flowSmooth;
                const double white = randomUniform() * 2.0 - 1.0;
                aspLow = aspLow + (white - aspLow) * aspCoef;
                const double aspiration =
                    src.breath * aspirationLevel * aspLow * (0.2 + std::fabs(u) / std::max(amp, 0.05)) * amp;
                if (noiseGain > 0.0 && ci > 0) {
                    shadow.injIndex = ci;
                    shadow.injValue = band.tick(randomUniform() * 2.0 - 1.0) * noiseGain * amp * std::fabs(u) *
                                      flowThrough * 4.0;
                } else {
                    shadow.injIndex = -1;
                    shadow.injValue = 0.0;
                }
                double click = 0.0;
                if (impact > 0.0) {
                    if (prevOpening > 0.02 && opening <= 0.02 + src.leak) {
                        const double speed = (prevOpening - opening) * rate / std::max(f, 40.0);
                        clickPending = impact * amp * speed * speed * 0.02;
                    }
                    if (clickPending != 0.0) {
                        click = clickPending;
                        clickPending = clickPending > 0.0 ? -clickPending * 0.6 : 0.0;
                    }
                }
                prevOpening = opening;
                if (burst > 1e-4) {
                    if (shadow.injIndex != burstIndex) {
                        shadow.injValue = 0.0;
                    }
                    shadow.injIndex = burstIndex;
                    shadow.injValue = shadow.injValue + burstBand.tick(randomUniform() * 2.0 - 1.0) * burst * amp * 0.4;
                    burst = burst * burstDecay;
                }
                const double r = glottis.reflection;
                double ym = mid.tick(u, r);
                if (seal > 0.0) {
                    wallLow = wallLow + (mid.pressureAt(mouthIndex) - wallLow) * wallCoef;
                    ym = ym + wallLow * seal * wallGain;
                }
                const double ys = shadow.tick(aspiration, r);
                double yh = high.tick(click, r);
                if (crossOn > 0.0) {
                    double c = 0.0;
                    for (int k = 0; k < 3; k = k + 1) {
                        const int idx = crossIndex[k];
                        c = c + cross[k].tick(mid.pressureAt(idx) + shadow.pressureAt(idx));
                    }
                    yh = yh + c * crossOn * 0.35 * mid.lipArea;
                }
                busTick(bus[0], ym);
                busTick(bus[1], ys);
                busTick(bus[2], yh);
            }
            const double m0 = busSample(bus[0], tickAcc) * gain;
            const double s0 = busSample(bus[1], tickAcc) * gain;
            const double h0 = busSample(bus[2], tickAcc) * gain;
            const double s = m0 * 1.0 + s0 * 1.0 + h0 * 1.0;
            outL[i] = static_cast<float>(static_cast<double>(outL[i]) + s * panL);
            outR[i] = static_cast<float>(static_cast<double>(outR[i]) + s * panR);
        }
    }
};

// ------------------------------------------------------------------ gnome_voice.mjs
constexpr double sr = 48000;

// STYLE: laughVowel 0.15, tuning "none", chuckle null, epilarynx 0.45, speak null,
// nyahVowel 0.6 (noise, aspiration, crossmodes and glottalBreath unset)
constexpr double style_laugh_vowel = 0.15;
constexpr double style_epilarynx = 0.45;
constexpr double style_nyah_vowel = 0.6;

Register with(Register r, double oq, double qa, double breath, double leak, double jitter, double shimmer) {
    r.oq = oq;
    r.qa = qa;
    r.breath = breath;
    r.leak = leak;
    r.jitter = jitter;
    r.shimmer = shimmer;
    return r;
}

const Register voice_register = [] {
    Register r = reg_head;
    r.oq = 0.52;
    r.leak = 0.03;
    r.breath = 0.06;
    r.qa = 0.07;
    return r;
}();
const Register speak_register = [] {
    Register r = reg_breathy;
    r.qa = 0.15;
    r.breath = reg_breathy.breath + 0.04;
    return r;
}();
const Register chuckle_register = with(reg_falsetto, 0.8, 0.3, 0.12, 0.15, 0.01, 0.05);

void set_source(HdrVoice& v, const Register& src) {
    v.goal = src;
    Source& t = v.sourceTarget;
    t.oq = src.oq;
    t.skew = src.skew;
    t.qa = src.qa;
    t.leak = src.leak;
    t.breath = src.breath;
    t.jitter = src.jitter;
    t.shimmer = src.shimmer;
    t.vent = src.vent;
    v.amountImpact = src.leak > 0.5 ? 0.0 : 0.3;
}

void follow_source(HdrVoice& v, double dt) {
    const double k = 1 - js::exp(-dt / 0.009);
    Source& s = v.source;
    const Register& g = v.goal;
    s.oq += (g.oq - s.oq) * k;
    s.skew += (g.skew - s.skew) * k;
    s.qa += (g.qa - s.qa) * k;
    s.leak += (g.leak - s.leak) * k;
    s.breath += (g.breath - s.breath) * k;
    s.vent += (g.vent - s.vent) * k;
    s.jitter = g.jitter;
    s.shimmer = g.shimmer;
}

HdrVoice gnome(int seed) {
    HdrVoice v(sr, tenor(0.72), reg_head, seed);
    v.goal = voice_register;
    v.vibratoDepth = 0.12;
    v.vibratoRate = 7.0;
    v.driftCents = 6.0;
    v.attack = 0.012;
    v.release = 0.011;
    v.glideRate = 90.0;
    v.vowelGlide = 0.025;
    v.effort = 0.8;
    v.epilarynx = style_epilarynx;
    v.amountNoise = 0.75;
    v.aspirationLevel = 0.2;
    return v;
}

struct Gesture {
    double time;
    std::function<void(HdrVoice&)> act;
};
using Gestures = std::vector<Gesture>;

voice::Signal perform(HdrVoice v, double seconds, Gestures gestures) {
    std::stable_sort(gestures.begin(), gestures.end(), [](const Gesture& a, const Gesture& b) { return a.time < b.time; });
    const auto n = static_cast<std::size_t>(js::round(seconds * sr));
    voice::Signal out(n, 0.0);
    std::array<float, control_block> l{};
    std::array<float, control_block> r{};
    std::size_t next = 0;
    for (std::size_t off = 0; off < n; off += control_block) {
        const double now = static_cast<double>(off) / sr;
        while (next < gestures.size() && gestures[next].time <= now) {
            gestures[next].act(v);
            next += 1;
        }
        follow_source(v, control_block / sr);
        l.fill(0.0f);
        r.fill(0.0f);
        v.render(l, r, control_block);
        for (std::size_t i = 0; i < control_block && off + i < n; i++) {
            out[off + i] = 0.5 * (static_cast<double>(l[i]) + static_cast<double>(r[i]));
        }
    }
    return out;
}

double hz(double note) {
    return 69 + 12 * js::log2(note / 440);
}

Register breathy(double amount) {
    const Register& base = chuckle_register;
    Register r = base;
    r.breath = (base.breath + 0.35 * amount) * 1;
    r.leak = base.leak + 0.4 * amount;
    r.oq = std::min(0.95, base.oq + 0.2 * amount);
    return r;
}

void chuckle(Gestures& g, double t, double from, double to, double length, double vowel, double level, double air = 0) {
    const double a = hz(from);
    const double b = hz(to);
    g.push_back({t, [=](HdrVoice& v) {
                     set_source(v, breathy(std::min(1.0, air + 0.6)));
                     v.vowel = v.vowelTarget = vowel;
                     v.glideRate = std::fabs(b - a) / (length * 0.8) + 1;
                     v.release = 0.026;
                     v.noteOn(a, level * 0.12);
                     v.note = a;
                 }});
    g.push_back({t + 0.012, [=](HdrVoice& v) { v.noteTarget = b; }});
    const int steps = std::max(2, static_cast<int>(js::round(length * 0.85 / 0.01)));
    for (int k = 1; k <= steps; k++) {
        const double u = static_cast<double>(k) / steps;
        g.push_back({t + u * length * 0.85, [=](HdrVoice& v) {
                         v.velocity = level * (0.12 + 0.88 * js::pow(u, 1.6));
                         set_source(v, breathy(air + 0.6 * (1 - std::min(1.0, u * 1.6))));
                     }});
    }
    g.push_back({t + length, [](HdrVoice& v) { v.noteOff(); }});
}

voice::Signal giggle(int seed) {
    Gestures g;
    const double vowel = style_laugh_vowel;
    chuckle(g, 0.03, 470, 615, 0.22, vowel, 0.85);
    chuckle(g, 0.37, 700, 570, 0.18, vowel, 0.9);
    chuckle(g, 0.69, 720, 545, 0.18, vowel, 0.85);
    chuckle(g, 1.01, 700, 520, 0.2, vowel, 0.8, 0.15);
    chuckle(g, 1.43, 565, 505, 0.11, vowel, 0.5, 0.35);
    chuckle(g, 1.72, 600, 520, 0.08, vowel, 0.3, 1.0);
    chuckle(g, 1.91, 540, 520, 0.2, vowel, 0.75, 0.2);
    chuckle(g, 2.21, 550, 445, 0.11, vowel, 0.45, 0.4);
    chuckle(g, 2.42, 260, 245, 0.08, 0.5, 0.25, 0.6);
    return perform(gnome(seed), 2.75, std::move(g));
}

voice::Signal hoots(int seed) {
    Gestures g;
    const double vowel = 0.06;
    chuckle(g, 0.03, 520, 660, 0.18, vowel, 0.85);
    chuckle(g, 0.31, 720, 560, 0.16, vowel, 0.9);
    chuckle(g, 0.57, 740, 540, 0.16, vowel, 0.85, 0.1);
    chuckle(g, 0.85, 700, 500, 0.22, vowel, 0.8, 0.2);
    chuckle(g, 1.2, 560, 480, 0.1, vowel, 0.35, 0.8);
    return perform(gnome(seed), 1.5, std::move(g));
}

voice::Signal nyah(int seed) {
    Gestures g;
    constexpr std::array<std::array<double, 4>, 5> tune{
        {{0.0, 0.22, 350, 340}, {0.27, 0.22, 300, 285}, {0.53, 0.15, 395, 385}, {0.72, 0.22, 352, 335}, {0.99, 0.5, 300, 255}}};
    for (const auto& [start, length, from, to] : tune) {
        const double t = start + 0.03;
        g.push_back({t - 0.03, [](HdrVoice& v) {
                         v.stopTipTarget = 1.0;
                         v.velumOpenTarget = 1.0;
                         v.vowel = v.vowelTarget = 1.0;
                     }});
        g.push_back({t, [from = from](HdrVoice& v) {
                         set_source(v, speak_register);
                         v.glideRate = 20;
                         v.noteOn(hz(from) - 0.8, 0.95);
                         v.noteTarget = hz(from);
                     }});
        g.push_back({t + 0.045, [](HdrVoice& v) {
                         v.stopTipTarget = 0.0;
                         v.vowelGlide = 0.05;
                         v.vowelTarget = style_nyah_vowel;
                         v.velumOpenTarget = 0.3;
                     }});
        g.push_back({t + length * 0.45, [to = to](HdrVoice& v) {
                         v.glideRate = 12;
                         v.noteTarget = hz(to);
                     }});
        g.push_back({t + length, [](HdrVoice& v) {
                         v.noteOff();
                         v.vowelGlide = 0.025;
                     }});
    }
    chuckle(g, 1.6, 690, 560, 0.16, 0.72, 0.8);
    chuckle(g, 1.9, 700, 530, 0.17, 0.72, 0.7, 0.2);
    return perform(gnome(seed), 2.25, std::move(g));
}

voice::Signal hoo(int seed, bool twice) {
    Gestures g;
    if (twice) {
        chuckle(g, 0.03, 480, 640, 0.2, 0.08, 0.9);
        chuckle(g, 0.33, 720, 540, 0.22, 0.08, 0.9, 0.1);
        return perform(gnome(seed), 0.75, std::move(g));
    }
    g.push_back({0.03, [](HdrVoice& v) {
                     set_source(v, chuckle_register);
                     v.vowel = v.vowelTarget = 0.08;
                     v.glideRate = 30;
                     v.noteOn(hz(460), 0.9);
                     v.note = hz(460);
                     v.noteTarget = hz(690);
                 }});
    g.push_back({0.17, [](HdrVoice& v) {
                     v.glideRate = 22;
                     v.noteTarget = hz(540);
                     v.vowelTarget = 0.2;
                 }});
    g.push_back({0.36, [](HdrVoice& v) { v.noteOff(); }});
    return perform(gnome(seed), 0.6, std::move(g));
}

// writeClip's levelling before it rounds to 16 bits: the voiced part at -17 dBFS RMS,
// the last 480 samples faded, a soft limit over half scale.
voice::Signal clip_master(const voice::Signal& mono) {
    double sum = 0;
    double count = 0;
    for (const double x : mono) {
        if (std::fabs(x) > 1e-4) {
            sum += x * x;
            count += 1;
        }
    }
    const double gain = 0.141 / std::sqrt(sum / std::max(1.0, count));
    const std::size_t n = mono.size();
    voice::Signal out(n);
    for (std::size_t i = 0; i < n; i++) {
        const double fade = std::min(1.0, static_cast<double>(n - 1 - i) / 480);
        const double y = mono[i] * gain * fade;
        const double a = std::fabs(y);
        const double sign = y > 0 ? 1.0 : (y < 0 ? -1.0 : 0.0);
        out[i] = sign * (a < 0.5 ? a : 0.5 + 0.45 * js::tanh((a - 0.5) / 0.45));
    }
    return out;
}

voice::Signal gnome_hoo() {
    return clip_master(hoo(11, false));
}
voice::Signal gnome_hoo2() {
    return clip_master(hoo(12, true));
}
voice::Signal gnome_giggle() {
    return clip_master(giggle(13));
}
voice::Signal gnome_giggle2() {
    return clip_master(hoots(14));
}
voice::Signal gnome_nyah() {
    return clip_master(nyah(15));
}

constexpr voice::Cue cues[] = {
    {.name = "mm_gnome_hoo", .mono = gnome_hoo},
    {.name = "mm_gnome_hoo2", .mono = gnome_hoo2},
    {.name = "mm_gnome_giggle", .mono = gnome_giggle},
    {.name = "mm_gnome_giggle2", .mono = gnome_giggle2},
    {.name = "mm_gnome_nyah", .mono = gnome_nyah},
};

}  // namespace

std::span<const voice::Cue> gnome_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

}  // namespace mm
