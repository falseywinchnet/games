#include "session.hpp"

#include "textures.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace mz {

namespace {
const char* kColorName[kColors] = {"red", "blue", "green", "yellow", "violet"};
double ease(double u) { u = std::clamp(u, 0.0, 1.0); return u * u * (3 - 2 * u); }
double eye_z(int floor, bool flipped) { return floor + (flipped ? .55 : .45); }
}  // namespace

double Session::rand01() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 7;
    rng_ ^= rng_ << 17;
    return (rng_ >> 11) * (1.0 / 9007199254740992.0);
}
int Session::rand_int(int n) { return n <= 1 ? 0 : std::min(n - 1, static_cast<int>(rand01() * n)); }

void Session::say(const std::string& who, const std::string& text) {
    if (!speech.empty() && speech.back().text == text) return;
    if (speech.size() > 3) speech.pop_front();
    speech.push_back({who, text, 0});
    ++speech_revision;
    events.push_back({Event::say, text, who});
}

void Session::start(const Level& level, std::uint64_t seed) {
    lv = level;
    rng_ = seed ? seed : 99;
    play = Play{};
    play.at = lv.start;
    play.dir = lv.start_dir;
    world = WorldState{};
    world.door_open.assign(8, 0);
    door_target_.assign(8, 0);
    world.floor = lv.start.f;
    world.reward = reward_tex;
    world.bulb_spent.assign(lv.things.size(), false);
    // The marble keeps to the maze's loops: everything left once dead-end branches are pruned away (doors count
    // as walls). With no dead end to turn back from, it only ever reverses when it meets you, then rolls on away,
    // so it can block you for a moment but never pen you in.
    marble_ok_.assign(lv.floors.size(), {});
    for (size_t f = 0; f < lv.floors.size(); ++f) {
        const Floor& fl = lv.floors[f];
        std::vector<char>& ok = marble_ok_[f];
        ok.assign(fl.cells.size(), 0);
        for (int y = 0; y < fl.h; ++y)
            for (int x = 0; x < fl.w; ++x) ok[static_cast<size_t>(y * fl.w + x)] = fl.at(x, y).block == Block::open && !fl.at(x, y).goal && !fl.at(x, y).elevator;
        for (bool again = true; again;) {
            again = false;
            for (int y = 0; y < fl.h; ++y)
                for (int x = 0; x < fl.w; ++x) {
                    if (!ok[static_cast<size_t>(y * fl.w + x)]) continue;
                    int n = 0;
                    for (int d = 0; d < 4; ++d) n += fl.in(x + kDX[d], y + kDY[d]) && ok[static_cast<size_t>((y + kDY[d]) * fl.w + x + kDX[d])];
                    if (n < 2) { ok[static_cast<size_t>(y * fl.w + x)] = 0; again = true; }
                }
        }
    }
    if (lv.marble.f >= 0) {
        // start it on the loops nearest its intended square
        const Floor& fl = lv.floors[static_cast<size_t>(lv.marble.f)];
        Pos best{-1, 0, 0};
        int bd = 1 << 30;
        for (int y = 0; y < fl.h; ++y)
            for (int x = 0; x < fl.w; ++x)
                if (marble_ok_[static_cast<size_t>(lv.marble.f)][static_cast<size_t>(y * fl.w + x)]) {
                    const int d = std::abs(x - lv.marble.x) + std::abs(y - lv.marble.y);
                    if (d < bd && !(Pos{lv.marble.f, x, y} == lv.start)) { bd = d; best = {lv.marble.f, x, y}; }
                }
        if (best.f >= 0) { world.marble.alive = true; world.marble.at = world.marble.to = best; world.marble.dir = rand_int(4); world.marble.t = 1; }
    }
    if (lv.snail.f >= 0) snail_place(world.snail, lv.snail, static_cast<std::uint32_t>(rand_int(1 << 30)));
    from_ = to_ = cell_center(play.at, eye_z(0, false) - play.at.f);
    from_.z = to_.z = eye_z(play.at.f, false);
    yaw_from_ = yaw_to_ = play.dir * M_PI / 2;
    move_t_ = turn_t_ = roll_t_ = 1;
    roll_from_ = roll_to_ = 0;
    ride_t_ = -1;
    queue_.clear();
    speech.clear();
    ++speech_revision;
    repainted.clear();
    events.clear();
    won_t = -1;
    bumps = 0;
    visit_cool_ = 10 + rand01() * 10;
    visit_who_ = -1;
    met_.clear();
    place_residents();
}

