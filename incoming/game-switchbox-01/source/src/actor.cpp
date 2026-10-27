#include "actor.hpp"

#include <algorithm>
#include <cmath>

namespace sbx {

namespace {
constexpr double kRootY = .44;
constexpr double kMaxLean = .36;  // she reaches with her arms; she never sprawls over the switches
constexpr double kStep = 1.0 / 240;  // spring integration step
double ease(double u) { u = std::clamp(u, 0.0, 1.0); return u * u * (3 - 2 * u); }
}  // namespace

Actor::Actor(std::uint64_t seed) : rng_(seed * 0x9E3779B97F4A7C15ULL + 1) {
    rise_.snap(kZHidden); rise_.k = 220; rise_.c = 16;
    slide_.snap(0); slide_.k = 55; slide_.c = 12;
    lean_.k = 90; lean_.c = 13;
    side_.k = 70; side_.c = 11;
    squash_.snap(1); squash_.k = 280; squash_.c = 10;
    head_yaw_.k = 80; head_yaw_.c = 12;
    head_pitch_.k = 80; head_pitch_.c = 12; head_pitch_.snap(-.22);
    head_roll_.k = 80; head_roll_.c = 11;
    lid_.k = 150; lid_.c = 11;
    for (Hand& h : hands_) { h.pos.k = 420; h.pos.c = 36; h.reach.k = 130; h.reach.c = 21; }
    hair_.k = 70; hair_.c = 9;
    ahoge_.k = 110; ahoge_.c = 4;
    next_peek_ = 3 + 4 * rand01();
}

double Actor::rand01() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 7; rng_ ^= rng_ << 17;
    return static_cast<double>(rng_ >> 11) * (1.0 / 9007199254740992.0);
}

void Actor::emit(const std::string& sound, float gain, float rate) { cues.push_back({Cue::sound, sound, gain, rate}); }

void Actor::say(const std::string& category, double chance) {
    if (say_cool_ > 0 || rand01() > chance) return;
    cues.push_back({Cue::line, category});
    say_cool_ = 3.5;
}

bool Actor::take_reset_request() {
    const bool r = reset_request_;
    reset_request_ = false;
    return r;
}

// A correct switch stays up with its lamp lit: she leaves it alone and only
// frets. A miss puts every lamp out, so she pushes down everything that is up.
void Actor::flipped(int sw, bool correct, bool solved) {
    last_switch_ = sw;
    if (solved) {
        mode_ = Mode::celebrate;
        celebrate_stage_ = 0;
        stage_t_ = 0;
        mode_t_ = 0;
        return;
    }
    mood_t_ = 2.5;
    if (correct) {
        mood_ = 1;  // a lamp lit: nervous
        say("nervous", .3);
        if (mode_ == Mode::hidden) { mode_ = Mode::peek; mode_t_ = 0; }
        if (mode_ == Mode::peek) mode_t_ = std::min(mode_t_, 1.0);
        return;
    }
    mood_ = 2;  // all dark again: smug
    sweep_ = true;
    if (mode_ == Mode::hidden && react_ <= 0) react_ = .1 + .12 * rand01();
    if (mode_ == Mode::peek) react_ = .04;
    if (mode_ == Mode::duck) { mode_ = Mode::up; mode_t_ = 0; squash_.v += 2.5; }
    linger_ = 0;
}

int Actor::bonk_level() const {
    const int b = static_cast<int>(bonks_);
    return b <= 0 ? 0 : b < 4 ? 1 : b < 8 ? 2 : b < 12 ? 3 : b < 16 ? 4 : 5;
}

void Actor::head_clicked() {
    if (mode_ == Mode::mole || mode_ == Mode::great || mode_ == Mode::celebrate) return;
    if (mode_ == Mode::hidden || mode_ == Mode::duck) {  // a knock on the lid: she peeks out, cross
        if (rise_.x < kZPeek - .1) { mode_ = Mode::peek; mode_t_ = 0; mood_ = 3; mood_t_ = 3; }
        lid_.v += 3;
        return;
    }
    bonks_ = std::floor(bonks_) + 1;
    bonk_idle_ = 0;
    eep_ = .45;
    squash_.v -= 3.5;
    // a wobble that never builds up: always tip away from where she is already leaning
    head_roll_.v = (head_roll_.x > 0 ? -1 : 1) * std::min(4.5, 3 + .1 * bonks_);
    emit("switchbox_giggle_boing", .7f, static_cast<float>(.9 + .03 * bonks_ + .08 * rand01()));
    if (mode_ == Mode::peek) { mode_ = Mode::up; mode_t_ = 0; }
    linger_ = 2;
    if (bonks_ >= 20) {
        // too much: she shrieks and drops out of sight, and the switches start popping
        bonks_ = 0;
        mole_request_ = true;
        mode_ = Mode::duck;
        mode_t_ = 0;
        emit("switchbox_reset", .8f, 1.4f);
        cues.push_back({Cue::line, "mole_start"});
        say_cool_ = 3;
        return;
    }
    const int lv = bonk_level();
    say(lv <= 1 ? "eep" : "head" + std::to_string(lv), lv <= 1 ? .45 : .4);
}

bool Actor::resting_on_hold() const {
    for (const Hand& h : hands_)
        if ((h.state == HandState::wait && h.reach.x > .8) || h.state == HandState::carry) return true;
    return false;
}

bool Actor::take_pointer_drop(V3& where) {
    if (!dropped_) return false;
    dropped_ = false;
    where = carry_at_;
    return true;
}

bool Actor::take_forced_release() {
    const bool f = forced_;
    forced_ = false;
    return f;
}

void Actor::steal(int sw) {
    if (stolen_ >= 0) return;
    steal_sw_ = sw;
}

