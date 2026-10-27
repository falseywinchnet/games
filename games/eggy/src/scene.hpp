#pragma once
// Builds each 3D frame of the mountain: sky, terrain, props, Eggy, weather and
// particles, rendered by R3D at low resolution.
#include "pose.hpp"
#include "render.hpp"
#include "sim.hpp"

#include <string>
#include <vector>

namespace eggy {

struct Particle {
    enum Kind : std::uint8_t { dust, splash, ripple, sparkle, feather, note, sweat, breath, snowpuff, confetti, heart, starburst } kind;
    V3 p, vel;
    double age = 0, life = 1, size = .1, spin = 0;
    Col col{1, 1, 1, 1};
};

class Scene {
public:
    R3D r;
    double zoom = 1;
    bool reduced_motion = false;
    Pose pose;              // current animated pose (smoothed)
    double cam_u = 0, cam_v = 0, cam_z = 0;
    bool cam_ready = false;
    double cheer = 0;       // seconds of celebration left (stars etc.)
    double squash = 0;      // landing squash timer
    double helped_flash = 0;
    double salute = 0;      // seconds of salute left (base camp)
    std::vector<Particle> parts;
    // summit officer
    double officer_u = 0, officer_v = 0;
    // sounds the scene wants played (rare life, toppling trees); drained by the view
    std::vector<std::pair<std::string, float>> sounds;
    double near_waterfall = 0;   // 0..1, for the ambience
    double near_fire = 0;        // 0..1, base camp fire

    void resize(int w, int h);
    void render(const Sim& s, double t, double dt);
    void on_event(const Event& e, const Sim& s);
    bool screen_to_ground(const Sim& s, double sx, double sy, double& u, double& v) const;
    void duck_screen(const Sim& s, double& x, double& y) const;  // head position on the low-res buffer
    double night() const { return night_; }

private:
    double night_ = 0, time_ = 0, rt_ = 0, phase_ = 0, sun_ = 1, sky_t_ = 0, sky_v_ = 0;
    double blink_t_ = 2, blink_left_ = 0;
    std::vector<float> sky_cache_;
    std::vector<std::uint8_t> looks_;
    std::vector<double> heights_;
    double sky_key_[6] = {};
    void animate(const Sim& s, double dt);
    void sky(const Sim& s);
    void terrain(const Sim& s);
    void props(const Sim& s);
    void weather(const Sim& s, double dt);
    void particles(double dt);
    void ceremony(const Sim& s);
    void life(const Sim& s, double dt);
    void storm_sky(const Sim& s);
    struct WaterSpot { double x, y, z; };
    std::vector<WaterSpot> water_spots_;
    double flash_t_ = 9, flash_dist_ = 1, next_fall_ = 120, fall_start_ = -1, fall_dir_ = 0, next_ribbit_ = 4, smoke_t_ = 0;
    int fall_u_ = 0;
    std::int64_t fall_v_ = -1;
    unsigned bolt_seed_ = 1;
    int fish_cycle_ = -1;
    bool fish_in_air_ = false;
    void spawn(Particle::Kind k, V3 p, int n, double spread, Col c);
};

}  // namespace eggy