// ------------------------------------------------------------------ the residents
namespace {
struct Dice {  // the residents' own dice, so placing them changes nothing else about the level
    std::uint64_t r;
    std::uint64_t next(std::uint64_t n) {
        r += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = r;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return n ? (z ^ (z >> 31)) % n : 0;
    }
};
}  // namespace

void Session::place_residents() {
    Dice dice{rng_ ^ 0x5EEDF00DULL};
    std::vector<Pos> open;  // quiet squares: no pad, portal, elevator, thing or reward room, and not where you start
    for (int f = 0; f < static_cast<int>(lv.floors.size()); ++f) {
        const Floor& fl = lv.floors[static_cast<size_t>(f)];
        for (int y = 0; y < fl.h; ++y)
            for (int x = 0; x < fl.w; ++x) {
                const Pos p{f, x, y};
                const Cell& c = fl.at(x, y);
                if (c.block == Block::open && !c.goal && !c.elevator && c.portal < 0 && c.pad < 0 && c.cpad < 0 && lv.thing_at(p) < 0 && !(p == lv.start)) open.push_back(p);
            }
    }
    if (!open.empty() && dice.next(100) < 55) {
        world.ceiling_cat.alive = true;
        world.ceiling_cat.at = open[static_cast<size_t>(dice.next(open.size()))];
    }
    if (open.size() > 1 && dice.next(100) < 45) {
        Pos p = open[static_cast<size_t>(dice.next(open.size()))];
        if (world.ceiling_cat.alive && p == world.ceiling_cat.at) p = open[(static_cast<size_t>(dice.next(open.size())))];
        if (!(world.ceiling_cat.alive && p == world.ceiling_cat.at)) {
            world.goose.alive = true;
            world.goose.at = p;
        }
    }
    // Keyboard Cat waits just beyond a portal, beside where it lets you out
    if (!lv.portals.empty()) {
        std::vector<Pos> ends;
        for (int f = 0; f < static_cast<int>(lv.floors.size()); ++f)
            for (int y = 0; y < lv.floors[static_cast<size_t>(f)].h; ++y)
                for (int x = 0; x < lv.floors[static_cast<size_t>(f)].w; ++x)
                    if (lv.has_portal({f, x, y})) ends.push_back({f, x, y});
        if (!ends.empty()) {
            const Pos out = ends[static_cast<size_t>(dice.next(ends.size()))];
            for (int k = 0, d0 = static_cast<int>(dice.next(4)); k < 4; ++k) {
                const int d = (d0 + k) % 4;
                const Pos n{out.f, out.x + kDX[d], out.y + kDY[d]};
                if (std::find(open.begin(), open.end(), n) != open.end()) {
                    world.keyboard_cat.alive = true;
                    world.keyboard_cat.at = n;
                    break;
                }
            }
        }
    }
    // Philosoraptor stands in the reward room, beside the trophy
    if (lv.goal.f >= 0) {
        const Floor& fl = lv.floors[static_cast<size_t>(lv.goal.f)];
        for (int k = 0, d0 = static_cast<int>(dice.next(4)); k < 8 && !world.raptor.alive; ++k) {
            const int d = (d0 + k) % 4, reach = k < 4 ? 1 : 2;
            const Pos n{lv.goal.f, lv.goal.x + kDX[d] * reach, lv.goal.y + kDY[d] * reach};
            if (fl.in(n.x, n.y) && fl.at(n.x, n.y).goal && fl.at(n.x, n.y).block == Block::open) {
                world.raptor.alive = true;
                world.raptor.at = n;
            }
        }
    }
    if (world.marble.alive) world.ball = static_cast<Ball>(dice.next(static_cast<std::uint64_t>(Ball::count)));
    world.ceiling_cat.wait = 2;
    world.goose.wait = 3;
    world.keyboard_cat.wait = 1;
    world.raptor.wait = 0;
    thinker_t_ = 0;
    ball_grumble_ = 3;
}

