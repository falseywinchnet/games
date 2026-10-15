#include "sky.hpp"

namespace cf {
namespace {
constexpr double earth_radius = 6360e3;
constexpr double air_radius = 6460e3;
constexpr double rayleigh_height = 8000;
const Rgb rayleigh_scattering{5.802e-6, 13.558e-6, 33.1e-6};
constexpr double mie_scattering = 3.996e-6;
constexpr double mie_extinction = 4.44e-6;
const Rgb ozone_absorption{.650e-6, 1.881e-6, .085e-6};
constexpr int view_steps = 40;
constexpr int sun_steps = 16;
// Light scattered more than once, as a share of the once-scattered light taken with
// no preference of direction: it lifts the shadowed blue and the sky's far side.
constexpr double multiple_share = .45;

// Where a ray from `origin` along `direction` leaves a sphere of `radius` about the
// planet's centre, or -1 when it misses it.
double sphere_exit(Vec3 origin, Vec3 direction, double radius) {
    const double b = dot(origin, direction);
    const double c = dot(origin, origin) - radius * radius;
    const double d = b * b - c;
    if (d < 0)
        return -1;
    return -b + std::sqrt(d);
}
// The nearer hit with the ground, or -1.
double ground_hit(Vec3 origin, Vec3 direction) {
    const double b = dot(origin, direction);
    const double c = dot(origin, origin) - earth_radius * earth_radius;
    const double d = b * b - c;
    if (d < 0 || b > 0)
        return -1;
    const double t = -b - std::sqrt(d);
    return t > 0 ? t : -1;
}
double ozone_density(double height) {
    return std::max(0.0, 1 - std::fabs(height - 25e3) / 15e3);
}
double rayleigh_phase(double mu) {
    return 3 / (16 * pi) * (1 + mu * mu);
}
// Cornette and Shanks.
double mie_phase(double mu, double g) {
    const double g2 = g * g;
    const double denominator = (2 + g2) * std::pow(1 + g2 - 2 * g * mu, 1.5);
    return 3 / (8 * pi) * (1 - g2) * (1 + mu * mu) / denominator;
}
// Steps along a ray crowd toward its start, where the air is densest: step i of n
// covers [length (i/n)^2, length ((i+1)/n)^2].
void step_at(int i, int n, double length, double& middle, double& size) {
    const double a = static_cast<double>(i) / n;
    const double b = static_cast<double>(i + 1) / n;
    middle = length * (a * a + b * b) / 2;
    size = length * (b * b - a * a);
}
} // namespace

Sky::Sky(const SkyParameters& parameters) : parameters_(parameters) {
    const double ce = std::cos(parameters.sun_elevation);
    sun_ = normalized({ce * std::sin(parameters.sun_azimuth), std::sin(parameters.sun_elevation),
                       ce * std::cos(parameters.sun_azimuth)});
    sunlight_ = transmittance_to_sun({0, earth_radius + parameters.altitude, 0});
}

Rgb Sky::transmittance_to_sun(Vec3 point) const {
    if (ground_hit(point, sun_) > 0)
        return {};
    const double total = sphere_exit(point, sun_, air_radius);
    if (total <= 0)
        return {1, 1, 1};
    double rayleigh = 0, mie = 0, ozone = 0;
    for (int i = 0; i < sun_steps; ++i) {
        double t = 0, dt = 0;
        step_at(i, sun_steps, total, t, dt);
        const double height = length(point + sun_ * t) - earth_radius;
        rayleigh += std::exp(-height / rayleigh_height) * dt;
        mie += std::exp(-height / parameters_.haze_height) * dt;
        ozone += ozone_density(height) * dt;
    }
    const double tint = parameters_.sun_tint;
    const double o = ozone * parameters_.ozone;
    const double depth_m = mie_extinction * parameters_.haze * mie;
    const double depth_r = tint * (rayleigh_scattering.r * rayleigh + ozone_absorption.r * o) + depth_m;
    const double depth_g = tint * (rayleigh_scattering.g * rayleigh + ozone_absorption.g * o) + depth_m;
    const double depth_b = tint * (rayleigh_scattering.b * rayleigh + ozone_absorption.b * o) + depth_m;
    return {std::exp(-depth_r), std::exp(-depth_g), std::exp(-depth_b)};
}

Rgb Sky::radiance(Vec3 direction) const {
    const Vec3 origin{0, earth_radius + parameters_.altitude, 0};
    double total = sphere_exit(origin, direction, air_radius);
    const double ground = ground_hit(origin, direction);
    if (ground > 0)
        total = ground;
    if (total <= 0)
        return {};
    const double mu = dot(direction, sun_);
    const double phase_r = rayleigh_phase(mu);
    const double phase_m = mie_phase(mu, parameters_.haze_forward);
    const double isotropic = 1 / (4 * pi);
    const double haze = parameters_.haze;
    // Optical depth from the eye to the start of the step, per channel.
    double depth_r = 0, depth_g = 0, depth_b = 0;
    Rgb once{};
    Rgb again{};
    for (int i = 0; i < view_steps; ++i) {
        double t = 0, dt = 0;
        step_at(i, view_steps, total, t, dt);
        const Vec3 point = origin + direction * t;
        const double height = length(point) - earth_radius;
        const double density_r = std::exp(-height / rayleigh_height);
        const double density_m = std::exp(-height / parameters_.haze_height);
        const double density_o = ozone_density(height) * parameters_.ozone;
        const double step_m = mie_extinction * haze * density_m * dt;
        const double step_r = (rayleigh_scattering.r * density_r + ozone_absorption.r * density_o) * dt + step_m;
        const double step_g = (rayleigh_scattering.g * density_r + ozone_absorption.g * density_o) * dt + step_m;
        const double step_b = (rayleigh_scattering.b * density_r + ozone_absorption.b * density_o) * dt + step_m;
        // Light reaching this step from the sun, then reaching the eye from its middle.
        const Rgb sun_in = transmittance_to_sun(point);
        const Rgb to_eye{std::exp(-(depth_r + step_r / 2)), std::exp(-(depth_g + step_g / 2)),
                         std::exp(-(depth_b + step_b / 2))};
        const Rgb lit = sun_in * to_eye;
        const Rgb scatter_r = rayleigh_scattering * (density_r * dt);
        const double scatter_m = mie_scattering * haze * density_m * dt;
        const Rgb directed = scatter_r * phase_r + Rgb{scatter_m, scatter_m, scatter_m} * phase_m;
        const Rgb diffuse = (scatter_r + Rgb{scatter_m, scatter_m, scatter_m}) * isotropic;
        once = once + lit * directed;
        again = again + lit * diffuse;
        depth_r += step_r;
        depth_g += step_g;
        depth_b += step_b;
    }
    return once + again * multiple_share;
}

Rgb Sky::disc(double angle) const {
    const double radius = disc_radius();
    if (angle >= radius)
        return {};
    // Limb darkening (the sun's edge is cooler and dimmer), redder at the limb.
    const double r = angle / radius;
    const double mu = std::sqrt(std::max(0.0, 1 - r * r));
    const double solid = pi * radius * radius;
    const double mean_r = 1 - .45 / 3, mean_g = 1 - .58 / 3, mean_b = 1 - .72 / 3;
    const Rgb limb{(1 - .45 * (1 - mu)) / mean_r, (1 - .58 * (1 - mu)) / mean_g, (1 - .72 * (1 - mu)) / mean_b};
    return sunlight_ * limb * (1 / solid);
}

Rgb Sky::glare(double angle) const {
    const double degree = pi / 180;
    const double core = .35 * degree;
    const double halo = 2.4 * degree;
    const double skirt = 7 * degree;
    const double g_core = std::exp(-angle * angle / (2 * core * core)) / (2 * pi * core * core);
    const double g_halo = std::exp(-angle * angle / (2 * halo * halo)) / (2 * pi * halo * halo);
    const double q = angle / skirt;
    const double g_skirt = 1 / (2 * pi * skirt * skirt) / std::pow(1 + q * q, 1.5);
    const double share = .006 * g_core + .0007 * g_halo + .0004 * g_skirt;
    return sunlight_ * share;
}

} // namespace cf
