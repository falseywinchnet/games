#include "show.hpp"

#include <algorithm>
#include <cmath>

namespace ct {

namespace {
struct ExpiredPuff { bool operator()(const Puff& puff) const { return puff.age >= puff.life; } };
struct ExpiredBubble { bool operator()(const Bubble& bubble) const { return bubble.age >= bubble.life; } };
const char* const kTaunt[] = {"Too slow!", "Nice hat, bear!", "Finders keepers!", "Can't catch me!", "Yoink!", "Nyah nyah!", "Over here!",
                              "Is that all?", "Missed me!", "Lovely carrots!", "Smell ya later!", "Ooh, a pumpkin. Scary."};
const char* const kTrapped[] = {"Mmmph!", "Hey! It's dark!", "Let me ouuut!", "Who turned off the sun?", "This pumpkin smells.", "Mmf mmf!", "Rude!"};
const char* const kFreed[] = {"Freedom!", "Ha! Thanks, bear!", "Fresh air!", "Woo hoo!", "Back in business!"};
const char* const kDuck[] = {"Eep!", "Yikes!", "Nope!", "Duck!"};
const char* const kLaugh[] = {"Ha ha ha!", "Stuck! Stuck!", "Now you've done it!", "That's not moving.", "Hee hee hee!", "Oh no, bear!"};
const char* const kGiveUp[] = {"We give up!", "Curses!", "Fair cop.", "You win, bear.", "Can we keep one carrot?", "Not the pumpkins!"};
const char* const kBearCatch[] = {"Gotcha!", "In you go!", "Got one!", "Stay put!", "Ha!"};
const char* const kBearStuck[] = {"Oh dear.", "Oops...", "Hmm. That's stuck.", "Oh, bother."};
const char* const kBearWin[] = {"All caught!", "Hooray!", "That's my garden!", "Tidy!"};
template <size_t N> constexpr int count(const char* const (&)[N]) { return static_cast<int>(N); }

double ease(double u) { u = std::clamp(u, 0.0, 1.0); return u * u * (3 - 2 * u); }
double yaw_of(int dir) {
    switch (dir) {
        case kUp: return M_PI;
        case kRight: return M_PI / 2;
        case kLeft: return -M_PI / 2;
        default: return 0;
    }
}
}  // namespace

Show::Show(std::uint64_t seed) : rng_(seed ? seed : 77) {}

double Show::rand01() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 7;
    rng_ ^= rng_ << 17;
    return (rng_ >> 11) * (1.0 / 9007199254740992.0);
}
int Show::rand_int(int n) { return std::min(n - 1, static_cast<int>(rand01() * n)); }

void Show::say(int who, const std::string& text) {
    // never more than two voices at once, and not the same speaker twice
    int live = 0;
    for (const Bubble& b : bubbles) { live += b.age < b.life; if (b.who == who && b.age < b.life) return; }
    if (live >= 2) return;
    bubbles.push_back({text, who, 0, 1.6 + .045 * static_cast<double>(text.size())});
    cues.push_back({Cue::say, text, 1, 1, who});
}

void Show::say_coon(int i, const char* const* lines, int n) {
    Coon& c = coons_[static_cast<size_t>(i)];
    if (c.say_cool > 0) return;
    c.say_cool = 5 + rand01() * 5;
    say(i, lines[rand_int(n)]);
}

void Show::place_all(const Board& board) {
    st_.pumpkins.clear();
    for (size_t b = 0; b < board.boxes().size(); ++b) {
        PumpkinView pv;
        pv.pos = (*garden_).cell_pos(board.boxes()[b]);
        pv.seed = static_cast<int>(b) * 3 + 1;
        pv.on_burrow = board.level().goal[static_cast<size_t>(board.boxes()[b])] != 0;
        st_.pumpkins.push_back(pv);
    }
    bear_from_ = bear_to_ = (*garden_).cell_pos(board.player());
    st_.bear.pos = bear_to_;
    step_t_ = step_len_ = 1;
    push_box_ = -1;
    for (size_t i = 0; i < coons_.size(); ++i) {
        Coon& c = coons_[i];
        const bool covered = board.box_at(c.burrow) >= 0;
        c.state = covered ? Coon::trapped : Coon::hidden;
        c.t = 0;
        c.dur = .5 + rand01() * 2.5;
        c.rise = c.target = 0;
        c.rise_v = 0;
    }
}