void Session::residents_update(double dt) {
    const auto dist_to = [this](Pos p) {  // squares away, on the same floor (far otherwise)
        return p.f == play.at.f ? std::hypot(p.x - play.at.x, p.y - play.at.y) : 99.0;
    };
    const auto gain_at = [](double d, double loud, double reach) { return static_cast<float>(std::clamp(loud * (1 - d / reach), 0.0, 1.0)); };
    if (won_t >= 0) return;
    // Ceiling Cat only meows
    if (Dweller& c = world.ceiling_cat; c.alive) {
        c.wait -= dt;
        const double d = dist_to(c.at);
        if (c.wait <= 0 && d < 5) {
            sfx("mz_meow", gain_at(d, .8, 6), static_cast<float>(.95 + .1 * rand01()));
            if (d < 2.5) say("Ceiling Cat", "Meow.");
            c.wait = 4 + rand01() * 4;
        }
    }
    // the Goose honks, and you hear it from a way off
    if (Dweller& g = world.goose; g.alive) {
        g.wait -= dt;
        const double d = dist_to(g.at);
        if (g.wait <= 0 && d < 10) {
            sfx("mz_honk", gain_at(d, .95, 11), static_cast<float>(.95 + .1 * rand01()));
            if (d < 2.5) say("The Goose", "HONK.");
            g.wait = 2.5 + rand01() * 4;
        }
    }
    // Keyboard Cat taunts you to come closer, and plays you off when you do
    if (Dweller& k = world.keyboard_cat; k.alive) {
        k.wait -= dt;
        const double d = dist_to(k.at);
        if (k.wait <= 0 && d < 4.5) {
            if (d < 1.5) {
                sfx("mz_keys", .8f);
                say("Keyboard Cat", "(plays you off)");
                k.wait = 9;
            } else {
                const std::vector<std::string>& lines = keyboard_cat_lines();
                say("Keyboard Cat", lines[static_cast<size_t>(rand_int(static_cast<int>(lines.size())))]);
                k.wait = 5 + rand01() * 3;
            }
        }
    }
    // Philosoraptor, in the reward room, wonders aloud as you come near
    if (Dweller& r = world.raptor; r.alive) {
        r.wait -= dt;
        if (r.wait <= 0 && dist_to(r.at) < 3.2) {
            const std::vector<std::string>& lines = raptor_lines();
            say("Philosoraptor", lines[static_cast<size_t>(rand_int(static_cast<int>(lines.size())))]);
            r.pose = 1 - r.pose;
            r.wait = 7 + rand01() * 4;
        }
    }
    // the ball: the frog rings his bell as he comes, Mazeball burps, and the Thinking Face has his own music
    if (world.marble.alive) {
        const double d = dist_to(world.marble.at);
        if (world.ball == Ball::thinker) {
            thinker_t_ -= dt;
            if (thinker_t_ <= 0) {  // a four-second loop, started again each time at the volume of where he is
                if (d < 8) sfx("mz_thinker", gain_at(d, .75, 8));
                thinker_t_ += 4.0;
            }
        } else if (world.ball == Ball::frog || world.ball == Ball::mazeball) {
            ball_grumble_ -= dt;
            if (ball_grumble_ <= 0) {
                if (d < 6) sfx(world.ball == Ball::frog ? "mz_bell" : "mz_burp", gain_at(d, .7, 7), static_cast<float>(.9 + .2 * rand01()));
                ball_grumble_ = 3 + rand01() * 4;
            }
        }
    }
}

void Session::command(Cmd c) {
    if (won_t >= 0) return;
    if (queue_.size() < 2) queue_.push_back(c);
}

bool Session::blocked_by_movers(Pos p) const {
    const Mover& m = world.marble;
    if (m.alive && ((m.at == p) || (m.to == p))) return true;
    if (world.visitor.alive && world.visitor.at == p) return true;
    return false;
}

