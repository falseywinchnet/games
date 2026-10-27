#pragma once
// One mowing of one garden, as a pure simulation: the autonomous mower (coverage
// engine), someone grabbing the controls, the gnome who pops up from the tall
// grass, things the deck runs over, the engine's governor, and the sounds all this
// asks for. Time and input are passed in; given the same seed and the same inputs
// it plays out the same way everywhere.
#include "coverage.hpp"
#include "garden.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mm {

enum class Livery : std::uint8_t { orange_h, red_t, green_jd };
constexpr int livery_count = 3;
const char* livery_name(Livery livery);  // "Orange H", ...
const char* livery_key(Livery livery);   // "h", "t", "jd": sound and settings suffix

// The engine each livery carries (see audio_src/README.md for the acoustics).
struct EngineSpec {
    double rated_rpm{};
    double droop{};     // fractional speed lost at full cutting load before the governor answers
    double response{};  // governor time constant, seconds
};
EngineSpec engine_spec(Livery livery);

enum class GnomeState : std::uint8_t { hidden, rising, looking, sinking, frozen, shattered };

struct Gnome {
    GnomeState state{GnomeState::hidden};
    double x{};
    double y{};
    double clock{};    // seconds in this state
    double length{};   // planned seconds of looking (dancing)
    double height{};   // 0 hidden .. 1 standing (may overshoot on the jump)
    double look{};     // head turn, radians
    double facing{};   // body facing, radians
    double next{};     // scene seconds until the next appearance (while hidden)
    bool sought{};     // the mower has been sent for him
    int dance{};       // which move he is dancing: 0 bounce, 1 jig, 2 spin, 3 nyah, 4 wiggle, 5 chicken
    double beat{};     // seconds into the move
    double home_x{};   // where he came up; he dances about it
    double home_y{};
    int pops{};        // times he will pop up again before he goes off for a while
    bool encore{};     // this appearance is one of those
};

// The old lady whose flowers they were. She comes out when a bed is driven over and
// goes for the mower with her rolling pin. She walks a straight line to where she
// last saw it, stops, turns, and sets off again; she will not step on a bed, and a
// mower with a hand on its wheel is a little quicker than she is.
constexpr double granny_patience = 30.0;  // seconds the mower must dodge her

enum class GrannyState : std::uint8_t { away, walking, turning, startled, fleeing };

struct Granny {
    GrannyState state{GrannyState::away};
    double x{};
    double y{};
    double facing{};
    double target_x{};  // the end of the line she is walking
    double target_y{};
    double clock{};     // seconds in this state
    double chase{};     // seconds she has been after the mower this time
    double stride{};    // metres walked, for her feet and the pin
    double rest{};      // seconds before she can be brought out again
    double next_line{}; // seconds until she has something more to say
    double replan{};    // seconds until she looks for the mower again
    double exit_x{};    // where she is running off the lawn
    double exit_y{};
};

// Bees know where the flowers are; birds know the trees and the fountain. Both keep
// clear of the mower.
enum class BeeState : std::uint8_t { arriving, visiting, leaving };

struct Bee {
    BeeState state{BeeState::arriving};
    double x{};
    double y{};
    double z{};        // height, metres
    double vx{};
    double vy{};
    double target_x{}; // the flower it is making for, or the way out
    double target_y{};
    double clock{};
    double stay{};     // seconds it means to spend on this flower
    int visits{};      // flowers still to visit before it goes
    double wobble{};
    std::uint32_t id{};  // which bee this is, for its own voice
};

enum class BirdState : std::uint8_t { arriving, perched, leaving };

struct Bird {
    BirdState state{BirdState::arriving};
    int kind{};        // 0 robin, 1 blue tit, 2 blackbird, 3 sparrow
    double x{};
    double y{};
    double z{};
    double heading{};
    double target_x{};
    double target_y{};
    double target_z{};
    double clock{};
    double stay{};
    double flap{};     // wing phase while flying
    bool singing{};
    int hops{};        // perches still to visit before it goes
};

enum class ParticleKind : std::uint8_t { clipping, seed, petal, mushroom, shard, hat, mulch, leaf, flame, smoke };