void Show::set_level(const Board& board, const Garden& garden, Season season) {
    garden_ = &garden;
    st_ = GardenState{};
    st_.season = season;
    burrow_cells_.clear();
    const Level& lv = board.level();
    for (int i = 0; i < lv.w * lv.h; ++i)
        if (lv.goal[static_cast<size_t>(i)]) burrow_cells_.push_back(i);
    coons_.assign(burrow_cells_.size(), Coon{});
    for (size_t i = 0; i < coons_.size(); ++i) { coons_[i].burrow = burrow_cells_[i]; coons_[i].say_cool = 1 + rand01() * 4; }
    st_.coons.assign(coons_.size(), CoonPose{});
    st_.trapped.assign(coons_.size(), 0);
    st_.paw_wiggle.assign(coons_.size(), 0);
    bubbles.clear();
    stuck_ = false;
    win_t_ = -1;
    bear_yaw_ = yaw_target_ = 0;
    idle_ = 0;
    place_all(board);
}

void Show::restart(const Board& board) {
    stuck_ = false;
    win_t_ = -1;
    bubbles.clear();
    place_all(board);
    for (int i = 0; i < 3; ++i) st_.puffs.push_back({st_.bear.pos + V3{(rand01() - .5) * .6, (rand01() - .5) * .4, .1}, 0, .5, .5, hex(0xFFFFFF)});
}

void Show::moved(const Board& board, const Move& m, bool undo) {
    // finish whatever step was still in flight
    if (step_t_ < step_len_) {
        step_t_ = step_len_;
        st_.bear.pos = bear_to_;
        if (push_box_ >= 0 && push_box_ < static_cast<int>(st_.pumpkins.size())) st_.pumpkins[static_cast<size_t>(push_box_)].pos = box_to_;
    }
    const Level& lv = board.level();
    const int back = (m.dir + 2) % 4;
    const int to = board.player(), from = lv.step(to, undo ? m.dir : back);
    bear_from_ = (*garden_).cell_pos(from);
    bear_to_ = (*garden_).cell_pos(to);
    dir_ = m.dir;
    yaw_target_ = yaw_of(m.dir);
    step_t_ = 0;
    step_len_ = undo ? .11 : .15;
    idle_ = 0;
    push_box_ = -1;
    if (m.push && m.box >= 0 && m.box < static_cast<int>(st_.pumpkins.size())) {
        push_box_ = m.box;
        const int bto = board.boxes()[static_cast<size_t>(m.box)], bfrom = lv.step(bto, undo ? m.dir : back);
        box_from_ = (*garden_).cell_pos(bfrom);
        box_to_ = (*garden_).cell_pos(bto);
        st_.pumpkins[static_cast<size_t>(m.box)].pos = box_from_;
        if (!undo) {
            push_pose_ = 1;
            cues.push_back({Cue::sound, "ct_push_0" + std::to_string(1 + rand_int(3)), .8f, .95f + .1f * static_cast<float>(rand01())});
            st_.puffs.push_back({box_from_ + V3{0, 0, .05}, 0, .5, .35, hex(0xE8D8C0)});
        }
        // a burrow uncovered: its raccoon bursts out
        if (lv.goal[static_cast<size_t>(bfrom)]) {
            for (size_t i = 0; i < coons_.size(); ++i)
                if (coons_[i].burrow == bfrom) {
                    Coon& c = coons_[i];
                    c.state = Coon::up;
                    c.t = 0;
                    c.dur = 2.5 + rand01() * 1.5;
                    c.act = 6;  // relieved
                    c.rise_v = 9;
                    cues.push_back({Cue::sound, "ct_pop", .8f, 1.1f});
                    if (!undo) say_coon(static_cast<int>(i), kFreed, count(kFreed));
                }
        }
    }
    cues.push_back({Cue::sound, "ct_step_0" + std::to_string(1 + rand_int(3)), undo ? .35f : .5f, .9f + .2f * static_cast<float>(rand01())});
    if (undo) cues.push_back({Cue::sound, "ct_undo", .4f});
}