void Session::execute(Cmd c) {
    if (c == Cmd::left || c == Cmd::right) {
        play.dir = (play.dir + (c == Cmd::left ? 3 : 1)) % 4;
        yaw_from_ = yaw_to_;
        yaw_to_ += c == Cmd::left ? -M_PI / 2 : M_PI / 2;
        turn_t_ = 0;
        sfx("mz_turn", .35f);
        return;
    }
    const int d = c == Cmd::forward ? play.dir : (play.dir + 2) % 4;
    const Pos ahead{play.at.f, play.at.x + kDX[d], play.at.y + kDY[d]};
    if (blocked_by_movers(ahead)) {
        bump_ = 1;
        ++bumps;
        if (world.visitor.alive && world.visitor.at == ahead) { sfx("mz_bump", .5f); say(world.visitor.alive && visit_who_ >= 0 ? cast[static_cast<size_t>(visit_who_)].name : "", "Excuse me!"); }
        else if (world.ball == Ball::frog) {
            sfx("mz_bell", .8f);
            const std::vector<std::string>& lines = frog_lines();
            say("The frog", lines[static_cast<size_t>(rand_int(static_cast<int>(lines.size())))]);
        } else if (world.ball == Ball::mazeball) { sfx("mz_burp", .8f); say("Mazeball", "*burp*"); }
        else if (world.ball == Ball::thinker) { sfx("mz_marble_bump", .5f, 1.4f); say("The Thinking Face", "Hmm."); }
        else { sfx("mz_marble_bump", .8f); say("", "The giant marble blocks the way. It will roll on."); }
        return;
    }
    const Pos from = play.at;
    Arrival arr;
    const StepResult res = step(lv, play, d, arr);
    if (res != StepResult::moved) {
        bump_ = 1;
        ++bumps;
        if (res == StepResult::blocked_door) {
            const int col = lv.cell(ahead).door;
            sfx("mz_locked", .7f);
            say("", std::string("The ") + kColorName[col % kColors] + " door is locked. Somewhere there is a " + kColorName[col % kColors] + " pad.");
        } else {
            sfx("mz_bump", .5f);
        }
        return;
    }
    // glide to the square walked into; what happens there happens when we arrive
    from_ = cell_center(from);
    from_.z = eye_z(from.f, roll_to_ > 1);
    const Pos walked{from.f, from.x + kDX[d], from.y + kDY[d]};
    to_ = cell_center(walked);
    to_.z = from_.z;
    move_t_ = 0;
    move_len_ = .22;
    sfx("mz_step_0" + std::to_string(1 + rand_int(3)), .45f, .95f + .1f * static_cast<float>(rand01()));
    arrive(arr, walked);
}

void Session::arrive(const Arrival& a, Pos walked) {
    if (a.pressed) {
        door_target_[static_cast<size_t>(a.pad)] = 1;
        sfx("mz_pad", .8f);
        sfx("mz_door", .7f);
        say("", std::string("Click. Somewhere a ") + kColorName[a.pad % kColors] + " door sinks into the floor.");
    }
    if (a.flipped) {
        roll_from_ = roll_from_ + (roll_to_ - roll_from_) * ease(roll_t_);
        roll_to_ = play.flipped ? M_PI : 0;
        roll_t_ = 0;
        sfx("mz_flip", .9f);
        say("", play.flipped ? "The stone spins, and the whole maze rolls over. You are walking on the ceiling." : "The maze rolls back over. Floor is floor again.");
    }
    if (a.bulb) {
        sfx(play.blackout ? "mz_bulb_off" : "mz_bulb_on", .9f);
        say("", play.blackout ? "Pop! The lights go out. Only your little lamp is left." : "The lights snap back on.");
    }
    if (a.elevator) {
        ride_t_ = 0;
        ride_from_ = a.from_floor;
        ride_to_ = play.at.f;
        world.elevator_at = walked;
        world.monke_rides = rand01() < .4;
        sfx("mz_elevator", .9f);
        say("", ride_to_ > ride_from_ ? "The floor lifts you up through the ceiling... there was another maze up here all along." : "Down you go, back to the maze below.");
        if (world.monke_rides) {  // Monke is riding too
            sfx("mz_ooh", .8f);
            const std::vector<std::string>& lines = monke_lines();
            say("Monke", lines[static_cast<size_t>(rand_int(static_cast<int>(lines.size())))]);
        }
    }
    if (a.portal) {
        flash_ = 1;
        snap_ = true;  // the walk ends somewhere else entirely
        sfx("mz_portal", .9f);
    }
    // arriving by portal or elevator right where the marble is: it rolls aside (it never shares a square with you)
    if ((a.portal || a.elevator) && world.marble.alive && (world.marble.at == play.at || world.marble.to == play.at)) {
        Mover& m = world.marble;
        const Floor& fl = lv.floors[static_cast<size_t>(m.at.f)];
        Pos away = m.at == play.at ? m.to : m.at;
        if (away == play.at) {
            for (int d = 0; d < 4; ++d) {
                const Pos n{play.at.f, play.at.x + kDX[d], play.at.y + kDY[d]};
                if (fl.in(n.x, n.y) && marble_ok_[static_cast<size_t>(n.f)][static_cast<size_t>(n.y * fl.w + n.x)]) { away = n; break; }
            }
        }
        m.at = m.to = away;
        m.t = 1;
        marble_wait_ = .8;
        sfx("mz_marble_bounce", .6f);
    }
    if ((a.portal || a.elevator) && world.visitor.alive && world.visitor.at == play.at) world.visitor.alive = false;
    if (a.won) {
        won_t = 0;
        events.push_back({Event::won, reward_name, {}});
        sfx("mz_stinger_win", .9f);
    }
}

