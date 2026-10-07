#include "vactor.hpp"

#include <algorithm>
#include <cmath>

namespace fp {

namespace {
constexpr double kStep = 1.0 / 240;
VFace face(VEyes e, VBrow b, VMouth m) { VFace f; f.eyes = e; f.brow = b; f.mouth = m; return f; }
}  // namespace

VillainActor::VillainActor(std::uint64_t seed) : script_(seed), rng_(seed * 0x2545F4914F6CDD1DULL | 1) {
    lean_.k = 60; lean_.c = 12;
    side_.k = 50; side_.c = 10;
    squash_.snap(1); squash_.k = 220; squash_.c = 12;
    head_yaw_.k = 60; head_yaw_.c = 11;
    head_pitch_.k = 60; head_pitch_.c = 11;
    head_roll_.k = 60; head_roll_.c = 10;
    rise_.k = 50; rise_.c = 11;
    glow_.k = 60; glow_.c = 14;  // drives the pocket watch in and out
    for (int s = 0; s < 2; ++s) {
        hand_[static_cast<size_t>(s)].k = 140; hand_[static_cast<size_t>(s)].c = 20;
        palm_[static_cast<size_t>(s)].k = 120; palm_[static_cast<size_t>(s)].c = 20;
        palm_[static_cast<size_t>(s)].x = palm_[static_cast<size_t>(s)].target = {0, -1, 0};
        reach_[static_cast<size_t>(s)].k = 90; reach_[static_cast<size_t>(s)].c = 18;
        curl_[static_cast<size_t>(s)].k = 120; curl_[static_cast<size_t>(s)].c = 20;
        point_[static_cast<size_t>(s)].k = 120; point_[static_cast<size_t>(s)].c = 20;
    }
    face_ = face(VEyes::half, VBrow::flat, VMouth::smirk);
}

double VillainActor::rand01() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 7; rng_ ^= rng_ << 17;
    return static_cast<double>(rng_ >> 11) * (1.0 / 9007199254740992.0);
}

void VillainActor::say(const std::string& text, Gesture g, VFace f, double hold) { speech.push_back({text, g, f, hold}); }

void VillainActor::new_game() {
    speech.clear();
    target_ = script_.target();
    const std::vector<std::string> lines = script_.oration(target_);
    phase_ = Phase::oration;
    phase_t_ = 0;
    won_ = lost_ = false;
    laughing_ = apocalypse_ = false;
    best_quality_ = -1;
    stall_ = 0;
    turns_left_ = kTurns;
    say(lines[0], Gesture::present, face(VEyes::glint, VBrow::arched, VMouth::grin), .5);
    say(lines[1], Gesture::decree, face(VEyes::glint, VBrow::furious, VMouth::shout), 1.0);
    say(lines[2], Gesture::offer, face(VEyes::menace, VBrow::quizzical, VMouth::smirk), .6);
    cues.push_back({VCue::sound, "fp_console_boot", .8f, 1});
}

void VillainActor::submitted(Score s, int turns_left, bool won, bool lost) {
    last_ = s;
    turns_left_ = turns_left;
    won_ = won;
    lost_ = lost;
    phase_ = Phase::inspect;
    phase_t_ = 0;
    speech.clear();
    set_gesture(Gesture::inspect);
}

bool VillainActor::take_reveal_pins() { const bool r = reveal_pins_; reveal_pins_ = false; return r; }
bool VillainActor::take_reveal_secret() { const bool r = reveal_secret_; reveal_secret_ = false; return r; }
bool VillainActor::take_apocalypse() { const bool r = apocalypse_; apocalypse_ = false; return r; }

void VillainActor::poked() {
    if (phase_ != Phase::idle || !speech.empty()) return;
    head_roll_.v += 3;
    squash_.v -= 2;
    say(script_.poke(), Gesture::tie, face(VEyes::half, VBrow::arched, VMouth::sneer), .4);
    cues.push_back({VCue::sound, "fp_voice_hm", .7f, 1});
}

void VillainActor::placed(int) {
    // he notices; sometimes a raised brow
    if (phase_ == Phase::idle && rand01() < .25) { face_ = face(VEyes::squint, VBrow::quizzical, VMouth::flat); face_hold_ = 1.2; }
}