void Actor::hole_clicked(int sw) {
    if (sw != stolen_ || mode_ == Mode::taunt || returning_ || mode_ == Mode::mole) return;
    mode_ = Mode::taunt;
    mode_t_ = 0;
    squash_.v += 3.5;
    emit("switchbox_reach", .8f, 1.1f);
    emit("switchbox_giggle_boing", .8f, 1.15f);
    cues.push_back({Cue::line, "taunt"});
    say_cool_ = 3;
}

bool Actor::take_mole_request() {
    const bool r = mole_request_;
    mole_request_ = false;
    return r;
}

void Actor::mole_begin() {
    mode_ = Mode::mole;
    mode_t_ = 0;
    jobs_.clear();
    claimed_.fill(false);
    for (Hand& h : hands_) { h.state = HandState::idle; h.job = -1; }
    point_t_ = 0;
    bonks_ = 0;
}

void Actor::mole_end(bool great) {
    mole_great_ = great;
    mode_ = great ? Mode::great : Mode::up;
    mode_t_ = 0;
    squash_.v += great ? 4.5 : 2.5;
    emit("switchbox_reach", .8f);
    if (great) {
        emit("stinger_win_switchbox", .9f);
        cues.push_back({Cue::special, "great"});
    } else {
        mood_ = 2;
        mood_t_ = 3;
        cues.push_back({Cue::line, "mole_fail"});
    }
    say_cool_ = 4;
    linger_ = 2;
}

void Actor::react(Reaction r, int first) {
    auto come_up = [&]() {
        if (mode_ == Mode::hidden || mode_ == Mode::peek || mode_ == Mode::duck) {
            if (mode_ != Mode::duck) emit("switchbox_reach", .8f);
            mode_ = Mode::up;
            mode_t_ = 0;
            squash_.v += 3;
        }
        linger_ = 1.5;
    };
    switch (r) {
        case Reaction::hint_first:
        case Reaction::forgot:
            come_up();
            point_sw_ = first;
            point_t_ = 2.6;
            mood_ = r == Reaction::forgot ? 5 : 6;
            mood_t_ = 3;
            break;
        case Reaction::scold:
            come_up();
            mood_ = 4;
            mood_t_ = 2.4;
            break;
        case Reaction::rip_out:
            // she vanishes into the box, then yanks the switches down from inside, one by one
            sweep_ = false;
            jobs_.clear();
            claimed_.fill(false);
            for (Hand& h : hands_) if (h.state != HandState::idle) { h.state = HandState::retract; h.timer = 0; h.job = -1; }
            point_t_ = 0;
            if (mode_ != Mode::hidden) { mode_ = Mode::duck; mode_t_ = 0; }
            rip_queue_.clear();
            for (int i = 0; i < kSwitches; ++i) if (i != first) rip_queue_.push_back(i);
            // start from the side nearest her, a little unpredictably
            if (rand01() < .5) std::reverse(rip_queue_.begin(), rip_queue_.end());
            rip_t_ = .45;
            mood_ = 4;
            mood_t_ = 1.5;
            break;
        case Reaction::restore:
            rip_queue_.clear();
            restore_pending_ = true;
            mood_ = 7;  // sulky
            mood_t_ = 4;
            if (mode_ == Mode::hidden) { mode_ = Mode::peek; mode_t_ = 0; }
            break;
        case Reaction::stuck_scold: come_up(); mood_ = 3; mood_t_ = 2; break;
        case Reaction::stuck_grumble: mood_ = 3; mood_t_ = 1.2; break;
        case Reaction::none: break;
    }
}

// The smallest lid opening (0..1) whose underside clears a head of hair
// centred at `c`: the lid is a line from the hinge, so test the distance from
// the head's centre to that segment in the y-z plane.
double Actor::lid_clearance(V3 c, double scale) {
    const double R = .5 * scale;  // head plus hair, a little generous
    const double hy = Stage::kOpenY1 + .06, hz = Stage::kBoxZ + .02 - .05;
    const double depth = Stage::kOpenY1 - Stage::kOpenY0 + .08;
    if (c.z + R < hz) return 0;
    for (double u = 0; u <= 1.0; u += .02) {
        const double a = u * 1.72;
        const double dy = -std::cos(a), dz = std::sin(a);  // along the lid from the hinge toward its free edge
        const double py = c.y - hy, pz = c.z - hz;
        const double along = std::clamp(py * dy + pz * dz, 0.0, depth);
        const double ey = py - dy * along, ez = pz - dz * along;
        if (ey * ey + ez * ez > R * R) return u;
    }
    return 1;
}

V3 Actor::rest_world(const StageState& st, int side) const {
    return body_frame(st.girl).apply(st.girl.rest[static_cast<size_t>(side)]);
}