void Session::camera(Soft3D& r) const {
    if (reduced_) {
        const V3 center = cell_center(play.at);
        r.eye = {center.x,center.y,eye_z(play.at.f,play.flipped)};
        r.yaw = yaw_to_; r.roll = play.flipped ? M_PI : 0; r.pitch = 0; r.blackout = play.blackout;
        return;
    }
    V3 e = from_ + (to_ - from_) * ease(move_t_);
    if (ride_t_ >= 0) {
        const double u = ease(ride_t_ / ride_len_);
        e.z = eye_z(ride_from_, false) + (ride_to_ - ride_from_) * u;
    } else if (roll_t_ < 1) {
        const double u = ease(roll_t_);
        e.z = eye_z(play.at.f, roll_from_ > 1) + (eye_z(play.at.f, roll_to_ > 1) - eye_z(play.at.f, roll_from_ > 1)) * u;
    } else {
        e.z = eye_z(play.at.f, play.flipped);
    }
    // a nudge forward and back on a bump
    const double b = std::sin(bump_ * M_PI) * .07;
    const double yaw = yaw_from_ + (yaw_to_ - yaw_from_) * ease(turn_t_);
    e.x += std::sin(yaw) * b;
    e.y += std::cos(yaw) * b;
    // a gentle walking bob
    if (move_t_ < 1) e.z += std::sin(move_t_ * M_PI) * .015;
    r.eye = e;
    r.yaw = yaw;
    r.roll = roll_from_ + (roll_to_ - roll_from_) * ease(roll_t_);
    r.pitch = 0;
    r.blackout = play.blackout;
}

bool Session::camera_animating() const {
    if (busy() || bump_ > 0 || flash_ > 0 || snap_) return true;
    for (std::size_t i=0;i<world.door_open.size();++i)
        if (world.door_open[i] < door_target_[i]) return true;
    return false;
}
bool Session::simulation_animating() const {
    return camera_animating() || world.marble.alive || world.snail.alive || world.visitor.alive;
}
double Session::next_update_delay() const {
    if (simulation_animating() || !queue_.empty() || (won_t>=0 && won_t<1.6)) return .033;
    double delay=std::numeric_limits<double>::infinity();
    if (!cast.empty() && won_t<0 && !play.blackout) delay=std::max(.001,visit_cool_);
    if (!speech.empty())
        delay=std::min(delay,std::max(.001,2.6+speech.front().text.size()/22.0-speech.front().age));
    return delay;
}

void Session::update(double dt) {
    repainted.clear();
    t_ += dt;
    move_t_ = std::min(1.0, move_t_ + dt / move_len_);
    turn_t_ = std::min(1.0, turn_t_ + dt / turn_len_);
    roll_t_ = std::min(1.0, roll_t_ + dt / roll_len_);
    bump_ = std::max(0.0, bump_ - dt * 5);
    flash_ = std::max(0.0, flash_ - dt * 2.5);
    if (move_t_ >= 1 && snap_) {
        // through the portal: the eye lands where the rules already put us
        snap_ = false;
        const V3 c = cell_center(play.at);
        from_ = to_ = {c.x, c.y, eye_z(play.at.f, play.flipped)};
    }
    if (ride_t_ >= 0) {
        ride_t_ += dt;
        const double u = ease(ride_t_ / ride_len_);
        world.elevator_lift = ride_to_ > ride_from_ ? u : 1 - u;
        world.floor = ride_from_;
        if (ride_t_ >= ride_len_) {
            ride_t_ = -1;
            world.elevator_lift = -1;
            world.monke_rides = false;  // Monke stays on the plate for the next one
            world.floor = ride_to_;
            from_.z = to_.z = eye_z(ride_to_, play.flipped);
        }
    } else {
        world.floor = play.at.f;
    }
    for (size_t i = 0; i < world.door_open.size(); ++i) world.door_open[i] = std::min(door_target_[i], world.door_open[i] + dt / 1.4);
    world.pressed = play.pressed;
    if (won_t >= 0) won_t += dt;
    // Age existing speech before an encounter can add a new line. A deadline
    // wake may account for several quiet seconds; newly spoken words are fresh.
    for (Speech& s : speech) s.age += dt;
    while (!speech.empty() && speech.front().age >= 2.6 + speech.front().text.size() / 22.0) {
        speech.pop_front();
        ++speech_revision;
    }
    marble_update(dt);
    snail_update(dt);
    residents_update(dt);
    visitor_update(dt);
    if (!busy() && !queue_.empty() && won_t < 0) {
        const Cmd c = queue_.front();
        queue_.pop_front();
        execute(c);
    }
}