struct Particle {
    ParticleKind kind{};
    double x{};
    double y{};
    double z{};  // height above the lawn, metres
    double vx{};
    double vy{};
    double vz{};
    double age{};
    double life{};
    double size{};
    double spin{};
    std::uint32_t tint{};  // 0xRRGGBB
};

struct Cue {
    std::string name{};
    double gain{1};
    double rate{1};
    double pan{};  // -1 left .. 1 right
};

// What the sound beds should do now.
struct EngineVoice {
    double rpm{};        // engine speed
    double rate{1};      // rpm / rated rpm
    double throttle{};   // 0 light .. 1 full
    double load{};       // 0 no grass .. 1 a full deck of tall grass
};

// The world rectangle (metres) whose look changed since it was last taken.
struct Dirty {
    double x0{1e9};
    double y0{1e9};
    double x1{-1e9};
    double y1{-1e9};
    [[nodiscard]] bool empty() const {
        return x1 < x0;
    }
    void include(double x, double y, double radius);
};

class Mowing final {
  public:
    Mowing(std::uint64_t seed, Livery livery);

    void advance(double seconds, bool reduced_motion);

    // Input in lawn metres. grab() is true when the point is on the mower, which
    // then follows steer() until let_go(); poke() freezes a visible gnome.
    bool grab(double x, double y);
    void steer(double x, double y);
    void let_go();
    bool poke(double x, double y);
    [[nodiscard]] bool held() const {
        return held_;
    }
    [[nodiscard]] bool near_mower(double x, double y) const;
    void set_livery(Livery livery);
    // The key. Off: the mower stands where it is and nothing is cut. On: it takes a
    // moment to start (the starter, up to speed, blades in) before it moves again.
    void set_engine(bool on);
    [[nodiscard]] bool engine_on() const {
        return engine_on_;
    }
    // Seconds since the key was last turned (large before it ever is).
    [[nodiscard]] double engine_seconds() const {
        return engine_clock_;
    }

    [[nodiscard]] const Garden& garden() const {
        return garden_;
    }
    [[nodiscard]] const coverage::Mower& mower() const {
        return mower_;
    }
    [[nodiscard]] const Gnome& gnome() const {
        return gnome_;
    }
    [[nodiscard]] const std::vector<Bee>& bees() const {
        return bees_;
    }
    [[nodiscard]] const std::vector<Bird>& birds() const {
        return birds_;
    }
    [[nodiscard]] const Granny& granny() const {
        return granny_;
    }
    // The gnome who sees her off once she has been dodged for long enough.
    [[nodiscard]] const Gnome& guard() const {
        return guard_;
    }
    // She reached the mower: the garden is to be started again.
    [[nodiscard]] bool caught() const {
        return caught_;
    }
    // Mulch the deck has been over, per fine cell: the flowers there are gone.
    [[nodiscard]] const std::vector<std::uint8_t>& trampled() const {
        return trampled_;
    }
    // 0 still .. 1 just struck, for each of the garden's trees.
    [[nodiscard]] double tree_shake(std::size_t index) const {
        return index < tree_shake_.size() ? tree_shake_[index] : 0.0;
    }
    // 0 .. 1: how hard the deck is chewing through a flower bed right now.
    [[nodiscard]] double wreck() const {
        return wreck_;
    }
    [[nodiscard]] const std::vector<Particle>& particles() const {
        return particles_;
    }
    [[nodiscard]] const EngineVoice& voice() const {
        return voice_;
    }
    [[nodiscard]] Livery livery() const {
        return livery_;
    }
    // The way the grass lies per fine cell: 1..254 for 0..2 pi, 0 not cut, 255 cut
    // without a direction. A second pass lays it again, weighted with how it lay first.
    [[nodiscard]] const std::vector<std::uint8_t>& stripes() const {
        return stripes_;
    }
    // Striping the open lawn again once it is all cut (the pro's finish).
    [[nodiscard]] bool finishing() const {
        return finishing_;
    }
    // Seconds left of the hurry after the gnome is broken: the mower runs at twice its
    // pace and the band plays fast.
    [[nodiscard]] double hurry() const {
        return hurry_;
    }
    // Seconds a knocked-over grill still burns, per prop (0 for none).
    [[nodiscard]] double fire(std::size_t prop) const {
        return prop < fire_.size() ? fire_[prop] : 0.0;
    }
    [[nodiscard]] double time() const {
        return time_;
    }
    // Finished, and the mower has stood admiring its work for a few seconds.
    [[nodiscard]] bool complete() const;
    // Sound events since the last call.
    std::vector<Cue> take_cues();
    Dirty take_dirty();