void Actor::brain(double dt, StageState&) {
    mode_t_ += dt;
    say_cool_ = std::max(0.0, say_cool_ - dt);
    mood_t_ = std::max(0.0, mood_t_ - dt);
    if (mood_t_ <= 0) mood_ = 0;
    eep_ = std::max(0.0, eep_ - dt);
    point_t_ = std::max(0.0, point_t_ - dt);
    bonk_idle_ += dt;
    if (bonk_idle_ > 3) bonks_ = std::max(0.0, bonks_ - dt * 1.2);
    // a stolen switch: if nobody comes asking for it, she comes out to gloat anyway
    if (stolen_ >= 0 && mode_ != Mode::taunt && !returning_) {
        stolen_t_ += dt;
        if (stolen_t_ > 15 && (mode_ == Mode::hidden || mode_ == Mode::peek)) hole_clicked(stolen_);
    }
    switch (mode_) {
        case Mode::hidden:
            rise_.target = kZHidden;
            rise_.k = 150; rise_.c = 17;
            if (!jobs_.empty() && rip_queue_.empty() && rip_cur_ < 0 && restore_queue_.empty()) {
                if (react_ <= 0) react_ = .1;
                lid_.target = .05;  // she has noticed: the lid twitches
                react_ -= dt;
                if (react_ <= 0) {
                    mode_ = Mode::up;
                    mode_t_ = 0;
                    squash_.v += 3.2;  // launch stretch
                    emit("switchbox_reach", .8f, static_cast<float>(.95 + .1 * rand01()));
                    say("pop", first_pop_ ? 1 : .3);
                    first_pop_ = false;
                }
            } else {
                lid_.target = 0;
                next_peek_ -= dt;
                if (next_peek_ <= 0) { mode_ = Mode::peek; mode_t_ = 0; }
            }
            break;
        case Mode::peek:
            rise_.target = kZPeek;
            rise_.k = 60; rise_.c = 12;
            if (!jobs_.empty()) {
                react_ -= dt;
                if (react_ <= 0) { mode_ = Mode::up; mode_t_ = 0; squash_.v += 3.2; emit("switchbox_reach", .8f); }
            } else if (mode_t_ > 2.6 + (mood_ == 3 ? 1.0 : 0.0) && restore_queue_.empty() && !restore_pending_) {
                mode_ = Mode::hidden;
                mode_t_ = 0;
                next_peek_ = 9 + 14 * rand01();
                say("idle_box", .25);
            }
            break;
        case Mode::up: {
            rise_.target = kZUp;
            rise_.k = 230; rise_.c = 17;
            const bool busy = !jobs_.empty() || hands_[0].state != HandState::idle || hands_[1].state != HandState::idle || point_t_ > 0 || mood_ == 4 ||
                              bonks_ >= 4 || eep_ > 0;
            if (busy) {
                linger_ = .7 + .7 * rand01();
            } else {
                linger_ -= dt;
                if (linger_ <= 0) {
                    mode_ = Mode::duck;
                    mode_t_ = 0;
                    squash_.v += 1.5;  // a little hop before dropping
                    if (mood_ == 2) say("smug", .35);
                }
            }
            break;
        }
        case Mode::duck:
            rise_.target = mode_t_ < .08 ? kZUp + .06 : kZHidden;
            rise_.k = 170; rise_.c = 18;
            if (rise_.x < kZPeek - .15) {
                mode_ = Mode::hidden;
                mode_t_ = 0;
                next_peek_ = 7 + 10 * rand01();
                if (returning_ && stolen_ >= 0) {
                    restore_queue_.push_back(stolen_);
                    rip_t_ = .45;
                    say("return", 1);
                }
            }
            break;
        case Mode::taunt:
            // up out of the box, waving her prize
            rise_.target = kZUp + .22;
            rise_.k = 200; rise_.c = 14;
            if (mode_t_ > 2.9) { mode_ = Mode::duck; mode_t_ = 0; returning_ = true; }
            break;
        case Mode::mole:
            // she works the switches from inside, peeking out of the gap to watch
            rise_.target = kZPeek - .12;
            rise_.k = 90; rise_.c = 14;
            break;
        case Mode::great:
            rise_.target = kZHigh;
            rise_.k = 230; rise_.c = 13;
            side_.target = .1 * std::sin(mode_t_ * 6);
            if (mode_t_ > 5.5) { side_.target = 0; mode_ = Mode::duck; mode_t_ = 0; }
            break;
        case Mode::celebrate: {
            stage_t_ += dt;
            auto next = [&]() { ++celebrate_stage_; stage_t_ = 0; };
            switch (celebrate_stage_) {
                case 0:  // spring up, higher than ever
                    if (stage_t_ > .12) {
                        rise_.target = kZHigh; rise_.k = 240; rise_.c = 13;
                        squash_.v += 4.5;
                        emit("switchbox_unlock", 1);
                        emit("stinger_win_switchbox", .9f);
                        say("win", 1);
                        next();
                    }
                    break;
                case 1:  // clap, beaming
                    if (stage_t_ > 1.5) { for (Hand& h : hands_) h.state = HandState::retract; next(); }
                    break;
                case 2:  // a happy wiggle
                    side_.target = .13 * std::sin(stage_t_ * 9);
                    if (stage_t_ > .9) { side_.target = 0; rise_.target = kZUp; sweep_ = true; next(); }
                    break;
                case 3:  // push every switch back down
                    if (stage_t_ > .25 && jobs_.empty() && hands_[0].state == HandState::idle && hands_[1].state == HandState::idle) next();
                    break;
                case 4:  // stretch and yawn
                    if (stage_t_ > 1.3) { next(); }
                    break;
                case 5:  // lie back down into the box
                    rise_.target = kZHidden; rise_.k = 30; rise_.c = 10;
                    if (rise_.x < kZPeek - .2 && stage_t_ > .6) {
                        reset_request_ = true;
                        emit("switchbox_reset", .7f);
                        mode_ = Mode::hidden;
                        mode_t_ = 0;
                        next_peek_ = 4 + 3 * rand01();
                        celebrate_stage_ = 0;
                    }
                    break;
            }
            break;
        }
    }
    // the lid's resting target; her head enforces clearance in pose_out
    {
        double base = 0;
        if (mode_ == Mode::up || mode_ == Mode::celebrate) base = 1;
        else if (mode_ == Mode::peek) base = .2;
        else if (mode_ == Mode::taunt || mode_ == Mode::great) base = 1;
        else if (mode_ == Mode::mole) base = .26;
        else if (mode_ == Mode::hidden && !jobs_.empty()) base = .05;
        lid_.target = base;
    }
}