// Where hands, body and head should be for the current gesture.
void VillainActor::pose_targets(const LairState& st) {
    const VillainPose& p = st.villain;
    const M34 b = villain_body(p);
    const M34 h = villain_head(p);
    const double gt = gesture_t_;
    auto body = [&](V3 v) { return b.apply(v); };
    auto head = [&](V3 v) { return h.apply(v); };
    auto both = [&](V3 l, V3 r, double reach, double curl, double point, V3 pl, V3 pr) {
        hand_[0].target = l; hand_[1].target = r;
        reach_[0].target = reach_[1].target = reach;
        curl_[0].target = curl_[1].target = curl;
        point_[0].target = point_[1].target = point;
        palm_[0].target = pl; palm_[1].target = pr;
    };
    double lean = .05, side = 0, hp = 0, hy = 0, rise = 0;
    watch_out_ = 0;
    switch (gesture_) {
        case Gesture::steeple:  // the contemplative gentleman: fingertips together before the chin
            both(head({-.15, -.55, -.72}), head({.15, -.55, -.72}), 1, 0, 0, norm(V3{.6, -.1, 1}), norm(V3{-.6, -.1, 1}));
            lean = .1; hp = .1 + .02 * std::sin(t_ * .9);
            break;
        case Gesture::rest:
            both(body(p.rest[0]), body(p.rest[1]), 0, .3, 0, {0, -1, 0}, {0, -1, 0});
            break;
        case Gesture::present:  // "behold": one palm up and out, the other hand at his heart
            hand_[1].target = body({.85, -.75, 1.12 + .03 * std::sin(gt * 2)});
            hand_[0].target = body({-.06, -.4, .95});
            reach_[0].target = reach_[1].target = 1;
            curl_[0].target = .2; curl_[1].target = 0; point_[0].target = point_[1].target = 0;
            palm_[1].target = norm(V3{.7, -.5, .3}); palm_[0].target = {1, 0, 0};
            lean = 0; hp = -.12; hy = -.1;
            break;
        case Gesture::decree:  // an index finger raised: a pronouncement
            hand_[1].target = body({.42, -.6, 1.5});
            hand_[0].target = body({-.06, -.4, .95});
            reach_[0].target = reach_[1].target = 1;
            curl_[0].target = .2; curl_[1].target = .85; point_[1].target = 1; point_[0].target = 0;
            palm_[1].target = {0, 0, 1}; palm_[0].target = {1, 0, 0};
            lean = 0; hp = -.15; rise = .04;
            break;
        case Gesture::offer:  // "after you": an open palm toward the console
            hand_[1].target = body({.3, -1.15, 1.02});
            hand_[0].target = body(p.rest[0]);
            reach_[1].target = 1; reach_[0].target = 0;
            curl_[1].target = 0; point_[1].target = 0; point_[0].target = 0;
            palm_[1].target = norm(V3{.2, -1, -.25});
            lean = .12; hp = .05;
            break;
        case Gesture::finger:  // a polite, admonishing finger beside his face, with the faintest shake
            hand_[1].target = body({.4 + .025 * std::sin(gt * 7), -.5, 1.4});
            hand_[0].target = body(p.rest[0]);
            reach_[1].target = 1; reach_[0].target = 0;
            curl_[1].target = .85; point_[1].target = 1;
            palm_[1].target = norm(V3{.06 * std::sin(gt * 7), -.2, 1});
            lean = .05; hy = -.06;
            break;
        case Gesture::inspect:  // leaning in to examine the guess
            both(body({-.5, -.8, .74}), body({.5, -.8, .74}), 1, .1, 0, {.3, -1, 0}, {-.3, -1, 0});
            lean = .18; hp = .14 + .03 * std::sin(gt * 2);
            break;
        case Gesture::grip: {  // both hands planted on the desk edge: cold, controlled anger
            const bool down = gt > .3;
            both(body({-.42, -.95, down ? .72 : .95}), body({.42, -.95, down ? .72 : .95}), 1, down ? .7 : .2, 0, {0, -1, -.3}, {0, -1, -.3});
            if (down && !slam_hit_) { slam_hit_ = true; cues.push_back({VCue::sound, "fp_desk", .8f, 1}); cues.push_back({VCue::shake, "", .4f, 1}); }
            lean = down ? .16 : .05; hp = .02;
            break;
        }
        case Gesture::chuckle:  // a gloved hand to his mouth, shoulders shaking quietly
            hand_[1].target = head({.05, -.42, -.46});
            hand_[0].target = body({-.06, -.4, .95});
            reach_[0].target = reach_[1].target = 1;
            curl_[1].target = .5; curl_[0].target = .2; point_[0].target = point_[1].target = 0;
            palm_[1].target = {-1, -.2, .5}; palm_[0].target = {1, 0, 0};
            lean = .05; hp = .1;
            squash_.target = 1 + .018 * std::sin(t_ * 22);
            break;
        case Gesture::bow:  // a formal bow, hand to his chest
            hand_[0].target = body({-.05, -.4, .95});
            hand_[1].target = body(p.rest[1]);
            reach_[0].target = 1; reach_[1].target = 0;
            curl_[0].target = .2; point_[0].target = point_[1].target = 0;
            palm_[0].target = {1, 0, 0};
            lean = .3; hp = .15;
            break;
        case Gesture::golf_clap: {  // restrained applause
            const bool together = std::fmod(gt, .8) > .45;
            // palms meet at the hand's own width, never through each other
            both(body({together ? -.075 : -.26, -.7, 1.1}), body({together ? .075 : .26, -.7, 1.1}), 1, 0, 0, {0, -.25, 1}, {0, -.25, 1});
            if (together && gt - clap_last_ > .5) { clap_last_ = gt; cues.push_back({VCue::sound, "fp_clap", .5f, 1}); }
            lean = .03;
            break;
        }
        case Gesture::chin:  // thumb and finger at his chin, considering
            hand_[1].target = head({.03, -.4, -.5 - .015 * std::sin(gt * 2)});
            hand_[0].target = body(p.rest[0]);
            reach_[1].target = 1; reach_[0].target = 0;
            curl_[1].target = .6; point_[1].target = .4;
            palm_[1].target = {-1, 0, .4};
            lean = .06; hp = -.06; hy = .1;
            break;
        case Gesture::tie:  // straightening his bow tie
            both(body({-.1, -.42, 1.04}), body({.1, -.42, 1.04}), 1, .5, 0, {1, -.2, 0}, {-1, -.2, 0});
            lean = 0; hp = .18; hy = .03 * std::sin(gt * 4);
            break;
        case Gesture::watch:  // consulting his pocket watch
            hand_[0].target = body({-.12, -.75, 1.0});
            hand_[1].target = body({.08, -.72, 1.04});
            reach_[0].target = reach_[1].target = 1;
            curl_[0].target = .3; curl_[1].target = .5; point_[0].target = point_[1].target = 0;
            palm_[0].target = {.3, -.3, 1}; palm_[1].target = {-.5, -.3, .8};
            watch_out_ = 1;
            lean = .1; hp = .3;
            break;
        case Gesture::pinch:  // pinching the bridge of his nose
            hand_[1].target = head({.02, -.46, .05});
            hand_[0].target = body(p.rest[0]);
            reach_[1].target = 1; reach_[0].target = 0;
            curl_[1].target = .6; point_[1].target = .3;
            palm_[1].target = {-.4, 0, 1};
            lean = .08; hp = .2;
            break;
        case Gesture::triumph: {  // he rises, arms flung high and wide, head thrown back, laughing
            const double shake = .04 * std::sin(t_ * 24);
            both(body({-.9, -.45, 1.95 + shake}), body({.9, -.45, 1.95 - shake}), 1, 0, 0, norm(V3{-.5, -.3, 1}), norm(V3{.5, -.3, 1}));
            lean = -.22; hp = -.42; rise = .32;
            squash_.target = 1 + .03 * std::sin(t_ * 26);
            break;
        }
        case Gesture::recoil:  // affronted, hands raised slightly
            both(body({-.45, -.55, 1.15}), body({.45, -.55, 1.15}), 1, .1, 0, {0, -1, .6}, {0, -1, .6});
            lean = -.12; hp = -.1;
            break;
    }
    lean_.target = lean;
    side_.target = side;
    head_pitch_.target = hp;
    rise_.target = rise;
    // eyes and head follow the peg the player is holding or the cursor over the console
    head_yaw_.target = hy + (watch_valid_ ? std::clamp(watch_.x * -.06, -.18, .18) : 0);
}