void Show::stuck(bool now) {
    if (now == stuck_) return;
    stuck_ = now;
    stuck_t_ = 0;
    if (now) {
        cues.push_back({Cue::sound, "ct_stinger_stuck", .9f});
        say(-1, kBearStuck[rand_int(count(kBearStuck))]);
        bool laughed = false;
        for (size_t i = 0; i < coons_.size(); ++i) {
            Coon& c = coons_[i];
            if (c.state == Coon::trapped) continue;
            c.state = Coon::up;
            c.act = 7;  // laughing at him
            c.t = 0;
            c.dur = 3.5 + rand01();
            c.say_cool = 0;
            if (!laughed) { say_coon(static_cast<int>(i), kLaugh, count(kLaugh)); laughed = true; }
        }
        if (laughed) cues.push_back({Cue::sound, "ct_laugh", .8f});
    }
}

void Show::won() {
    win_t_ = 0;
    cues.push_back({Cue::sound, "ct_stinger_win", .9f});
    say(-1, kBearWin[rand_int(count(kBearWin))]);
    for (size_t i = 0; i < coons_.size(); ++i) { coons_[i].state = Coon::surrender; coons_[i].t = -static_cast<double>(i) * .25; }
}

void Show::blocked(int dir) {
    yaw_target_ = yaw_of(dir);
    bump_ = 1;
    idle_ = 0;
    cues.push_back({Cue::sound, "ct_bump", .5f});
}