void Actor::choose_lean(const StageState& st) {
    std::vector<V3> targets;
    for (const Hand& h : hands_)
        if (h.state == HandState::reach || h.state == HandState::press) targets.push_back(h.pos.target);
    if (targets.empty()) {
        lean_.target = mode_ == Mode::up ? .06 : 0;
        if (!(mode_ == Mode::celebrate && celebrate_stage_ == 2)) side_.target = 0;
        slide_.target *= .995;  // drift home slowly
        return;
    }
    double mx = 0;
    for (const V3& t : targets) mx += t.x;
    mx /= static_cast<double>(targets.size());
    slide_.target = std::clamp(mx, -1.5, 1.5);
    side_.target = targets.size() == 1 ? std::clamp((targets[0].x - slide_.target) * .5, -.35, .35) : 0;
    // the smallest forward lean that brings every target within reach
    GirlPose g = st.girl;
    g.root = {slide_.target, kRootY, std::max(rise_.target, rise_.x)};
    g.side = side_.target;
    const double reach = kArmReach * g.scale * .93;
    double best = kMaxLean;
    for (double l = 0; l <= kMaxLean; l += .02) {
        g.lean = l;
        bool ok = true;
        for (const V3& t : targets) {
            const double dl = len(t - shoulder_world(g, 0)), dr = len(t - shoulder_world(g, 1));
            if (std::min(dl, dr) > reach) { ok = false; break; }
        }
        if (ok) { best = l; break; }
    }
    lean_.target = best;
}

