#pragma once
// The sky over the falls, as light arrives through a real atmosphere: sunlight
// scattered once by air molecules (Rayleigh: the blue, deepening away from the sun),
// by the humid haze of a rainforest afternoon (Mie: the white glory round the sun,
// strongly forward), and absorbed by ozone (the violet-blue cast of the high sky),
// integrated along each view ray through a spherical atmosphere with its density
// thinning upward. The sun's disc is limb-darkened and reddened by the air it shines
// through; its glare in the eye is a separate, measured spread (glare()).
//
// Directions are in the scene's frame: x right, y up, z toward the falls. Radiance
// is relative to the sun's irradiance at the top of the air (1): a sky of about 0.03
// to 0.3, a disc of about 10^4.
#include "falls_math.hpp"

namespace cf {

struct SkyParameters {
    double sun_elevation = 36 * pi / 180;  // above the horizon
    double sun_azimuth = 6 * pi / 180;     // to the right of straight ahead
    double haze = 3.5;           // the Mie scatterers, as a multiple of a clear day's
    double haze_height = 1400;   // metres: the haze's scale height (humid air sits low)
    double haze_forward = .78;   // Mie asymmetry: how tightly the glory hugs the sun
    double ozone = 1;            // a multiple of the usual column
    double altitude = 250;       // the observer, metres above the sea (the falls' river)
    double sun_tint = 1;         // 1 for sunlight as it is; below 1 whiter, above warmer
};

class Sky {
  public:
    explicit Sky(const SkyParameters& parameters);
    [[nodiscard]] Vec3 sun_direction() const {
        return sun_;
    }
    // The sky's radiance along `direction` (unit), sun disc excluded.
    [[nodiscard]] Rgb radiance(Vec3 direction) const;
    // Sunlight arriving at the observer (the top-of-air irradiance through the air
    // on the way down): what lights the falls.
    [[nodiscard]] Rgb sunlight() const {
        return sunlight_;
    }
    // The disc's radiance at `angle` radians from its centre: 0 outside it.
    [[nodiscard]] Rgb disc(double angle) const;
    [[nodiscard]] static double disc_radius() {
        return .2666 * pi / 180;
    }
    // The sun's glare in the eye at `angle` radians from it: added light, as a
    // fraction of the disc's irradiance per steradian (a narrow core, the aureole of
    // the eye's scattering, and a long faint skirt).
    [[nodiscard]] Rgb glare(double angle) const;

  private:
    SkyParameters parameters_{};
    Vec3 sun_{};
    Rgb sunlight_{};
    [[nodiscard]] Rgb transmittance_to_sun(Vec3 point) const;
};

} // namespace cf