// ------------------------------------------------------------------ the bear
void Show::bear_update(double dt) {
    BearPose& b = st_.bear;
    const double u = step_len_ > 0 ? std::min(1.0, step_t_ / step_len_) : 1;
    const bool stepping = step_t_ < step_len_;
    const V3 base = bear_from_ + (bear_to_ - bear_from_) * ease(u);
    // turn toward the way he is going
    double dy = yaw_target_ - bear_yaw_;
    while (dy > M_PI) dy -= 2 * M_PI;
    while (dy < -M_PI) dy += 2 * M_PI;
    bear_yaw_ += dy * std::min(1.0, dt * 22);
    if (stepping) walk_phase_ += dt * 26;
    else walk_phase_ *= std::max(0.0, 1 - dt * 10);
    push_pose_ = std::max(0.0, push_pose_ - dt * 2.2);
    bump_ = std::max(0.0, bump_ - dt * 5);
    idle_ += dt;
    const double bumpd = std::sin(bump_ * M_PI) * .08;
    b = BearPose{};
    b.pos = base + V3{std::sin(bear_yaw_) * bumpd, -std::cos(bear_yaw_) * bumpd, 0};
    b.yaw = bear_yaw_;
    b.bob = stepping ? std::sin(u * M_PI) * .07 : .01 * std::sin(t_ * 2.2);
    b.squash = stepping ? -.3 * std::sin(u * M_PI) : 0;
    b.walk = stepping ? walk_phase_ : 0;
    b.lean = .25 * push_pose_;
    b.face.eyes = BEyes::open;
    b.face.mouth = BMouth::smile;
    // blink now and then
    if (std::fmod(t_ + 1.7, 3.9) < .12) b.face.eyes = BEyes::closed;
    if (push_pose_ > .2) {
        b.face.eyes = BEyes::shut_tight;
        b.face.mouth = BMouth::effort;
        b.face.brow = 3;
        b.left.hand = {-.14, -.38, .5};
        b.right.hand = {.14, -.38, .5};
    }
    if (thinking_ && !stepping) {
        b.right.hand = {.1, -.24, .7};
        b.face.eyes = BEyes::squint;
        b.face.mouth = BMouth::flat;
        b.face.brow = 1;
        b.head_tilt = .2;
        b.head_nod = -.1;
        yaw_target_ = 0;
    }
    if (stuck_) {
        // a paw to the face, a slow sad shake of the head
        b.facepalm = stuck_t_ < 2.6;
        if (b.facepalm) { b.right.hand = {.1, -.3, .84}; b.face.eyes = BEyes::closed; b.face.mouth = BMouth::frown; b.head_nod = .25; b.head_turn = .15 * std::sin(t_ * 5); }
        else { b.face.eyes = BEyes::worried; b.face.mouth = BMouth::flat; b.face.brow = 2; }
    }
    if (win_t_ >= 0) {
        // jumping for joy, arms up, hat in the air
        const double j = std::fabs(std::sin(win_t_ * 7));
        b.bob = j * .28;
        b.squash = j > .9 ? -.3 : .2 * (1 - j);
        b.face.eyes = BEyes::happy;
        b.face.mouth = BMouth::grin;
        b.face.blush = 1;
        b.left.hand = {-.3, -.05, 1.0 + .05 * j};
        b.right.hand = {.3, -.05, 1.0 + .05 * j};
        b.hat_lift = .12 * j;
        b.yaw = bear_yaw_ * std::max(0.0, 1 - win_t_ * 3);
    } else if (!stepping && idle_ > 5 && !stuck_) {
        // idle business: look about, tap a foot, wipe the brow
        const double it = std::fmod(idle_ - 5, 9.0);
        const int act = static_cast<int>((idle_ - 5) / 9.0) % 3;
        if (act == 0) { b.head_turn = .5 * std::sin(it * 1.4); if (it < .3) b.yaw = 0; }
        else if (act == 1) { b.right.hand = {.12, -.25, .9}; b.face.eyes = BEyes::closed; b.face.mouth = BMouth::o; }
        else { b.walk = std::sin(it * 9) > 0 ? .4 : 0; b.face.mouth = BMouth::flat; b.head_tilt = .15; }
        if (act != 0) yaw_target_ = 0;
    }
}