void Actor::hands_update(double dt, StageState& st) {
    const bool can_work = (mode_ == Mode::up && rise_.x > kZUp - .35 && bonk_level() < 4) || (mode_ == Mode::celebrate && celebrate_stage_ == 3);
    // a hint: the nearer free hand hovers over the first switch, bobbing to say "this one"
    if (point_t_ > 0 && point_sw_ >= 0 && can_work) {
        const int s = Stage::switch_x(point_sw_) < slide_.x ? 0 : 1;
        Hand& h = hands_[static_cast<size_t>(s)];
        if (h.state == HandState::idle || h.state == HandState::retract) { h.state = HandState::point; h.timer = 0; }
    }
    for (int s = 0; s < 2; ++s) {
        Hand& h = hands_[static_cast<size_t>(s)];
        Hand& o = hands_[static_cast<size_t>(1 - s)];
        h.timer += dt;
        switch (h.state) {
            case HandState::idle: {
                h.pos.target = rest_world(st, s);
                h.reach.target = 0;
                if (!can_work) break;
                // take the waiting switch on this hand's side; stay within one body position of the other hand
                int pick = -1;
                double bestd = 1e9;
                const bool other_busy = o.state == HandState::reach || o.state == HandState::press;
                for (int j : jobs_) {
                    if (claimed_[static_cast<size_t>(j)]) continue;
                    const double x = Stage::switch_x(j);
                    if (other_busy && std::fabs(x - Stage::switch_x(o.job)) > 1.45) continue;
                    if (other_busy && (s == 0) != (x < Stage::switch_x(o.job))) continue;
                    const double pref = (s == 0 ? x : -x) + (other_busy ? 0 : std::fabs(x - slide_.x) * .3);
                    if (!other_busy && o.state == HandState::idle && (s == 0) != (x <= slide_.x + .05)) continue;
                    if (pref < bestd) { bestd = pref; pick = j; }
                }
                if (pick >= 0) {
                    h.state = HandState::reach;
                    h.job = pick;
                    h.timer = 0;
                    claimed_[static_cast<size_t>(pick)] = true;
                }
                break;
            }
            case HandState::reach: {
                const SwitchVis& v = st.sw[static_cast<size_t>(h.job)];
                h.pos.target = Stage::knob(h.job, v.on) + V3{0, .1, .1};
                h.reach.target = 1;
                if ((len(h.pos.x - h.pos.target) < .07 && h.reach.x > .85) || h.timer > .5) {
                    h.timer = 0;
                    if (h.job == steal_sw_) h.state = HandState::grab;
                    else if (h.job == hold_sw_) { h.state = HandState::wait; hold_t_ = 0; }
                    else h.state = HandState::press;
                }
                break;
            }
            case HandState::wait: {
                // the player is holding it: her hand rests on the pointer and she waits... then less patiently
                const V3 top = Stage::knob(h.job, st.sw[static_cast<size_t>(h.job)].on) + V3{0, .02, .17};
                double bob = 0;
                if (hold_phase_ == 1) bob = .025 * std::max(0.0, std::sin(h.timer * 7));                 // patting
                if (hold_phase_ == 2) bob = .04 * std::sin(h.timer * 16);                                   // tugging
                if (hold_phase_ == 3) bob = .06 * std::sin(h.timer * 26);                                   // shaking it
                h.pos.target = top + V3{hold_phase_ >= 2 ? bob * .6 : 0, hold_phase_ >= 2 ? bob : 0, hold_phase_ == 1 ? bob : 0};
                h.reach.target = 1;
                if (hold_sw_ != h.job) {  // let go: she pushes it down at last, huffily
                    h.state = HandState::press;
                    h.timer = 0;
                    if (hold_t_ > 1) { mood_ = 3; mood_t_ = 1.5; say("hold_release", .5); }
                    hold_t_ = 0;
                } else if (hold_t_ > 7) {
                    // she has had enough: this hand lifts the pointer away while the other
                    // dives in and pushes the switch down anyway
                    hold_t_ = 0;
                    mood_ = 8; mood_t_ = 3;
                    emit("switchbox_giggle_boing", .8f, 1.2f);
                    say("hold_force", 1);
                    Hand& o2 = hands_[static_cast<size_t>(1 - s)];
                    if (o2.state == HandState::idle || o2.state == HandState::retract) {
                        o2.state = HandState::press;
                        o2.job = h.job;
                        o2.timer = -.18;  // a beat to get there
                        h.job = -1;
                        h.state = HandState::carry;
                        h.timer = 0;
                        carry_ = true;
                        carry_from_ = h.pos.x;
                        carry_at_ = Stage::knob(o2.job, 1) + V3{0, -.06, .09};
                    } else {
                        h.state = HandState::press;
                        h.timer = .05;
                        forced_ = true;
                    }
                }
                break;
            }
            case HandState::carry: {
                // up and away with the pointer pinched under her palm, held aloft, then dropped
                const double u = ease(h.timer / .35);
                const double sx = s ? 1 : -1;  // up and out to her side, held high like a prize
                h.pos.target = carry_from_ + V3{sx * .42 * u, -.08 * u, .5 * u + .03 * std::sin(h.timer * 9) * u};
                h.reach.target = 1;
                carry_at_ = h.pos.x + V3{0, -.05, -.07};
                if (h.timer > 1.15) {
                    carry_ = false;
                    dropped_ = true;
                    forced_ = true;
                    h.state = HandState::retract;
                    h.timer = 0;
                    emit("switchbox_switch_on_01", .5f, 1.6f);
                }
                break;
            }
            case HandState::grab: {
                // a theft: fingers close on the knob, and the whole switch comes up out of its socket
                SwitchVis& v = st.sw[static_cast<size_t>(h.job)];
                h.reach.target = 1;
                const V3 knob = Stage::knob(h.job, v.on);
                h.pos.target = knob + V3{0, .06, .02 + (h.timer > .14 ? (h.timer - .14) * 2.2 : 0)};
                if (h.timer > .14 && v.sink < 1) {
                    v.sink = 1;
                    v.on = 0;
                    st.held = h.job;
                    st.held_hand = s;
                    stolen_ = h.job;
                    stolen_t_ = 0;
                    steal_sw_ = -1;
                    jobs_.erase(std::remove(jobs_.begin(), jobs_.end(), h.job), jobs_.end());
                    claimed_[static_cast<size_t>(h.job)] = false;
                    emit("switchbox_switch_on_02", 1, .6f);
                    emit("switchbox_giggle_boing", .8f, 1.1f);
                    mood_ = 8; mood_t_ = 2.5;
                    say("steal", 1);
                }
                if (h.timer > .55) {
                    h.state = HandState::retract;
                    h.timer = 0;
                    h.job = -1;
                    linger_ = .5;
                }
                break;
            }
            case HandState::press: {
                SwitchVis& v = st.sw[static_cast<size_t>(h.job)];
                // push it just past centre; the toggle's spring snaps it the rest of the way
                const double u = ease(h.timer / .1);
                if (h.timer > 0) v.on = std::min(v.on, 1 - .58 * u);
                h.pos.target = Stage::knob(h.job, v.on) + V3{0, .09, .03};
                h.reach.target = 1;
                if (h.timer >= .1) {
                    v.on = 0;
                    emit("switchbox_switch_off_0" + std::to_string(1 + static_cast<int>(rand01() * 3)), .9f, static_cast<float>(.95 + .1 * rand01()));
                    jobs_.erase(std::remove(jobs_.begin(), jobs_.end(), h.job), jobs_.end());
                    claimed_[static_cast<size_t>(h.job)] = false;
                    h.state = HandState::retract;
                    h.timer = 0;
                    squash_.v -= 1.2;
                    head_pitch_.v += 2.5;  // a satisfied little nod
                    say("push", .12);
                }
                break;
            }
            case HandState::retract:
                h.pos.target = rest_world(st, s);
                h.reach.target = 0;
                if (h.timer > .22) { h.state = HandState::idle; h.job = -1; }
                break;
            case HandState::clap:
                break;
            case HandState::point: {
                // arm out toward the switch, finger extended, jabbing the air: "that one"
                const V3 sh = shoulder_world(st.girl, s);
                const V3 aim = Stage::knob(point_sw_, 0) + V3{0, 0, .1};
                const double jab = .78 + .1 * std::max(0.0, std::sin(h.timer * 10));
                h.pos.target = sh + norm(aim - sh) * (kArmReach * st.girl.scale * jab);
                h.reach.target = 1;
                if (point_t_ <= 0) { h.state = HandState::retract; h.timer = 0; }
                break;
            }
        }
    }
    // holding: the clock runs while a hand rests on the pointer
    bool waiting = false;
    for (const Hand& h : hands_) waiting = waiting || h.state == HandState::wait;
    if (waiting) {
        hold_t_ += dt;
        const int phase = hold_t_ < 2.4 ? 1 : hold_t_ < 4.6 ? 2 : 3;
        if (phase != hold_phase_) {
            hold_phase_ = phase;
            say(phase == 1 ? "hold_calm" : phase == 2 ? "hold_mad" : "hold_rage", 1);
            if (phase == 3) emit("switchbox_giggle_boing", .6f, .7f);
        }
    } else {
        hold_phase_ = 0;
    }
    // the free hand: flailing in a rage, rubbing or guarding her head when bonked, waving a stolen switch
    const M34 bf = body_frame(st.girl);
    for (int s = 0; s < 2; ++s) {
        Hand& h = hands_[static_cast<size_t>(s)];
        if (h.state != HandState::idle && h.state != HandState::retract) continue;
        const double sx = s ? 1 : -1;
        const int lv = bonk_level();
        if (mode_ == Mode::taunt && s == st.held_hand) {
            h.pos.target = bf.apply({sx * (.28 + .1 * std::sin(mode_t_ * 9)), -.3, 1.32 + .06 * std::sin(mode_t_ * 18)});
            h.reach.target = 1;
        } else if (hold_phase_ == 3) {
            h.pos.target = bf.apply({sx * (.45 + .12 * std::sin(t_ * 17 + s)), -.25, .95 + .2 * std::sin(t_ * 13 + s * 2)});
            h.reach.target = 1;
        } else if (mode_ == Mode::up && lv >= 2) {
            const M34 hf = head_frame(st.girl);
            if (lv == 2) h.pos.target = s == 0 ? hf.apply({-.2 + .06 * std::sin(t_ * 12), -.1, .5}) : rest_world(st, s);  // rubbing the sore spot
            else if (lv == 3 || lv == 5) h.pos.target = hf.apply({sx * .45, -.2, .25});                                        // guarding her head
            else h.pos.target = bf.apply({sx * (.42 + .1 * std::sin(t_ * 15 + s)), -.3, 1.05 + .18 * std::sin(t_ * 11 + s * 3)});  // flailing
            h.reach.target = lv == 2 && s == 1 ? 0 : 1;
        }
    }
    // celebration overrides: clapping, then a big stretch
    const bool clapping = (mode_ == Mode::celebrate && celebrate_stage_ == 1) || (mode_ == Mode::great && mode_t_ > .3 && mode_t_ < 2.6);
    if (clapping || (mode_ == Mode::celebrate && celebrate_stage_ == 4)) {
        const M34 b = body_frame(st.girl);
        for (int s = 0; s < 2; ++s) {
            Hand& h = hands_[static_cast<size_t>(s)];
            const double sx = s ? 1 : -1;
            if (h.job >= 0) { claimed_[static_cast<size_t>(h.job)] = false; h.job = -1; }  // drop the switch; she comes back for it
            h.state = HandState::clap;
            h.reach.target = 1;
            if (clapping) {
                const double ph = (mode_ == Mode::great ? mode_t_ : stage_t_) * 3.4;
                const bool together = std::fmod(ph, 1.0) > .5;
                const double w = together ? .035 : .26;
                h.pos.target = b.apply({sx * w, -.42, .66 + .04 * std::sin(ph * 6.283)});
                if (s == 0 && together != clap_closed_) {
                    clap_closed_ = together;
                    if (together) emit("switchbox_clap", .8f, static_cast<float>(.95 + .1 * rand01()));
                }
            } else {
                h.pos.target = b.apply({sx * .32, -.05, 1.55});
            }
        }
    } else {
        for (Hand& h : hands_) if (h.state == HandState::clap) { h.state = HandState::retract; h.timer = 0; }
    }
}