// ------------------------------------------------------------------ the marble
void Session::marble_update(double dt) {
    Mover& m = world.marble;
    if (!m.alive) return;
    const double speed = 1.5;
    if (marble_wait_ > 0) {
        marble_wait_ -= dt;
        if (marble_wait_ <= 0) {
            // it changes its mind and rolls back the way it came
            m.dir = (m.dir + 2) % 4;
        }
        return;
    }
    if (m.t < 1) {
        m.t = std::min(1.0, m.t + speed * dt);
        m.roll += speed * dt / .42;
        return;
    }
    m.at = m.to;
    // pick the next square: straight on if it can, otherwise bounce off at random
    struct Allowed { const Level& lv; const std::vector<std::vector<char>>& cells; Pos at;
        bool operator()(int d) const {
            const Pos n{at.f,at.x+kDX[d],at.y+kDY[d]};
            const Floor& fl=lv.floors[static_cast<size_t>(n.f)];
            return fl.in(n.x,n.y) && cells[static_cast<size_t>(n.f)][static_cast<size_t>(n.y*fl.w+n.x)];
        }
    };
    const Allowed ok{lv,marble_ok_,m.at};
    const Pos me = play.at;
    int nd = -1;
    if (ok(m.dir)) nd = m.dir;
    else {
        int opts[4], k = 0;
        for (int d = 0; d < 4; ++d)
            if (d != (m.dir + 2) % 4 && ok(d)) opts[k++] = d;
        if (k) nd = opts[rand_int(k)];
        else if (ok((m.dir + 2) % 4)) nd = (m.dir + 2) % 4;
        if (nd >= 0) sfx("mz_marble_bounce", .25f, .9f + .2f * static_cast<float>(rand01()));
    }
    if (nd < 0) return;
    if ((Pos{m.at.f,m.at.x+kDX[nd],m.at.y+kDY[nd]}) == me) {
        // face to face: it stops and waits, then rolls away. It never runs you over and never pens you in.
        m.dir = nd;
        marble_wait_ = 1.4;
        if (world.ball == Ball::frog) {  // irritated, and he lets you know
            sfx("mz_bell", .7f);
            const std::vector<std::string>& lines = frog_lines();
            say("The frog", lines[static_cast<size_t>(rand_int(static_cast<int>(lines.size())))]);
        } else if (world.ball == Ball::mazeball) {
            sfx("mz_burp", .6f);
        } else if (world.ball == Ball::rock) {
            sfx("mz_marble_bump", .5f, .8f);
        }
        return;
    }
    m.dir = nd;
    m.to = {m.at.f, m.at.x + kDX[nd], m.at.y + kDY[nd]};
    m.t = 0;
    const double dist = std::hypot(m.at.x - play.at.x, m.at.y - play.at.y);
    if (m.at.f == play.at.f && dist < 7 && (world.ball == Ball::rock || world.ball == Ball::mazeball))
        sfx("mz_marble_roll", static_cast<float>(std::clamp(.7 - dist * .09, .05, .7)));
}