void VillainActor::face_update(double dt, LairState& st) {
    VFace f = face_;
    face_hold_ = std::max(0.0, face_hold_ - dt);
    if (face_hold_ <= 0 && phase_ == Phase::idle && speech.empty()) {
        // the contemplative default: half-lidded, sometimes closed in meditation
        const bool meditating = std::fmod(t_, 17) > 12;
        f = face(meditating ? VEyes::closed : VEyes::half, VBrow::flat, turns_left_ <= 3 ? VMouth::grin : VMouth::smirk);
    }
    if (watch_valid_ && f.eyes != VEyes::closed && f.eyes != VEyes::laugh) {
        const V3 hc = villain_head_center(st.villain);
        f.look_x = static_cast<int>(std::lround(std::clamp((watch_.x - hc.x) / .7, -2.0, 2.0)));
        f.look_y = -1;
    }
    // talking: the jaw works between his expression and an open mouth
    if (talking_ && std::fmod(t_ * 7.5, 1.0) < .5 && f.mouth != VMouth::laugh) f.mouth = f.mouth == VMouth::shout ? VMouth::grimace : VMouth::sneer;
    blink_t_ -= dt;
    if (blink_t_ <= 0) { blink_left_ = .12; blink_t_ = 2.5 + 4 * rand01(); }
    if (blink_left_ > 0) { blink_left_ -= dt; if (f.eyes == VEyes::menace || f.eyes == VEyes::half || f.eyes == VEyes::squint) f.eyes = VEyes::closed; }
    st.villain.face = f;
}