void Actor::face_update(double dt, StageState& st) {
    Face f;
    f.blush = 1;
    // where she looks: her current switch, else the cursor, else wandering
    const V3 hc = head_center(st.girl);
    V3 look = cursor_;
    bool have = cursor_valid_;
    for (const Hand& h : hands_)
        if (h.state == HandState::reach || h.state == HandState::press) { look = Stage::knob(h.job, 1); have = true; }
        else if (h.state == HandState::point && point_sw_ >= 0) {
            // glance between the switch and the player while pointing
            look = std::fmod(t_, 1.2) < .7 ? Stage::knob(point_sw_, 0) : hc + V3{0, -3, 0};
            have = true;
        }
    if (have) {
        f.look_x = static_cast<int>(std::lround(std::clamp((look.x - hc.x) / .45, -2.0, 2.0)));
        f.look_y = look.z < hc.z - .55 ? -1 : 0;
        head_yaw_.target = std::clamp((look.x - hc.x) * -.12, -.3, .3);
        head_pitch_.target = look.z < hc.z - .55 ? .05 : -.2;
    } else {
        f.look_x = static_cast<int>(std::lround(2 * std::sin(t_ * .9) * std::sin(t_ * .37)));
        head_yaw_.target = .15 * std::sin(t_ * .5);
        head_pitch_.target = -.22;
    }
    // expression by situation
    switch (mood_) {
        case 1: f.eyes = Eyes::wide; f.brow = Brow::worried; f.mouth = Mouth::wavy; break;  // a lamp lit: nervous
        case 2: f.eyes = Eyes::half; f.brow = Brow::raised; f.mouth = Mouth::cat; break;    // wrong: smug
        case 3: f.eyes = Eyes::angry; f.brow = Brow::angry; f.mouth = Mouth::pout; break;   // knocked on: cross
        case 4:  // scolding: shouting, head shaking
            f.eyes = std::fmod(t_, .9) < .45 ? Eyes::angry : Eyes::squeeze;
            f.brow = Brow::angry;
            f.mouth = std::fmod(t_, .3) < .18 ? Mouth::shout : Mouth::frown;
            f.blush = 2;
            head_yaw_.target += .22 * std::sin(t_ * 13);
            break;
        case 5: f.eyes = Eyes::half; f.brow = Brow::worried; f.mouth = std::fmod(t_, .4) < .25 ? Mouth::shout : Mouth::frown; break;  // you forgot?!
        case 7: f.eyes = Eyes::half; f.brow = Brow::worried; f.mouth = Mouth::pout; f.blush = 2; break;  // sulking
        case 6: f.eyes = Eyes::half; f.brow = Brow::raised; f.mouth = std::fmod(t_, .4) < .25 ? Mouth::o_small : Mouth::pout; break;  // fine, this one
        default: f.eyes = Eyes::open; f.brow = Brow::neutral; f.mouth = mode_ == Mode::up ? Mouth::pout : Mouth::smile; break;
    }
    if (mode_ == Mode::up && (hands_[0].state == HandState::reach || hands_[1].state == HandState::reach) && mood_ == 0) {
        f.brow = Brow::angry;
        f.mouth = Mouth::pout;
    }
    if (mode_ == Mode::celebrate) {
        switch (celebrate_stage_) {
            case 0: f.eyes = Eyes::wide; f.mouth = Mouth::o_small; break;
            case 1: f.eyes = Eyes::sparkle; f.mouth = Mouth::open_smile; f.blush = 2; f.brow = Brow::raised; break;
            case 2: f.eyes = Eyes::happy; f.mouth = Mouth::open_smile; f.blush = 2; break;
            case 3: f.eyes = Eyes::happy; f.mouth = Mouth::cat; break;
            case 4: f.eyes = Eyes::blink; f.mouth = stage_t_ > .3 && stage_t_ < 1.1 ? Mouth::o_small : Mouth::smile; f.brow = Brow::worried; break;
            default: f.eyes = Eyes::blink; f.mouth = Mouth::smile; break;
        }
    }
    // the bonk ladder, from mildly cross to completely overloaded
    switch (mode_ == Mode::up || mode_ == Mode::peek ? bonk_level() : 0) {
        case 2: f.eyes = Eyes::angry; f.brow = Brow::angry; f.mouth = Mouth::pout; break;
        case 3: f.eyes = Eyes::teary; f.brow = Brow::worried; f.mouth = Mouth::wavy; f.blush = 2; break;
        case 4:
            f.eyes = std::fmod(t_, .7) < .4 ? Eyes::angry : Eyes::squeeze;
            f.brow = Brow::angry;
            f.mouth = std::fmod(t_, .26) < .16 ? Mouth::shout : Mouth::wavy;
            f.blush = 2;
            break;
        case 5: f.eyes = Eyes::swirl; f.brow = Brow::worried; f.mouth = Mouth::wavy; f.blush = 2; break;
        default: break;
    }
    // her hand on the player's pointer: patient, then cross, then a full anime meltdown
    switch (hold_phase_) {
        case 1: f.eyes = Eyes::half; f.brow = Brow::raised; f.mouth = Mouth::flat; f.look_y = -1; f.look_x = 0; break;
        case 2: f.eyes = Eyes::angry; f.brow = Brow::angry; f.mouth = std::fmod(t_, .5) < .3 ? Mouth::pout : Mouth::frown; break;
        case 3: {
            const double ph = std::fmod(t_, 1.2);
            f.eyes = ph < .4 ? Eyes::squeeze : ph < .8 ? Eyes::teary : Eyes::swirl;
            f.brow = Brow::angry;
            f.mouth = std::fmod(t_, .22) < .13 ? Mouth::shout : Mouth::wavy;
            f.blush = 2;
            head_yaw_.target += .2 * std::sin(t_ * 15);
            break;
        }
        default: break;
    }
    if (mood_ == 8) { f.eyes = std::fmod(t_, .8) < .5 ? Eyes::happy : Eyes::sparkle; f.mouth = Mouth::bleh; f.brow = Brow::raised; f.blush = 1; }  // mischief
    if (mode_ == Mode::taunt) {
        f.eyes = std::fmod(t_, .9) < .55 ? Eyes::happy : Eyes::sparkle;
        f.mouth = std::fmod(t_, .9) < .55 ? Mouth::grin_tongue : Mouth::bleh;
        f.brow = Brow::raised;
        f.blush = 1;
    }
    if (mode_ == Mode::mole) {
        f.eyes = Eyes::wide;
        f.brow = Brow::raised;
        if (mole_look_ >= 0) f.look_x = static_cast<int>(std::lround(std::clamp((Stage::switch_x(mole_look_) - hc.x) / .45, -2.0, 2.0)));
        f.look_y = -1;
    }
    if (mode_ == Mode::great) {
        f.eyes = std::fmod(mode_t_, 1.6) < 1.1 ? Eyes::sparkle : Eyes::happy;
        f.mouth = Mouth::open_smile;
        f.brow = Brow::raised;
        f.blush = 2;
        head_yaw_.target = 0;
        head_pitch_.target = -.3;
    }
    if (eep_ > 0) {
        f.eyes = Eyes::squeeze;
        f.mouth = bonk_level() >= 4 ? Mouth::shout : Mouth::wavy;
        f.blush = 2;
        f.brow = Brow::worried;
    }
    // blinking, only over open eyes
    blink_t_ -= dt;
    if (blink_t_ <= 0) { blink_left_ = .11; blink_t_ = 1.8 + 3.5 * rand01(); }
    if (blink_left_ > 0) {
        blink_left_ -= dt;
        if ((f.eyes == Eyes::open || f.eyes == Eyes::half || f.eyes == Eyes::wide || f.eyes == Eyes::angry) && hold_phase_ < 2) f.eyes = Eyes::blink;
    }
    face_ = f;
}

