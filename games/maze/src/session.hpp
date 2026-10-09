#pragma once
// A level in play: the rules (maze.hpp) put in motion. Steps and quarter turns
// glide; doors sink when their pad is pressed; the elevator rises through the
// ceiling into the maze above; a flip stone rolls the whole world over; a bulb
// puts the lights out; portals blink you away. The marble rolls the corridors
// and gets in your way but never traps you; the snail repaints the walls behind
// it. Now and then someone steps into the corridor ahead with something to say,
// and occasionally something useful to do. Rules always commit before the
// animation catches up.
#include "maze.hpp"
#include "soft3d.hpp"
#include "world.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace mz {

enum class Cmd { forward, back, left, right };

struct Event {                 // for the view: a sound to play, or words to show
    enum Kind { sound, say, won } kind = sound;
    std::string text;          // sound name, or the words
    std::string who;           // the speaker (say)
    float gain = 1, rate = 1;
};

struct Speech {
    std::string who, text;
    double age = 0;
};

struct VisitorDef {            // supplied by the view: who might drop by
    std::string name;
    const Tex32* tex = nullptr;
    std::vector<std::string> lines;
    double w = .7, h = .9;
};

class Session {
public:
    void start(const Level& lv, std::uint64_t seed);
    void command(Cmd c);
    void update(double dt);
    void set_reduced_motion(bool value) { reduced_ = value; }
    bool camera_animating() const;
    bool simulation_animating() const;
    double next_update_delay() const;
    std::uint64_t speech_revision = 0;
    std::vector<Pos> repainted;
    void camera(Soft3D& r) const;     // where the eye is and how it is turned

    Level lv;                         // a copy: the snail repaints it
    Play play;
    WorldState world;
    std::vector<Event> events;
    std::deque<Speech> speech;        // what is being said (the front is showing)
    std::vector<VisitorDef> cast;     // filled in by the view
    const Tex32* reward_tex = nullptr;
    std::string reward_name;
    bool busy() const { return move_t_ < 1 || turn_t_ < 1 || roll_t_ < 1 || ride_t_ >= 0; }
    bool flipped_view() const { return play.flipped; }
    double won_t = -1;                // seconds since the reward was reached
    int bumps = 0;                    // times walked into something

private:
    bool reduced_ = false;
    std::uint64_t rng_ = 1;
    double t_ = 0;
    // the eye's motion
    V3 from_{}, to_{};
    double move_t_ = 1, move_len_ = .22;
    double yaw_from_ = 0, yaw_to_ = 0, turn_t_ = 1, turn_len_ = .2;
    double roll_from_ = 0, roll_to_ = 0, roll_t_ = 1, roll_len_ = 1.1;
    double ride_t_ = -1, ride_len_ = 2.4;
    int ride_from_ = 0, ride_to_ = 0;
    double flash_ = 0;                // a portal's white blink
    bool snap_ = false;               // after a portal: move the eye to the far side when the step ends
    double bump_ = 0;
    std::deque<Cmd> queue_;
    std::vector<double> door_target_;
    // the movers
    double marble_wait_ = 0;
    std::vector<std::vector<char>> marble_ok_;  // per floor: squares on the maze's loops, where the marble may roll
    // visitors
    double visit_cool_ = 12;
    double visit_t_ = 0;
    int visit_who_ = -1;
    std::vector<int> met_;            // recently seen, so the cast takes turns
    double rand01();
    int rand_int(int n);
    void execute(Cmd c);
    void arrive(const Arrival& a, Pos from);
    void marble_update(double dt);
    void snail_update(double dt);
    void visitor_update(double dt);
    void say(const std::string& who, const std::string& text);
    bool blocked_by_movers(Pos p) const;
    void sfx(const std::string& name, float gain = 1, float rate = 1) { events.push_back({Event::sound, name, {}, gain, rate}); }

public:
    double flash() const { return reduced_ ? 0 : flash_; }
};

}  // namespace mz