void VillainActor::update(double dt, LairState& st) {
    dt = std::clamp(dt, 0.0, .1);
    t_ += dt;
    phase_t_ += dt;
    gesture_t_ += dt;
    // the head of the speech queue sets his gesture and expression
    if (!speech.empty()) {
        set_gesture(speech.front().gesture);
        face_ = speech.front().face;
        face_hold_ = .3;
    }
    switch (phase_) {
        case Phase::idle:
            if (speech.empty()) {
                // between guesses: steepled fingers, with the occasional flourish and muttered threat
                idle_t_ -= dt;
                if (gesture_t_ > 5 && gesture_ != Gesture::steeple) set_gesture(Gesture::steeple);
                if (idle_t_ <= 0) {
                    idle_t_ = 9 + 9 * rand01();
                    const double r = rand01();
                    if (r < .3) set_gesture(Gesture::chin);
                    else if (r < .5) set_gesture(Gesture::tie);
                    else if (r < .65 || turns_left_ <= 3) set_gesture(Gesture::watch);
                    else if (rand01() < .5) say(script_.stakes(target_), Gesture::decree, face(VEyes::glint, VBrow::arched, VMouth::smirk), .4);
                    else say(script_.muse(target_), turns_left_ <= 3 ? Gesture::watch : Gesture::finger, face(VEyes::menace, VBrow::arched, VMouth::smirk), .4);
                }
            }
            break;
        case Phase::oration:
            if (speech.empty()) { phase_ = Phase::idle; set_gesture(Gesture::steeple); idle_t_ = 8; }
            break;
        case Phase::inspect:
            // a dramatic pause over the pegs; then the pins light
            face_ = face(VEyes::squint, VBrow::quizzical, VMouth::purse);
            if (phase_t_ > 1.0) {
                reveal_pins_ = true;
                phase_ = Phase::verdict;
                phase_t_ = 0;
            }
            break;
        case Phase::verdict:
            if (phase_t_ < .7) break;  // let the pins light first
            if (won_) {
                phase_ = Phase::defeat;
                phase_t_ = 0;
                reveal_secret_ = true;
                say("...I beg your pardon?", Gesture::recoil, face(VEyes::wide, VBrow::worried, VMouth::gasp), .4);
                // composure lost: a melodramatic "curses, foiled again", then a grudging bow
                const std::vector<std::string> f = script_.foiled(target_);
                say(f[0], Gesture::grip, face(VEyes::furious, VBrow::furious, VMouth::shout), .6);
                say(f[1], Gesture::pinch, face(VEyes::closed, VBrow::worried, VMouth::grimace), .7);
                say(f[2], Gesture::decree, face(VEyes::glint, VBrow::furious, VMouth::shout), .8);
                say("...Well played.", Gesture::golf_clap, face(VEyes::half, VBrow::flat, VMouth::flat), .6);
                cues.push_back({VCue::sound, "fp_desk", .6f, .8f});
            } else if (lost_) {
                phase_ = Phase::triumph;
                phase_t_ = 0;
                reveal_secret_ = true;
                // the stakes made good, then a maniacal laugh as the lair comes down around him
                say(script_.lose(target_), Gesture::present, face(VEyes::glint, VBrow::arched, VMouth::grin), .6);
                say("Ha. Ha ha. HA HA HA HA HA HA!", Gesture::triumph, face(VEyes::laugh, VBrow::arched, VMouth::laugh), 2.2);
                say(script_.gloat(), Gesture::triumph, face(VEyes::glint, VBrow::furious, VMouth::laugh), 2.5);
            } else {
                phase_ = Phase::idle;
                Gesture g = Gesture::finger;
                VFace f = face(VEyes::menace, VBrow::arched, VMouth::smirk);
                if (last_.exact == 0 && last_.near == 0) { g = Gesture::chuckle; f = face(VEyes::laugh, VBrow::arched, VMouth::laugh); cues.push_back({VCue::sound, "fp_chuckle", .7f, 1}); }
                else if (last_.exact == 3) { g = Gesture::tie; f = face(VEyes::twitch, VBrow::worried, VMouth::grimace); }
                else if (last_.exact >= 2) { g = Gesture::chin; f = face(VEyes::squint, VBrow::worried, VMouth::flat); }
                else if (last_.exact == 0) { g = Gesture::offer; f = face(VEyes::glint, VBrow::arched, VMouth::grin); }
                // no better than their best so far? a jab, sharper the longer it goes on
                std::string v = script_.verdict(last_, turns_left_);
                const int quality = last_.exact * 3 + last_.near;
                if (quality <= best_quality_) v += " " + script_.jab(++stall_);
                else { best_quality_ = quality; stall_ = 0; }
                say(v, g, f, .7);
                // now and then, the stakes, so they are never forgotten
                if (last_.exact <= 1 && turns_left_ > 3 && rand01() < .35)
                    say(script_.stakes(target_), Gesture::decree, face(VEyes::glint, VBrow::arched, VMouth::smirk), .4);
                if (last_.exact == 3) say(script_.nervous(), Gesture::pinch, face(VEyes::closed, VBrow::worried, VMouth::grimace), .4);
                if (turns_left_ <= 3 && turns_left_ > 0) say(script_.taunt_low(turns_left_), Gesture::watch, face(VEyes::glint, VBrow::arched, VMouth::grin), .4);
                idle_t_ = 10;
            }
            break;
        case Phase::triumph:
            // the laugh begins: so does the end of everything
            if (!speech.empty() && speech.front().gesture == Gesture::triumph && !laughing_) {
                laughing_ = true;
                apocalypse_ = true;
                cues.push_back({VCue::sound, "fp_laugh", 1, 1});
            }
            if (laughing_) { set_gesture(Gesture::triumph); face_ = face(VEyes::laugh, VBrow::arched, VMouth::laugh); }
            break;
        case Phase::defeat:
            if (speech.empty() && phase_t_ > 1) {
                phase_ = Phase::idle;
                set_gesture(Gesture::steeple);
            }
            break;
    }
    if (speech.empty() && phase_ == Phase::idle && face_hold_ <= 0) face_ = face(VEyes::half, VBrow::flat, VMouth::smirk);
    squash_.target = 1;
    pose_targets(st);
    face_update(dt, st);
    glow_.target = watch_out_;
    for (double left = dt; left > 1e-9; left -= kStep) {
        const double h = std::min(kStep, left);
        for (Spring* s : {&lean_, &side_, &squash_, &head_yaw_, &head_pitch_, &head_roll_, &rise_, &glow_}) s->step(h);
        for (int s = 0; s < 2; ++s) {
            hand_[static_cast<size_t>(s)].step(h);
            palm_[static_cast<size_t>(s)].step(h);
            reach_[static_cast<size_t>(s)].step(h);
            curl_[static_cast<size_t>(s)].step(h);
            point_[static_cast<size_t>(s)].step(h);
        }
    }
    head_roll_.target = 0;
    VillainPose& p = st.villain;
    p.lean = lean_.x;
    p.side = side_.x;
    p.squash = std::clamp(squash_.x, .8, 1.2);
    p.breathe = .5 + .5 * std::sin(t_ * 1.3);
    p.root = {0, 1.0, -.08 + rise_.x};
    p.head_yaw = head_yaw_.x;
    p.head_pitch = head_pitch_.x;
    p.head_roll = head_roll_.x;
    for (int s = 0; s < 2; ++s) {
        const size_t i = static_cast<size_t>(s);
        p.hand[i] = hand_[i].x;
        p.reach[i] = std::clamp(reach_[i].x, 0.0, 1.0);
        p.curl[i] = std::clamp(curl_[i].x, 0.0, 1.0);
        p.point[i] = std::clamp(point_[i].x, 0.0, 1.0);
        p.palm[i] = norm(palm_[i].x);
    }
    p.cape_sway = std::clamp(lean_.v * .5, -1.0, 1.0);
    {
        const double want = gesture_ == Gesture::golf_clap || gesture_ == Gesture::steeple ? 1 : 0;
        for (double& v : p.palms_in) v += (want - v) * std::min(1.0, dt * 8);
    }
    p.watch = std::clamp(glow_.x, 0.0, 1.0);
}

}  // namespace fp