// ------------------------------------------------------------------ raccoons
void Show::coon_update(int i, double dt, const Board& board) {
    Coon& c = coons_[static_cast<size_t>(i)];
    CoonPose& p = st_.coons[static_cast<size_t>(i)];
    c.t += dt;
    c.say_cool = std::max(0.0, c.say_cool - dt);
    const int box = board.box_at(c.burrow);
    const V3 home = (*garden_).cell_pos(c.burrow);
    const V3 bear = st_.bear.pos;
    const double near = std::max(std::fabs(bear.x - home.x), std::fabs(bear.y - home.y));
    if (c.state != Coon::surrender) {
        if (box >= 0 && step_t_ >= step_len_) {
            if (c.state != Coon::trapped) {
                // a pumpkin has landed on it
                c.state = Coon::trapped;
                c.t = 0;
                c.rise = 0;
                c.rise_v = 0;
                cues.push_back({Cue::sound, "ct_trap", .9f, .95f + .1f * static_cast<float>(rand01())});
                PumpkinView& pv = st_.pumpkins[static_cast<size_t>(box)];
                pv.glow = 1;
                pv.hop = .6;
                st_.puffs.push_back({home + V3{0, 0, .1}, 0, .7, .6, hex(0xE8D8C0)});
                if (win_t_ < 0 && !board.solved() && rand01() < .35) say(-1, kBearCatch[rand_int(count(kBearCatch))]);
                else if (rand01() < .4) say_coon(i, kTrapped, count(kTrapped));
            }
        } else if (c.state == Coon::trapped) {
            c.state = Coon::hidden;
            c.t = 0;
            c.dur = .2;
        }
    }
    p = CoonPose{};
    p.pos = home;
    p.tail = t_ * 3 + i;
    p.prop_t = t_ + i;
    // turn toward the bear
    const double to_bear = std::atan2(bear.x - home.x, -(bear.y - home.y));
    p.yaw = std::clamp(to_bear, -.9, .9) * .6;
    p.head_turn = std::clamp(to_bear * .4, -.5, .5);
    switch (c.state) {
        case Coon::trapped: {
            c.target = 0;
            st_.trapped[static_cast<size_t>(i)] = 1;
            c.wiggle += dt * 6;
            st_.paw_wiggle[static_cast<size_t>(i)] = c.wiggle;
            // now and then it shoves at the pumpkin from below
            if (box >= 0 && std::fmod(c.t + i * 1.3, 6.0) < dt && win_t_ < 0) {
                st_.pumpkins[static_cast<size_t>(box)].wobble = 1;
                cues.push_back({Cue::sound, "ct_muffle_0" + std::to_string(1 + rand_int(2)), .55f, .9f + .2f * static_cast<float>(rand01())});
                if (rand01() < .25) say_coon(i, kTrapped, count(kTrapped));
            }
            break;
        }
        case Coon::surrender: {
            // a white flag waved from under the pumpkin
            st_.trapped[static_cast<size_t>(i)] = 0;
            c.target = c.t > 0 ? .85 : 0;
            p.pos = home + V3{0, -.42, 0};
            p.yaw = 0;
            p.head_turn = 0;
            p.prop = 3;
            p.face.eyes = c.act % 2 ? CEyes::teary : CEyes::closed;
            p.face.mouth = CMouth::wobble;
            p.face.brow = 2;
            p.right.hand = {.15, -.1, .3 + .05 * std::sin(t_ * 8)};
            if (c.t > .3 && c.t - dt <= .3 && i == 0) say_coon(i, kGiveUp, count(kGiveUp));
            break;
        }
        case Coon::hidden:
            st_.trapped[static_cast<size_t>(i)] = 0;
            c.target = 0;
            if (c.t > c.dur && near > 1.5 && win_t_ < 0) { c.state = Coon::peeking; c.t = 0; c.dur = .8 + rand01() * 1.6; }
            break;
        case Coon::peeking:
            c.target = .55;
            p.face.eyes = CEyes::sly;
            p.face.mouth = CMouth::smirk;
            if (near <= 1.5) { c.state = Coon::ducking; c.t = 0; break; }
            if (c.t > c.dur) {
                if (rand01() < .6) { c.state = Coon::up; c.t = 0; c.dur = 2.4 + rand01() * 2; c.act = rand_int(6); cues.push_back({Cue::sound, "ct_pop", .6f, .9f + .3f * static_cast<float>(rand01())}); }
                else { c.state = Coon::hidden; c.t = 0; c.dur = 1 + rand01() * 3; }
            }
            break;
        case Coon::up: {
            c.target = 1.6;
            const double k = c.t;
            switch (c.act) {
                case 0:  // a raspberry
                    p.face.eyes = CEyes::sly; p.face.mouth = k > .4 ? CMouth::raspberry : CMouth::smirk;
                    p.left.hand = {-.1, -.2, .45}; p.right.hand = {.1, -.2, .45};
                    if (k > .4 && k - dt <= .4) { cues.push_back({Cue::sound, "ct_raspberry", .6f, .9f + .2f * static_cast<float>(rand01())}); say(i, "Pbbbt!"); }
                    break;
                case 1:  // juggling stolen carrots
                    p.prop = 2; p.face.eyes = CEyes::open; p.face.mouth = CMouth::grin;
                    p.left.hand = {-.15, -.15, .35 + .08 * std::sin(t_ * 10)}; p.right.hand = {.15, -.15, .35 + .08 * std::cos(t_ * 10)};
                    if (k < dt * 1.5) say_coon(i, kTaunt, count(kTaunt));
                    break;
                case 2:  // waving a carrot about, then a crunch
                    p.prop = 1; p.face.eyes = CEyes::sly; p.face.mouth = std::fmod(k, 1.2) < .3 ? CMouth::o : CMouth::grin;
                    p.right.hand = {.18, -.15, .3 + .2 * std::fabs(std::sin(t_ * 4))};
                    if (std::fmod(k, 1.2) < dt) cues.push_back({Cue::sound, "ct_crunch", .4f, .9f + .2f * static_cast<float>(rand01())});
                    if (k < dt * 1.5) say_coon(i, kTaunt, count(kTaunt));
                    break;
                case 3:  // a little dance
                    p.sway = .25 * std::sin(t_ * 8); p.bounce = .05 * std::fabs(std::sin(t_ * 8));
                    p.face.eyes = CEyes::closed; p.face.mouth = CMouth::grin;
                    p.left.hand = {-.25, -.05, .4 + .1 * std::sin(t_ * 8)}; p.right.hand = {.25, -.05, .4 - .1 * std::sin(t_ * 8)};
                    break;
                case 4:  // pointing at the bear and laughing
                    p.face.eyes = CEyes::laugh; p.face.mouth = CMouth::laugh; p.bounce = .03 * std::fabs(std::sin(t_ * 12));
                    p.right.hand = {.25, -.3, .35}; p.left.hand = {-.1, -.15, .05};
                    if (k < dt * 1.5) say_coon(i, kTaunt, count(kTaunt));
                    break;
                case 5:  // a turnip, tossed and caught
                    p.prop = 4; p.face.eyes = CEyes::open; p.face.mouth = CMouth::smirk;
                    p.right.hand = {.16, -.15, .3 + .25 * std::fabs(std::sin(t_ * 3))};
                    break;
                case 6:  // freed: a stretch and a cheer
                    p.face.eyes = CEyes::laugh; p.face.mouth = CMouth::grin;
                    p.left.hand = {-.22, -.05, .6}; p.right.hand = {.22, -.05, .6};
                    break;
                default:  // 7: laughing at a stuck pumpkin
                    p.face.eyes = CEyes::laugh; p.face.mouth = CMouth::laugh; p.bounce = .05 * std::fabs(std::sin(t_ * 14));
                    p.left.hand = {-.12, -.18, .02}; p.right.hand = {.24, -.3, .3};
                    p.sway = .12 * std::sin(t_ * 7);
                    break;
            }
            if (c.act != 7 && near <= 1.5) { c.state = Coon::ducking; c.t = 0; if (rand01() < .5) say_coon(i, kDuck, count(kDuck)); break; }
            if (c.t > c.dur && !(c.act == 7 && stuck_)) { c.state = Coon::ducking; c.t = 0; }
            break;
        }
        case Coon::ducking:
            c.target = 0;
            p.face.eyes = CEyes::wide;
            p.face.mouth = CMouth::o;
            if (c.t > .25) { c.state = Coon::hidden; c.t = 0; c.dur = 1.2 + rand01() * 3.5; cues.push_back({Cue::sound, "ct_duck", .4f}); }
            break;
    }
    // the rise springs toward its target: quick pops, quicker ducks
    const double k = c.state == Coon::ducking ? 400 : 160, d = c.state == Coon::ducking ? 30 : 16;
    c.rise_v += (k * (c.target - c.rise) - d * c.rise_v) * dt;
    c.rise += c.rise_v * dt;
    if (c.state == Coon::trapped) c.rise = 0;
    p.rise = std::max(0.0, c.rise);
}