  private:
    void step(double seconds);
    void record_cut(const coverage::Pose& from, const coverage::Pose& to, std::uint8_t lay);
    void plan_finish();
    coverage::Step finish_step(double seconds);
    void knock_grills();
    void burn(double seconds);
    void run_over_things();
    void wreck_beds(double seconds, bool driven);
    void strike_trees();
    void update_granny(double seconds);
    void update_life(double seconds);
    bool flower_for_bee(double& x, double& y);
    bool perch_for_bird(double& x, double& y, double& z);
    void summon_granny();
    void aim_granny();
    void gnome_say(const char* name, double gain, double rate, double x);
    void aim_granny_at(double gx, double gy);
    [[nodiscard]] bool clear_around(double x, double y, double radius) const;
    [[nodiscard]] bool granny_can_stand(double x, double y) const;
    [[nodiscard]] bool granny_can_walk(double x0, double y0, double x1, double y1) const;
    void update_gnome(double seconds);
    void place_gnome();
    void shatter();
    void update_engine(double seconds, int newly_cut);
    void update_particles(double seconds);
    void spray(double seconds);
    void cue(const std::string& name, double gain, double rate, double x);

    std::uint64_t random_{};
    Garden garden_;
    Livery livery_;
    coverage::Mower mower_;
    Gnome gnome_{};
    std::vector<Particle> particles_{};
    std::vector<Cue> cues_{};
    std::vector<std::uint8_t> stripes_{};
    std::vector<std::uint8_t> first_lay_{};  // how each cell lay after its first cut
    // The finish: lanes along the sun's line over each open stretch, mown back and forth.
    struct Lane {
        double x0{};
        double y0{};
        double x1{};
        double y1{};
        int section{};
    };
    std::vector<Lane> lanes_{};
    std::size_t lane_{};
    int lane_stage_{};      // 0 going there through the garden, 1 turning onto it, 2 mowing it
    double lane_clock_{};   // seconds in this stage, to give up on one that cannot be reached
    bool goal_sent_{};
    bool finish_planned_{};
    bool finishing_{};
    double hurry_{};
    std::vector<double> fire_{};
    std::vector<double> fire_carry_{};
    EngineVoice voice_{};
    Dirty dirty_{};
    double time_{};
    long long pending_us_{};
    double idle_{};          // seconds finished
    double load_smooth_{};
    double spray_carry_{};
    double bump_quiet_{};
    double scream_delay_{-1};
    bool held_{};
    double hold_x_{};
    double hold_y_{};
    bool reduced_{};
    Granny granny_{};
    std::vector<Bee> bees_{};
    std::uint32_t next_bee_{1};
    std::vector<Bird> birds_{};
    double bee_wait_{6};
    double bird_wait_{10};
    Gnome guard_{};
    std::vector<std::uint8_t> trampled_{};
    std::vector<double> tree_shake_{};
    double gnome_voice_{};  // seconds until the gnome's last line is over
    std::vector<std::uint8_t> walkable_{};  // the old lady's map, a quarter-metre grid
    std::vector<std::uint8_t> clearance_{}; // cells from each standing place to the nearest obstacle
    double wreck_{};
    double wreck_cue_{};
    double mulch_carry_{};
    bool caught_{};
    bool rescuing_{};   // let go on a bed: it drives itself back to the grass first
    double rescue_x_{};
    double rescue_y_{};
    bool engine_on_{true};
    double engine_clock_{1000};
};

// Converts between headings and stripe codes, 1..254.
std::uint8_t heading_code(double heading);
double code_heading(std::uint8_t code);
// The way short grass lies when laid towards the grass art's sun (the dark lay).
constexpr double sun_lay = -2.443;

} // namespace mm