void Actor::fx_update(double dt, StageState& st) {
    Fx want;
    const int lv = mode_ == Mode::up || mode_ == Mode::peek ? bonk_level() : 0;
    want.anger = std::max({mood_ == 4 ? 1.0 : 0.0, mood_ == 3 ? .7 : 0.0, hold_phase_ >= 2 ? 1.0 : 0.0, lv == 2 ? .7 : lv == 4 ? 1.0 : 0.0});
    want.steam = std::max({hold_phase_ == 3 ? 1.0 : 0.0, lv >= 4 ? 1.0 : 0.0, mood_ == 4 ? .6 : 0.0});
    want.sweat = std::max({lv == 3 || lv == 5 ? 1.0 : 0.0, mood_ == 1 ? .8 : 0.0, mood_ == 5 ? .8 : 0.0, hold_phase_ == 1 && hold_t_ > 1.2 ? .7 : 0.0});
    want.hearts = mode_ == Mode::great ? 1 : 0;
    want.sparkle = std::max({mode_ == Mode::great ? 1.0 : 0.0, mode_ == Mode::taunt ? 1.0 : 0.0,
                             mode_ == Mode::celebrate && celebrate_stage_ <= 2 ? 1.0 : 0.0});
    auto ease_to = [&](double& v, double w) { v += (w - v) * std::min(1.0, dt * (w > v ? 10 : 4)); };
    ease_to(fx_.anger, want.anger);
    ease_to(fx_.steam, want.steam);
    ease_to(fx_.sweat, want.sweat);
    ease_to(fx_.hearts, want.hearts);
    ease_to(fx_.sparkle, want.sparkle);
    st.fx = fx_;
}