void Show::update(double dt, const Board& board) {
    t_ += dt;
    if (!garden_) return;
    step_t_ += dt;
    // the pumpkin being pushed slides and rocks with the bear
    if (push_box_ >= 0 && push_box_ < static_cast<int>(st_.pumpkins.size())) {
        PumpkinView& pv = st_.pumpkins[static_cast<size_t>(push_box_)];
        const double u = std::min(1.0, step_t_ / step_len_);
        pv.pos = box_from_ + (box_to_ - box_from_) * ease(u);
        const double rock = std::sin(u * M_PI) * .3;
        pv.roll_x = dir_ == kUp ? -rock : dir_ == kDown ? rock : 0;
        pv.roll_y = dir_ == kRight ? rock : dir_ == kLeft ? -rock : 0;
        if (u >= 1) { pv.pos = box_to_; pv.roll_x = pv.roll_y = 0; push_box_ = -1; }
    }
    bear_update(dt);
    if (stuck_) stuck_t_ += dt;
    if (win_t_ >= 0) win_t_ += dt;
    for (size_t i = 0; i < coons_.size(); ++i) coon_update(static_cast<int>(i), dt, board);
    for (size_t b = 0; b < st_.pumpkins.size(); ++b) {
        PumpkinView& pv = st_.pumpkins[b];
        pv.wobble = std::max(0.0, pv.wobble - dt * 1.6);
        pv.glow = std::max(0.0, pv.glow - dt * 1.2);
        pv.hop = std::max(0.0, pv.hop - dt * 3);
        pv.on_burrow = board.level().goal[static_cast<size_t>(board.boxes()[b])] != 0;
        if (win_t_ > .4) pv.hop = std::max(pv.hop, .5 * std::fabs(std::sin((win_t_ - b * .15) * 6)) * std::max(0.0, 1 - (win_t_ - 2) * .5));
    }
    for (Puff& p : st_.puffs) p.age += dt;
    st_.puffs.erase(std::remove_if(st_.puffs.begin(), st_.puffs.end(), ExpiredPuff{}), st_.puffs.end());
    for (Bubble& b : bubbles) b.age += dt;
    bubbles.erase(std::remove_if(bubbles.begin(), bubbles.end(), ExpiredBubble{}), bubbles.end());
    st_.hint_t += dt;
}

