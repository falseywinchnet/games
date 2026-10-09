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
    if (lv.snail.f >= 0) { world.snail.alive = true; world.snail.at = world.snail.to = lv.snail; world.snail.dir = rand_int(4); world.snail.t = 1; }
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
        sfx("mz_elevator", .9f);
        say("", ride_to_ > ride_from_ ? "The floor lifts you up through the ceiling... there was another maze up here all along." : "Down you go, back to the maze below.");
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
        sfx("mz_marble_bump", .5f, .8f);
        return;
    }
    m.dir = nd;
    m.to = {m.at.f, m.at.x + kDX[nd], m.at.y + kDY[nd]};
    m.t = 0;
    const double dist = std::hypot(m.at.x - play.at.x, m.at.y - play.at.y);
    if (m.at.f == play.at.f && dist < 7) sfx("mz_marble_roll", static_cast<float>(std::clamp(.7 - dist * .09, .05, .7)));
}

// ------------------------------------------------------------------ the paint snail
void Session::snail_update(double dt) {
    Mover& s = world.snail;
    if (!s.alive) return;
    if (s.t < 1) {
        s.t = std::min(1.0, s.t + dt * .35);
        return;
    }
    // leaving a square, it repaints the walls around it
    Floor& fl = lv.floors[static_cast<size_t>(s.at.f)];
    if (fl.in(s.to.x, s.to.y)) {
        Cell& c = fl.at(s.at.x, s.at.y);
        for (int d = 0; d < 4; ++d) {
            const int nx = s.at.x + kDX[d], ny = s.at.y + kDY[d];
            if (fl.in(nx, ny) && fl.at(nx, ny).block == Block::wall && !c.goal && rand01() < .6) {
                c.face[static_cast<size_t>(d)] = static_cast<std::uint8_t>(64 + rand_int(paint_count()));
                repainted.push_back(s.at);
            }
        }
    }
    s.at = s.to;
    // keep a hand on the right-hand wall
    const int order[4] = {(s.dir + 1) % 4, s.dir, (s.dir + 3) % 4, (s.dir + 2) % 4};
    for (int d : order) {
        const Pos n{s.at.f, s.at.x + kDX[d], s.at.y + kDY[d]};
        if (lv.open(n) && !lv.cell(n).goal) { s.dir = d; s.to = n; s.t = 0; break; }
    }
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
