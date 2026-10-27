#pragma once
// Eggy's simulation: gravity, hops, ice, wind, leaves, breath, drinking,
// stars, idle behaviour, the patient autopilot, and the summit ceremony.
// Pure logic: no drawing, audio, files or wall clock. The view feeds input
// and real elapsed seconds; the simulation emits events for speech and sound.
#include "world.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace eggy {

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed = 1) : s(mix64(seed) | 1) {}
    std::uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    double uni() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
    double range(double a, double b) { return a + (b - a) * uni(); }
    int below(int n) { return static_cast<int>(uni() * n); }
};

enum class Act : std::uint8_t {
    none, rest, drink, preen, flap, look, knocked, celebrate, shiver, fan, chirp, ceremony
};

enum class Ev : std::uint8_t {
    hop, land, step, splash, swim_stroke, drink, refreshing, breathless, rested, knocked, got_up, star,
    gust, slide, preen, flap, look, chirp, milestone, biome, helped, auto_on, shiver, fan, ledge_found,
    summit_seen, breath_low, lightning, thunder, storm_begin, storm_end, tent_open, general_out, salute, return_salute, medal, title, ceremony_done, idle_line
};

struct Event {
    Ev type;
    int a = 0;       // surface, biome, star index, milestone …
    double x = 0;    // extra payload
};

struct Leaf {
    double u, v, z, vu, vv, vz, phase, spin;
    int color;
    double landed = -1;  // seconds since landing, -1 while falling
    bool aimed = false;
};

struct Footprint {
    double u, v, z, angle;
    int side;
    double age;
};

struct Input {
    double iu = 0, iv = 0;      // keyboard direction in world axes
    bool hop = false;
    bool has_target = false;    // mouse: walk toward this ground point
    double tu = 0, tv = 0;
};

struct Duck {
    double u = 5, v = 2, z = 0, vz = 0, vu = 0, vv = 0;
    double heading = 1.5707963;  // world angle: atan2(dv, du); pi/2 = uphill
    bool air = false, swim = false, sliding = false;
    Act act = Act::none;
    double act_t = 0, act_len = 0;
    double breath = 1, refreshed = 0;
    double walk_phase = 0, speed = 0;
    double stand_t = 0, next_idle = 3, next_break = 50;
    double last_drink = 0;
    double brace = 0;          // 0..1 leaning into wind
    double heat = 0, cold = 0; // felt now, 0..1
    int knock_dir = 1;
};

class Sim {
public:
    explicit Sim(std::uint64_t seed);
    World world;
    Duck d;
    Input in;
    std::vector<Event> events;
    std::vector<Leaf> leaves;
    std::deque<Footprint> prints;
    std::vector<std::uint8_t> collected;  // per star
    int stars_collected = 0;
    double elapsed = 0;        // seconds of climbing (includes time away)
    double best_v = 0;
    bool player_mode = false;  // the human is helping right now
    double since_input = 1e9;
    double gust_t = 0, gust_len = 0, gust_strength = 0, gust_du = 0, gust_dv = -1, next_gust = 12;
    double wind_vis = 0;       // smoothed gust strength for visuals
    double day_offset = .28;   // start in the morning
    bool hold = false;         // base-camp speech: Eggy stays put (idle life still runs)
    // storms: rare, slow to build, minutes long; lightning with thunder delayed by distance
    double storm = 0, storm_target = 0, storm_left = 0, next_storm = 2400, next_flash = 5;
    std::vector<std::pair<double, double>> thunder_due;  // (time, distance)
    bool finished = false;
    double ceremony_t = -1;    // >=0 while the summit ceremony runs
    int last_milestone = 0;
    Biome last_biome = Biome::meadow;

    void step(double dt);                 // advance real seconds (internally sub-stepped)
    double day_phase() const;             // 0..1, 0.25 sunrise .. 0.75 sunset
    double sun() const;                   // 0..1 daylight
    double progress() const { return d.v / static_cast<double>(world.length()); }
    void place_at(double v);              // teleport to a safe lane spot (time away, dev warp)
    void advance_offline(double seconds, double& rows_gained);
    static constexpr double kAutoAfter = 7.0;      // seconds without input before autopilot
    static constexpr double kPlayerSpeed = 1.9;
    static constexpr double kAutoSpeed = 1.12;
    static constexpr double kOfflineRowsPerSecond = 0.80;
    std::vector<std::pair<double, double>> path;   // autopilot waypoints (for debug drawing)
    double t_since_swim() const { return elapsed - last_swim_; }

private:
    Rng rng_;
    double replan_t_ = 0, stuck_t_ = 0, last_v_check_ = 0;
    bool keys_target_ = false;
    double last_swim_ = -1e9;
    bool breath_warned_ = false;
    int print_side_ = 0;
    double step_dist_ = 0;
    void tick(double dt);
    void begin(Act a, double len);
    void plan(bool to_target);
    void move(double dt, double du, double dv, double speed);
    bool try_hop();
    double surface_speed(const Tile& t) const;
    void hazards(double dt);
    void ceremony(double dt);
    void emit(Ev e, int a = 0, double x = 0) { events.push_back({e, a, x}); }
    bool passable(int u, std::int64_t v) const;
};

}  // namespace eggy