void Actor::pose_out(StageState& st) {
    GirlPose& g = st.girl;
    g.root = {slide_.x, kRootY, rise_.x};
    g.lean = lean_.x + (mode_ == Mode::celebrate && celebrate_stage_ >= 4 ? -.25 : 0);
    g.side = side_.x;
    g.squash = std::clamp(squash_.x, .72, 1.3);
    g.head_yaw = head_yaw_.x;
    g.head_pitch = head_pitch_.x;
    g.head_roll = head_roll_.x + side_.x * -.4;
    for (int s = 0; s < 2; ++s) {
        g.hand[static_cast<size_t>(s)] = hands_[static_cast<size_t>(s)].pos.x;
        g.reach[static_cast<size_t>(s)] = std::clamp(hands_[static_cast<size_t>(s)].reach.x, 0.0, 1.0);
        const HandState hs = hands_[static_cast<size_t>(s)].state;
        g.fist[static_cast<size_t>(s)] = hs == HandState::press || hs == HandState::grab || (st.held >= 0 && s == st.held_hand) ? 1 : 0;
        g.point[static_cast<size_t>(s)] = hands_[static_cast<size_t>(s)].state == HandState::point ? 1 : 0;
    }
    g.face = face_;
    g.hair_lag = hair_.x;
    g.ahoge = ahoge_.x;
    // her head (with hair) lifts the lid: never let it pass through her
    const double need = lid_clearance(head_center(g), g.scale);
    if (lid_.x < need) { lid_.x = need; lid_.v = std::max(lid_.v, 0.0); }
    st.lid = std::clamp(lid_.x, 0.0, 1.08);
    st.girl_visible = rise_.x > kZHidden + .05 || st.lid > .01;
    if (stolen_ >= 0 && restore_cur_ != stolen_ && std::find(restore_queue_.begin(), restore_queue_.end(), stolen_) == restore_queue_.end()) st.held = stolen_;
}

// Yanking switches down from inside the box, and pushing them back up.
void Actor::rearrange(double dt, StageState& st) {
    if (restore_pending_) {
        restore_pending_ = false;
        for (int i = 0; i < kSwitches; ++i)
            if (st.sw[static_cast<size_t>(i)].sink > 0 && i != rip_cur_) restore_queue_.push_back(i);
        if (rip_cur_ >= 0) { restore_queue_.push_front(rip_cur_); rip_cur_ = -1; }
        rip_t_ = .5;
    }
    rip_t_ -= dt;
    // a yank: the knob rattles, then the switch drops through its socket
    if (rip_cur_ >= 0) {
        rip_anim_ += dt;
        SwitchVis& v = st.sw[static_cast<size_t>(rip_cur_)];
        if (rip_anim_ < .16) v.wiggle = 1;
        else {
            if (v.sink == 0) emit("switchbox_switch_off_0" + std::to_string(1 + static_cast<int>(rand01() * 3)), 1, .55f);
            v.sink = std::min(1.0, (rip_anim_ - .16) / .14);
            v.on = 0;
            if (v.sink >= 1) rip_cur_ = -1;
        }
    } else if (!rip_queue_.empty() && rip_t_ <= 0 && rise_.x < kZPeek - .1) {
        rip_cur_ = rip_queue_.front();
        rip_queue_.pop_front();
        rip_anim_ = 0;
        rip_t_ = .38;
        if (rip_queue_.empty()) say("rip_out", 1);
    }
    // a return: the switch pops up out of its hole with a little overshoot
    if (restore_cur_ >= 0) {
        restore_anim_ += dt;
        SwitchVis& v = st.sw[static_cast<size_t>(restore_cur_)];
        const double u = std::min(1.0, restore_anim_ / .22);
        v.sink = 1 - u;
        if (u >= 1) {
            v.sink = 0;
            v.wiggle = .8;
            if (restore_cur_ == stolen_) { stolen_ = -1; returning_ = false; stolen_t_ = 0; }
            restore_cur_ = -1;
        }
    } else if (!restore_queue_.empty() && rip_t_ <= 0) {
        restore_cur_ = restore_queue_.front();
        restore_queue_.pop_front();
        restore_anim_ = 0;
        rip_t_ = .34;
        if (restore_cur_ == stolen_) st.held = -1;
        emit("switchbox_switch_on_0" + std::to_string(1 + static_cast<int>(rand01() * 3)), .9f, 1.25f);
        if (restore_queue_.empty() && restore_cur_ != stolen_) say("restore", 1);
    }
}

void Actor::update(double dt, StageState& st) {
    dt = std::clamp(dt, 0.0, .1);
    t_ += dt;
    if (sweep_) {
        sweep_ = false;
        for (int i = 0; i < kSwitches; ++i)
            if (st.sw[static_cast<size_t>(i)].on > .01 && st.sw[static_cast<size_t>(i)].sink <= 0 &&
                std::find(jobs_.begin(), jobs_.end(), i) == jobs_.end())
                jobs_.push_back(i);
    }
    rearrange(dt, st);
    brain(dt, st);
    hands_update(dt, st);
    choose_lean(st);
    face_update(dt, st);
    fx_update(dt, st);
    head_roll_.target = 0;
    // rage shows in the body: trembling while she fumes, stomping when bonked too much
    if (hold_phase_ == 3 || bonk_level() >= 4) squash_.v += std::sin(t_ * 40) * 1.5;
    for (double left = dt; left > 1e-9; left -= kStep) {
        const double h = std::min(kStep, left);
        for (Spring* s : {&rise_, &slide_, &lean_, &side_, &squash_, &head_yaw_, &head_pitch_, &head_roll_, &lid_}) s->step(h);
        for (Hand& hd : hands_) { hd.pos.step(h); hd.reach.step(h); }
        hair_.step(h);
        ahoge_.step(h);
    }
    if (lid_.x < 0) { lid_.x = 0; lid_.v = std::fabs(lid_.v) * .3; }  // the lid bounces on the rim
    pose_out(st);
    // secondary motion from the head's movement
    const V3 hc = head_center(st.girl);
    if (dt > 0) head_vel_ = (hc - head_prev_) * (1 / dt);
    head_prev_ = hc;
    hair_.target = V3{-head_vel_.x, -head_vel_.y, 0} * .045;
    ahoge_.target = std::clamp(-head_vel_.x * .5 + head_vel_.z * .1, -1.2, 1.2);
}

}  // namespace sbx