// ------------------------------------------------------------------ the paint snail
void Session::snail_update(double dt) {
    if (!world.snail.alive) return;
    Soft3D eye;
    camera(eye);
    const SnailNews news = snail_step(world.snail, lv, world.patches, eye.eye, dt);
    const Surface& on = world.snail.on;
    if (news.painted) repainted.push_back({on.f, on.x, on.y});
    // heard only near, and on its own floor
    const double dist = len(eye.eye - world.snail.world());
    if (on.f != play.at.f || dist > 7) return;
    const float gain = static_cast<float>(std::clamp(.6 - dist * .085, .04, .6));
    if (news.squelched) sfx("mz_snail_squelch", gain * .7f, static_cast<float>(.9 + .2 * std::sin(world.snail.clock * 7.3)));
    if (news.hid) sfx("mz_snail_hide", gain);
    if (news.peeked) sfx("mz_snail_peek", gain * .8f);
}

// ------------------------------------------------------------------ visitors
void Session::visitor_update(double dt) {
    Visitor& v = world.visitor;
    if (v.alive) {
        visit_t_ += dt;
        v.bob += dt * 3;
        // in for a chat, then gone with a poof
        const double stay = 6.5;
        if (visit_t_ < .4) v.appear = visit_t_ / .4;
        else if (visit_t_ > stay) v.appear = std::max(0.0, 1 - (visit_t_ - stay) / .4);
        else v.appear = 1;
        if (visit_t_ > stay + .4) { v.alive = false; sfx("mz_poof", .6f); visit_cool_ = 18 + rand01() * 22; }
        return;
    }
    if (cast.empty() || won_t >= 0 || busy() || play.blackout) return;
    visit_cool_ -= dt;
    if (visit_cool_ > 0) return;
    // someone steps into the corridor two or three squares ahead
    Pos p = play.at;
    Pos spot{-1, 0, 0};
    for (int k = 1; k <= 3; ++k) {
        p = {p.f, p.x + kDX[play.dir], p.y + kDY[play.dir]};
        if (!passable(lv, play, p) || blocked_by_movers(p) || lv.thing_at(p) >= 0 || p == lv.goal || lv.cell(p).goal) break;
        if (k >= 2) spot = p;
    }
    if (spot.f < 0) { visit_cool_ = 3; return; }
    // the cast take turns
    int who = rand_int(static_cast<int>(cast.size()));
    for (int tries = 0; tries < 8 && std::find(met_.begin(), met_.end(), who) != met_.end(); ++tries) who = rand_int(static_cast<int>(cast.size()));
    met_.push_back(who);
    if (met_.size() > std::min<size_t>(10, cast.size() - 1)) met_.erase(met_.begin());
    const VisitorDef& def = cast[static_cast<size_t>(who)];
    visit_who_ = who;
    v.alive = true;
    v.at = spot;
    v.tex = def.tex;
    v.w = def.w;
    v.h = def.h;
    v.lift = def.lift;
    v.appear = 0;
    visit_t_ = 0;
    sfx("mz_appear", .6f);
    if (!def.lines.empty()) say(def.name, def.lines[static_cast<size_t>(rand_int(static_cast<int>(def.lines.size())))]);
    // and sometimes, a good turn
    const double roll = rand01();
    int locked = -1;
    for (int c = 0; c < lv.params.doors && locked < 0; ++c)
        if (!(play.pressed >> c & 1)) locked = c;
    if (roll < .18 && locked >= 0 && lv.params.doors > 0) {
        play.pressed |= 1u << locked;
        door_target_[static_cast<size_t>(locked)] = 1;
        sfx("mz_door", .6f);
        say(def.name, std::string("Here, a ") + kColorName[locked % kColors] + " key turned up in my pocket. Your door is open.");
    } else if (roll < .45) {
        // a hint: which way the reward lies, as the crow flies
        const V3 g = cell_center(lv.goal), me = cell_center(play.at);
        const double dx = g.x - me.x, dy = g.y - me.y;
        std::string dir;
        if (std::fabs(dy) > std::fabs(dx) * .4) dir += dy > 0 ? "north" : "south";
        if (std::fabs(dx) > std::fabs(dy) * .4) dir += dx > 0 ? "east" : "west";
        std::string where = lv.goal.f != play.at.f ? (lv.goal.f > play.at.f ? " And up, I think. Upstairs." : " And down below.") : "";
        say(def.name, "The " + reward_name + "? Off to the " + dir + ", as the crow flies." + where);
    } else if (roll < .55 && world.marble.alive) {
        marble_wait_ = 0;
        world.marble.dir = (world.marble.dir + 2) % 4;
        say(def.name, "I'll keep that great marble busy for you.");
    }
}

}  // namespace mz