void Show::settle(const Board& board) {
    if (!garden_) return;
    const int hint_from = st_.hint_from, hint_dir = st_.hint_dir, stuck_cell = st_.stuck_cell;
    for (std::size_t index = 0; index < st_.pumpkins.size() && index < board.boxes().size(); ++index) {
        if (!st_.pumpkins[index].on_burrow && board.level().goal[static_cast<std::size_t>(board.boxes()[index])] && !board.solved())
            cues.push_back({Cue::sound, "ct_trap", .9f});
    }
    place_all(board);
    st_.bear = BearPose{};
    st_.bear.pos = (*garden_).cell_pos(board.player());
    st_.bear.yaw = yaw_target_;
    bear_yaw_ = yaw_target_;
    st_.bear.face.mouth = board.solved() ? BMouth::grin : BMouth::smile;
    st_.puffs.clear();
    st_.fade = 0;
    st_.celebrate = board.solved() ? 1 : 0;
    st_.hint_from = hint_from;
    st_.hint_dir = hint_dir;
    st_.stuck_cell = stuck_cell;
    for (std::size_t i = 0; i < coons_.size(); ++i) {
        const int cell = coons_[i].burrow;
        const bool covered = board.box_at(cell) >= 0;
        st_.trapped[i] = covered ? 1 : 0;
        st_.paw_wiggle[i] = 0;
        st_.coons[i] = CoonPose{};
        st_.coons[i].pos = (*garden_).cell_pos(cell);
        st_.coons[i].rise = covered ? 0 : .65;
        st_.coons[i].face.mouth = CMouth::smirk;
        if (board.solved()) {
            st_.trapped[i] = 0;
            st_.coons[i].pos = st_.coons[i].pos + V3{0, -.42, 0};
            st_.coons[i].rise = .85;
            st_.coons[i].prop = 3;
            st_.coons[i].face.mouth = CMouth::wobble;
        }
    }
    for (Bubble& bubble : bubbles) bubble.age = std::max(.2, bubble.age);
}

}  // namespace ct
