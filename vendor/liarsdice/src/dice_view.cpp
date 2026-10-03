#include "runtime_paths.hpp"
#include "dice_view.hpp"

#include "platform/audio.hpp"
#include "platform/text.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace ld {

namespace {
const Col kInk = hex(0x10181C), kParch = hex(0xEFE4C8), kParchDark = hex(0xD8C8A0), kBrass = hex(0xD0A848), kSea = hex(0x0E2A30), kRed = hex(0xC03030),
          kFoam = hex(0xD8F0EC), kGreen = hex(0x3AA060);

std::string save_path(bool dev) {
    const char* name = dev ? "liars_dice-dev-v1.txt" : "liars_dice-v1.txt";
    if (const char* dir = std::getenv("GAMES_STATE_DIR")) return (std::filesystem::path(dir) / name).string();
    return (games::state_directory() / name).string();
}
std::string cap(std::string s) { if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 32); return s; }
std::string years(int y) { return std::to_string(y) + (y == 1 ? " year" : " years"); }
const char* danger_word(int d) { return d <= 1 ? "easy pickings" : d == 2 ? "a steady hand" : "dangerous"; }
std::string a_an(const std::string& w) { return (w.empty() || std::strchr("aeiou", w[0]) == nullptr ? "a " : "an ") + w; }
}  // namespace

DiceView::DiceView(gf::StableId id, Options opt) : Control(std::move(id)), opt_(opt) {
    cab_front_ = !opt_.hosted;
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    {
        gf::SurfaceMaterial none;
        none.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(0, 0, 0))};
        set_authored_surface_material(none);
    }
    set_accessible_name("Liar's Dice in Davy Jones' locker. Bid on how many dice show a face, ones wild, or call the last bid a lie.");
    if (!load_ledger(save_path(opt_.dev), L_)) {
        L_ = Ledger{};
        L_.offer_seed = static_cast<std::uint64_t>(wall_clock() * 1000) ^ 0xD1CEULL;
    }
    if (L_.in_match) {
        rng_.s = L_.rng ? L_.rng : 1;
        setup_table(false);
        phase_ = Phase::turn;
        say(-1, "Back to the table, sailor. Your cup's where you left it.", 3);
        start_turn();
    } else {
        show_wagers();
    }
    if (const char* sc = std::getenv("LD_SCRIPT"); sc && opt_.dev) {
        std::string all = sc;
        for (size_t pos = 0; pos < all.size();) {
            size_t end = all.find(',', pos);
            if (end == std::string::npos) end = all.size();
            const std::string item = all.substr(pos, end - pos);
            const size_t colon = item.find(':');
            if (colon != std::string::npos) script_.push_back({std::atof(item.c_str()), item.substr(colon + 1)});
            pos = end + 1;
        }
    }
}

void DiceView::on_attached_to_window() {
    audio_start(asset_dir());
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(33));
    subs_.push_back((*timer_).tick().subscribe(*this, gf::Delegate<>::bind<DiceView, &DiceView::tick>(*this)));
    last_ = std::chrono::steady_clock::now();
    (*timer_).start();
}

void DiceView::on_detaching_from_window(gf::Window&) noexcept {
    try { persist(); } catch (...) {}
    if (timer_) (*timer_).stop();
    timer_.reset();

    audio_stop();
}

void DiceView::activate() {
    if (attached_window()) static_cast<void>((*attached_window()).request_focus(shared_from_this()));
}

void DiceView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    pw_ = std::max(64, static_cast<int>(std::ceil(bounds.width / pixel_)));
    ph_ = std::max(64, static_cast<int>(std::ceil(bounds.height / pixel_)));
    frame_.resize(pw_, ph_);
    // the bid panel is one row when there's room (dice | composer | Liar!), two rows when not
    wide_ = pw_ >= 520;
    panel_h_ = wide_ ? std::max(100, ph_ / 4 + 6) : 76;
    top_h_ = wide_ ? 0 : 28;
    cab_.resize(pw_, ph_, panel_h_, top_h_);
    const auto keep = st_;
    cab_.seat(std::max(1, static_cast<int>(st_.crew.size()) - 1), st_);
    for (size_t i = 1; i < st_.crew.size() && i < keep.crew.size(); ++i) {
        const V3 p = st_.crew[i].pos;
        const double y = st_.crew[i].yaw;
        st_.crew[i] = keep.crew[i];
        st_.crew[i].pos = p;
        st_.crew[i].yaw = y;
    }
    bs_ = attached_window() ? (*attached_window()).scale() : 1.0;
    phys_w_ = std::max(1, static_cast<int>(std::lround(bounds.width * bs_)));
    phys_h_ = std::max(1, static_cast<int>(std::lround(bounds.height * bs_)));
    xmap_.clear();
    if (surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        static_cast<void>(surface_->reconfigure(d));
    }
    layout_buttons();
}

void DiceView::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    if (surface_) p.draw_live_surface(surface_, b);
    else p.fill_rect(b, gf::Color::rgba(14, 42, 48));
}

// ------------------------------------------------------------------ the wagers and the game
void DiceView::show_wagers() {
    phase_ = Phase::wagers;
    phase_t_ = 0;
    offers_ = offer(L_.offer_seed, L_.owed, L_.jar);
    st_ = CabinState{};
    cab_.seat(1, st_);
    st_.crew.resize(1);  // an empty table
    st_.cups.resize(1);
    st_.dice.assign(1, {});
    bubbles_.clear();
    if (L_.owed > 0) say(-1, L_.won + L_.lost == 0 ? "Welcome to the locker, drowned one. You owe me a hundred years. Fancy winning some back? Pick your wager."
                                                   : "You owe me " + years(L_.owed) + ". Another wager?", 5);
    else say(-1, "Your debt's paid, free sailor. Play on for trinkets, if you like. Lose, and the years come back.", 5);
    panel_ = Panel::none;
    layout_buttons();
}

void DiceView::take_wager(int i) {
    if (i < 0 || i >= static_cast<int>(offers_.size())) return;
    L_.wager = offers_[static_cast<size_t>(i)];
    L_.in_match = true;
    Rng seeder(L_.offer_seed ^ 0xA11CEULL);
    rng_.s = seeder.next() | 1;
    L_.offer_seed = seeder.next();  // the next wagers will be new ones
    const int n = static_cast<int>(L_.wager.seats.size()) + 1;
    m().start(n, rng_.range(n), rng_);
    for (int s : L_.wager.seats) ++L_.notes[static_cast<size_t>(s)].games;
    setup_table(true);
    phase_ = Phase::greet;
    phase_t_ = 0;
    greet_next_ = 0;
    play("ld_cup_down", .8f);
    persist();
}

void DiceView::setup_table(bool fresh) {
    const int n = m().players;
    st_ = CabinState{};
    cab_.seat(n - 1, st_);
    for (int s = 1; s < n; ++s) {
        st_.crew[static_cast<size_t>(s)].who = L_.wager.seats[static_cast<size_t>(s - 1)];
        if (!m().alive(s)) st_.crew[static_cast<size_t>(s)].fade = 1;
    }
    st_.jar = 0;
    for (int s = 0; s < n; ++s) st_.jar += kStartDice - m().count[static_cast<size_t>(s)];
    st_.dice.assign(static_cast<size_t>(n), {});
    for (int s = 0; s < n; ++s)
        for (int v : m().dice[static_cast<size_t>(s)]) st_.dice[static_cast<size_t>(s)].push_back({v});
    tell_t_.assign(static_cast<size_t>(n), 0);
    hist_tell_.assign(m().history.size(), false);
    bubbles_.clear();
    if (!fresh) reset_composer();
    layout_buttons();
}

void DiceView::begin_round() {
    m().roll(rng_);
    hist_tell_.clear();
    for (auto& c : st_.cups) c = CupShow{};
    phase_ = Phase::shake;
    phase_t_ = 0;
    play("ld_rattle_0" + std::to_string(1 + rng_.range(3)), .8f);
    status_.clear();
    layout_buttons();
}

void DiceView::start_turn() {
    phase_ = Phase::turn;
    phase_t_ = 0;
    const int s = m().turn;
    if (s == 0) {
        reset_composer();
        status_ = "Your move.";
        think_ = auto_ ? .6 : 0;
    } else {
        // thinking takes longer when the bid is already big
        const double weight = m().bid.none() ? 0 : std::clamp(m().bid.qty / (m().total_dice() / 3.0 + 1) - .6, 0.0, 1.5);
        think_ = .6 + rng_.unit() * .8 + weight * .9;
        status_ = ch(s).name + " is thinking...";
    }
    persist();
    layout_buttons();
}

void DiceView::reset_composer() {
    // the default: the smallest raise on the face you hold most of
    const auto& own = m().dice[0];
    int best = 2, bc = -1;
    for (int f = 2; f <= 6; ++f) {
        int c = 0;
        for (int v : own) c += v == f || v == 1;
        if (c > bc || (c == bc && f > best)) { bc = c; best = f; }
    }
    sel_face_ = best;
    const Bid b = m().bid;
    sel_qty_ = b.none() ? std::max(1, bc) : (best > b.face ? b.qty : b.qty + 1);
}

void DiceView::ai_move() {
    const int s = m().turn;
    const Character& c = ch(s);
    Reading r;
    r.bluff.assign(static_cast<size_t>(m().players), kTypicalBluff);
    r.bluff[0] = read_player(c, L_.rec);
    const Decision d = decide(m(), s, c, r, rng_);
    if (d.call) { call_liar(); return; }
    const bool opening = m().bid.none();
    m().place(d.bid);
    hist_tell_.push_back(d.tell);
    say(s, opening ? line_open(c, d.bid, rng_) : line_bid(c, d.bid, rng_));
    if (d.tell) {
        tell_t_[static_cast<size_t>(s)] = 2.0;
        if (c.tell == Tell::bubbles) st_.puffs.push_back({crew_mouth(st_.crew[static_cast<size_t>(s)]), 0});
    }
    play("ld_tap", .5f, .9f + .2f * static_cast<float>(rng_.unit()));
    phase_ = Phase::act;
    phase_t_ = 0;
    status_.clear();
    layout_buttons();
}

void DiceView::player_bid() {
    if (phase_ != Phase::turn || m().turn != 0) return;
    const Bid b{sel_qty_, sel_face_};
    const bool opening = m().bid.none();
    if (!m().place(b)) { play("ld_nope", .5f); return; }
    hist_tell_.push_back(false);
    say(0, cap(bid_words(b)) + (opening ? ", to start." : "."), 2.2);
    play("ld_tap", .5f);
    phase_ = Phase::act;
    phase_t_ = 0;
    status_.clear();
    layout_buttons();
}

void DiceView::call_liar() {
    if ((phase_ != Phase::turn && phase_ != Phase::act) || !m().can_call()) return;
    const int caller = m().turn;
    say(caller, caller == 0 ? "Liar!" : line_call(ch(caller), rng_), 2.4);
    play("ld_liar", .9f);
    audio_duck_music(.6f);
    // hold on to the round's dice for the counting; the match moves on underneath
    rv_ = m().call();
    count_list_.clear();
    for (int s = 0; s < m().players; ++s)
        for (int i = 0; i < static_cast<int>(st_.dice[static_cast<size_t>(s)].size()); ++i) {
            const int v = st_.dice[static_cast<size_t>(s)][static_cast<size_t>(i)].value;
            if (v == rv_.bid.face || v == 1) count_list_.push_back({s, i});
        }
    counted_ = lifted_ = 0;
    sunk_ = settled_ = false;
    phase_ = Phase::reveal;
    phase_t_ = 0;
    status_.clear();
    layout_buttons();
}

void DiceView::step_reveal(double) {
    const double t = phase_t_;
    const int n = m().players;
    // lift the cups, one by one round the table
    const double t_lift = .9, per_lift = .32;
    while (lifted_ < n && t > t_lift + lifted_ * per_lift) {
        if (!st_.dice[static_cast<size_t>(lifted_)].empty() && lifted_ > 0) play("ld_cup_lift", .6f, .95f + .05f * lifted_);
        ++lifted_;
    }
    // then count the dice that match, one at a time
    const double t_count = t_lift + n * per_lift + .3, per_count = std::clamp(3.2 / std::max<size_t>(1, count_list_.size()), .16, .34);
    while (counted_ < static_cast<int>(count_list_.size()) && t > t_count + counted_ * per_count) {
        const auto [s, i] = count_list_[static_cast<size_t>(counted_)];
        DieShow& d = st_.dice[static_cast<size_t>(s)][static_cast<size_t>(i)];
        d.glow = 1;
        d.hop = 1;
        ++counted_;
        play("ld_count", .55f, std::min(2.0f, .85f + .07f * counted_));
    }
    const double t_done = t_count + count_list_.size() * per_count + .35;
    if (t > t_done && result_line_.empty()) {
        for (auto& dv : st_.dice)
            for (DieShow& d : dv)
                if (d.glow <= 0) d.fade = 1;
        const std::string face = face_word(rv_.bid.face, true);
        result_line_ = cap(bid_words(rv_.bid)) + "? " + (rv_.count == 0 ? "Not a single one!" : "There " + std::string(rv_.count == 1 ? "is " : "are ") + number_word(rv_.count) + ".") +
                       (rv_.stood ? " The bid stands." : " A lie!");
        play(rv_.stood ? "ld_true" : "ld_false", .8f);
        // the loser speaks
        const int lo = rv_.loser;
        if (lo != 0) say(lo, lo == rv_.bidder ? line_caught(ch(lo), rng_) : line_wrong_call(ch(lo), rng_), 2.4);
        const int wi = lo == rv_.bidder ? rv_.caller : rv_.bidder;
        if (wi != 0 && wi != lo) say(wi, line_gloat(ch(wi), rng_), 2.2);
    }
    if (t > t_done + 1.1 && !sunk_) {
        sunk_ = true;
        auto& dv = st_.dice[static_cast<size_t>(rv_.loser)];
        if (!dv.empty()) dv.pop_back();
        st_.sinking.push_back({rv_.loser, 0});
        play("ld_plunk", .8f);
    }
    if (t > t_done + 2.8 && !settled_) {
        settled_ = true;
        settle_reveal();
    }
}

void DiceView::settle_reveal() {
    // what everyone learned: the crew note your bluffs, you note theirs (and whether their tell showed)
    int total = 0;
    for (const auto& d : st_.dice) total += static_cast<int>(d.size());
    total += 1;  // the die just lost was on the table too
    for (size_t k = 0; k < m().history.size(); ++k) {
        const BidRec& h = m().history[k];
        const bool bl = was_bluff(m().dice[static_cast<size_t>(h.who)], h.bid, total);
        if (h.who == 0) L_.rec.observe(bl);
        else {
            CrewNotes& nt = L_.notes[static_cast<size_t>(L_.wager.seats[static_cast<size_t>(h.who - 1)])];
            ++nt.bids_seen;
            if (bl) {
                ++nt.bluffs_seen;
                if (k < hist_tell_.size() && hist_tell_[k]) ++nt.tells_caught;
            }
        }
    }
    result_line_.clear();
    // out of dice?
    const int lo = rv_.loser;
    if (!m().alive(0)) { end_match(false); return; }
    if (m().over()) { end_match(true); return; }
    if (!m().alive(lo) && lo != 0) {
        say(lo, line_out(ch(lo), rng_), 2.5);
        play("ld_out", .7f);
    }
    for (auto& dv : st_.dice) dv.clear();
    begin_round();
}

void DiceView::end_match(bool won) {
    won_ = won;
    phase_ = Phase::result;
    phase_t_ = 0;
    const Wager& w = L_.wager;
    std::string keeper;
    if (won) {
        ++L_.won;
        const int off = std::min(w.years_win, L_.owed);
        L_.owed -= off;
        L_.struck += off;
        for (int s : w.seats) ++L_.notes[static_cast<size_t>(s)].beaten;
        if (w.prize.rfind("back:", 0) == 0) {
            const std::string thing = w.prize.substr(5);
            const auto it = std::find(L_.jar.begin(), L_.jar.end(), thing);
            if (it != L_.jar.end()) L_.jar.erase(it);
        } else if (!w.prize.empty()) {
            L_.trophies.push_back(w.prize);
        }
        keeper = "Well played, sailor. " + (off > 0 ? years(off) + " struck off" : std::string("A win")) + (w.prize.empty() ? "." : ", and " + prize_words(w.prize) + ".") +
                 (L_.owed > 0 ? " You owe me " + years(L_.owed) + "." : "");
        play("ld_stinger_win", .9f);
    } else {
        ++L_.lost;
        L_.owed += w.years_lose;
        L_.served += w.years_lose;
        if (!w.forfeit.empty() && std::find(L_.jar.begin(), L_.jar.end(), w.forfeit) == L_.jar.end()) L_.jar.push_back(w.forfeit);
        keeper = "That's " + years(w.years_lose) + " more in my locker" + (w.forfeit.empty() ? "" : ", and " + w.forfeit + " in my jar") + ". You owe me " + years(L_.owed) + ".";
        // whoever took your last die enjoys it
        const int wi = rv_.loser == rv_.bidder ? rv_.caller : rv_.bidder;
        if (wi > 0) say(wi, line_win(ch(wi), rng_), 2.6);
        play("ld_stinger_lose", .9f);
    }
    if (won) for (int s = 1; s < m().players; ++s) if (st_.crew.size() > static_cast<size_t>(s)) st_.crew[static_cast<size_t>(s)].slump = 1;
    say(-1, keeper, 6);
    L_.in_match = false;
    if (won && L_.owed == 0 && !L_.freed) {
        L_.freed = true;
        phase_ = Phase::freedom;
        play("ld_stinger_free", .9f);
    }
    persist();
    layout_buttons();
}

void DiceView::say(int seat, const std::string& text, double life) {
    bubbles_.erase(std::remove_if(bubbles_.begin(), bubbles_.end(), [&](const Bubble& b) { return b.seat == seat; }), bubbles_.end());
    bubbles_.push_back({seat, text, 0, life > 0 ? life : 1.6 + text.size() / 30.0, 0});
    while (bubbles_.size() > 4) bubbles_.pop_front();
}

void DiceView::persist() {
    L_.rng = rng_.s;
    save_ledger(save_path(opt_.dev), L_);
    dirty_ = false;
}

void DiceView::play(const std::string& name, float gain, float rate) {
    audio_sfx(name, gain, rate, (opt_.hosted || L_.sound) && cab_sound_ && cab_front_ && visible());
}

// ------------------------------------------------------------------ the frame
void DiceView::run_script() {
    while (!script_.empty() && script_.front().first <= t_) {
        const std::string c = script_.front().second;
        script_.erase(script_.begin());
        if (c.rfind("size", 0) == 0) {  // dev: size600x370 resizes the window
            int ww = 0, hh = 0;
            if (std::sscanf(c.c_str() + 4, "%dx%d", &ww, &hh) == 2) dev_resize_window(ww, hh);
            continue;
        }
        if (c[0] == 'w' && c.size() == 2) take_wager(c[1] - '0');
        else if (c == "skip") { if (phase_ == Phase::greet) { bubbles_.clear(); begin_round(); } }
        else if (c == "call") { if (m().turn == 0) call_liar(); }
        else if (c == "bid") player_bid();
        else if (c == "auto") auto_ = true;
        else if (c.rfind("speed", 0) == 0) speed_ = std::atof(c.c_str() + 5);
        else if (c == "help") open(Panel::help);
        else if (c == "log") open(Panel::logbook);
        else if (c == "rec") open(Panel::records);
        else if (c == "close") open(Panel::none);
        else if (c == "again") action("again");
        else if (c.rfind("owed", 0) == 0) { L_.owed = std::atoi(c.c_str() + 4); show_wagers(); }
    }
}

void DiceView::animate(double dt) {
    const int turn = (phase_ == Phase::turn || phase_ == Phase::act) ? m().turn : -1;
    // who is speaking (their mouth moves while their bubble types)
    std::vector<int> talking(st_.crew.size(), 0);
    for (const Bubble& b : bubbles_)
        if (b.seat > 0 && b.seat < static_cast<int>(talking.size()) && b.shown < static_cast<int>(b.text.size())) talking[static_cast<size_t>(b.seat)] = 1;
    for (size_t i = 1; i < st_.crew.size(); ++i) {
        CrewPose& p = st_.crew[i];
        const int seat = static_cast<int>(i);
        const bool out = m().players > seat && !m().alive(seat) && phase_ != Phase::wagers;
        p.mouth = talking[i] ? .45 + .45 * std::sin(t_ * 19 + seat) : 0;
        p.blink = std::fmod(t_ * .9 + seat * 1.37, 4.1) < .13 ? 1 : 0;
        // looking: at whoever's moving, at you when it's your move, at the dice in a reveal
        double look = 0;
        int target = turn;
        if (phase_ == Phase::reveal) target = rv_.bidder;
        if (target == 0 || target < 0) look = 0;
        else if (target != seat) {
            const V3 a = st_.crew[static_cast<size_t>(target)].pos, me = p.pos;
            look = (a.x - me.x) > 0 ? .45 : -.45;
        }
        look += .12 * std::sin(t_ * .4 + seat * 2.1);
        p.head_turn += (look - p.head_turn) * std::min(1.0, dt * 4);
        p.head_down = phase_ == Phase::reveal && phase_t_ > 1 ? .5 : (turn == seat && phase_ == Phase::turn ? .25 : 0);
        p.lean = p.flush = p.sway = p.twitch = p.tap = 0;
        // a tell, played small: up quickly, held, down
        double& tt = tell_t_.size() > i ? tell_t_[i] : p.hover;
        if (tell_t_.size() > i && tt > 0) {
            tt = std::max(0.0, tt - dt);
            const double env = std::min({1.0, (2.0 - tt) * 4, tt * 2});
            apply_tell(p, ch(seat).tell, env * .7, t_);
        }
        p.fade += ((out ? 1.0 : 0.0) - p.fade) * std::min(1.0, dt * 2);
        if (phase_ != Phase::result) p.slump = std::max(0.0, p.slump - dt);
        p.bob = 0;
        if (phase_ == Phase::result && !won_ && !out) p.bob = std::fmod(t_ * 1.6 + seat * .3, 1.0);
        p.hover = hover_seat_ == seat ? 1 : 0;
    }
    // cups: rattling while shaken, up during a reveal
    for (size_t s = 0; s < st_.cups.size(); ++s) {
        CupShow& c = st_.cups[s];
        const bool shaking = phase_ == Phase::shake && phase_t_ < 1.1 * (1 / speed_);
        c.shake = shaking && (s == 0 || m().alive(static_cast<int>(s))) ? 1 : 0;
        double lift = 0;
        if (phase_ == Phase::reveal || phase_ == Phase::result || phase_ == Phase::freedom) lift = static_cast<int>(s) < lifted_ || phase_ != Phase::reveal ? 1 : 0;
        if (s == 0 && phase_ != Phase::shake) lift = 1;
        if (static_cast<int>(s) < m().players && !m().alive(static_cast<int>(s)) && phase_ != Phase::reveal) lift = 1;
        c.lift += (lift - c.lift) * std::min(1.0, dt * 6);
    }
    for (auto& dv : st_.dice)
        for (DieShow& d : dv) d.hop = std::max(0.0, d.hop - dt * 3);
    for (Sink& sk : st_.sinking) sk.t += dt * 1.1;
    while (!st_.sinking.empty() && st_.sinking.front().t > 1) { st_.sinking.erase(st_.sinking.begin()); ++st_.jar; }
    for (auto& pf : st_.puffs) pf.second += dt;
    st_.puffs.erase(std::remove_if(st_.puffs.begin(), st_.puffs.end(), [](const auto& p) { return p.second > 2.6; }), st_.puffs.end());
    // the Keeper glows while he talks
    bool keeper_talks = false;
    for (const Bubble& b : bubbles_) keeper_talks = keeper_talks || (b.seat < 0 && b.age < b.life - .3);
    st_.keeper += ((keeper_talks ? 1.0 : 0.0) - st_.keeper) * std::min(1.0, dt * 3);
    if (cab_reduced_) {
        for (CrewPose& p : st_.crew) { p.mouth = p.blink = p.bob = 0; }
        for (CupShow& c : st_.cups) { c.shake = 0; c.lift = c.lift > .05 ? 1 : 0; }
        for (std::vector<DieShow>& row : st_.dice) for (DieShow& d : row) d.hop = 0;
        st_.puffs.clear();
    }
    // the room darkens as the bids climb past what's likely
    double tension = 0;
    if (phase_ == Phase::turn || phase_ == Phase::act) {
        const double exp = m().total_dice() / 3.0;
        tension = m().bid.none() ? 0 : std::clamp((m().bid.qty - exp) / (exp * .6 + .5), 0.0, 1.0);
    } else if (phase_ == Phase::reveal) tension = .8;
    st_.tension += (tension - st_.tension) * std::min(1.0, dt * 1.5);
}

void DiceView::tick() {
    if (!visible() || !cab_front_) { last_ = std::chrono::steady_clock::now(); return; }
    const auto now = std::chrono::steady_clock::now();
    const double rdt = std::clamp(std::chrono::duration<double>(now - last_).count(), 0.0, .25);
    last_ = now;
    const double dt = rdt * speed_;
    t_ += dt;
    phase_t_ += dt;
    run_script();
    switch (phase_) {
        case Phase::wagers: break;
        case Phase::greet:
            if (phase_t_ > .5 + greet_next_ * 1.3) {
                if (greet_next_ < m().players - 1) {
                    const int s = greet_next_ + 1;
                    say(s, line_greet(ch(s), rng_), 1.8);
                    ++greet_next_;
                } else if (phase_t_ > .5 + greet_next_ * 1.3 + .6) {
                    begin_round();
                }
            }
            break;
        case Phase::shake:
            if (phase_t_ > 1.4) {
                st_.dice.assign(static_cast<size_t>(m().players), {});
                for (int s = 0; s < m().players; ++s)
                    for (int v : m().dice[static_cast<size_t>(s)]) st_.dice[static_cast<size_t>(s)].push_back({v});
                play("ld_cup_down", .7f);
                start_turn();
            }
            break;
        case Phase::turn:
            if (m().turn != 0) {
                think_ -= dt;
                if (think_ <= 0) ai_move();
            } else if (auto_) {
                think_ -= dt;
                if (think_ <= 0) {
                    Character me;
                    me.bluff = .15; me.nerve = .42; me.greed = .3; me.skill = .7;
                    Reading r;
                    r.bluff.assign(static_cast<size_t>(m().players), kTypicalBluff);
                    const Decision d = decide(m(), 0, me, r, rng_);
                    if (d.call) call_liar();
                    else { sel_qty_ = d.bid.qty; sel_face_ = d.bid.face; player_bid(); }
                }
            }
            break;
        case Phase::act:
            if (phase_t_ > (m().history.empty() || m().history.back().who == 0 ? .7 : 1.25)) start_turn();
            break;
        case Phase::reveal: step_reveal(dt); break;
        case Phase::result:
        case Phase::freedom:
            if (auto_ && phase_t_ > 4) action("again");
            break;
    }
    // speech types out with a babble in the speaker's voice
    for (Bubble& b : bubbles_) {
        const int before = b.shown;
        b.age += dt;
        b.shown = std::min(static_cast<int>(b.text.size()), static_cast<int>(b.age * 38));
        if (b.shown / 3 != before / 3 && b.shown < static_cast<int>(b.text.size()) && b.text[static_cast<size_t>(b.shown)] != ' ') {
            if (b.seat < 0) play("ld_keeper_0" + std::to_string(1 + rng_.range(3)), .35f, .9f + .1f * static_cast<float>(rng_.unit()));
            else if (b.seat > 0 && b.seat < static_cast<int>(st_.crew.size())) {
                const Species sp = ch(b.seat).species;
                const int v = static_cast<int>(sp);
                const float pitch = sp == Species::turtle || sp == Species::grouper ? .8f : sp == Species::eel || sp == Species::crab ? 1.25f : 1.0f;
                play("ld_voice_" + std::to_string(v) + "_0" + std::to_string(1 + rng_.range(3)), .32f, pitch * (.94f + .12f * static_cast<float>(rng_.unit())));
            }
        }
    }
    while (!bubbles_.empty() && bubbles_.front().age > bubbles_.front().life) bubbles_.pop_front();
    animate(dt);
    {
        const bool on = (opt_.hosted || L_.music) && cab_music_ && cab_front_ && visible();
        audio_music_on_bar(visible() && cab_front_ ? (st_.tension > .45 ? "ld_music_tense" : "ld_music") : "", on);
    }
    audio_tick(rdt);
    save_t_ += rdt;
    if (dirty_ && save_t_ > .5) { save_t_ = 0; persist(); }
    gf::Window* win = attached_window();
    // dev captures (--dev with LD_PRINT_WID) keep drawing even when the window is covered
    static const bool capturing = opt_.dev && std::getenv("LD_PRINT_WID");
    const bool hidden = win && (*win).occluded() && !capturing;
    const bool front = !win || (*win).active();
    const bool shown = visible();
    const auto pace = std::chrono::milliseconds(hidden || !shown || !cab_front_ ? 500 : front ? 33 : 100);
    if (timer_ && (*timer_).interval() != pace) (*timer_).set_interval(pace);

    if (hidden || !shown || frame_.px.empty() || cab_.r.rgb.empty()) return;
    cab_.render(st_, cab_reduced_ ? 0 : t_);
    compose();
    publish();
}

void DiceView::publish() {
    if (!surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        surface_ = gf::LiveSurface::create(d);
        if (surface_ && attached_window()) direct_ = (*attached_window()).queue_live_surface_presentation(shared_from_this(), surface_);
    }
    if (!surface_) return;
    gf::LiveSurfaceWriteLease lease = surface_->try_acquire_write();
    if (lease && static_cast<int>(lease.width()) == phys_w_ && static_cast<int>(lease.height()) == phys_h_) {
        std::span<std::byte> dst = lease.pixels();
        const size_t rb = lease.row_bytes();
        const double k = pixel_ * bs_;
        if (xmap_.size() != static_cast<size_t>(phys_w_)) {
            xmap_.resize(static_cast<size_t>(phys_w_));
            for (int x = 0; x < phys_w_; ++x) xmap_[static_cast<size_t>(x)] = std::min(frame_.w - 1, static_cast<int>(x / k));
        }
        const std::uint32_t* src = reinterpret_cast<const std::uint32_t*>(frame_.px.data());
        for (int y = 0; y < phys_h_; ++y) {
            const int sy = std::min(frame_.h - 1, static_cast<int>(y / k));
            std::uint32_t* o = reinterpret_cast<std::uint32_t*>(dst.data() + static_cast<size_t>(y) * rb);
            const std::uint32_t* sr = src + static_cast<size_t>(sy) * frame_.w;
            for (int x = 0; x < phys_w_; ++x) o[x] = sr[xmap_[static_cast<size_t>(x)]];
        }
        blit_texts(reinterpret_cast<std::uint32_t*>(dst.data()), rb / 4, k);
        static_cast<void>(lease.publish());
    }
    if (!direct_) invalidate(gf::Dirty::paint);
    text_cache_trim();
}

// ------------------------------------------------------------------ input
void DiceView::on_pointer(gf::PointerEvent& e) {
    const gf::Point local = point_from_window(e.position);
    mouse_x_ = local.x / pixel_;
    mouse_y_ = local.y / pixel_;
    auto hit = [&]() -> std::string {
        for (const Button& b : buttons_)
            if (b.enabled && mouse_x_ >= b.x && mouse_x_ < b.x + b.w && mouse_y_ >= b.y && mouse_y_ < b.y + b.h) return b.id;
        return {};
    };
    if (e.action == gf::PointerAction::move) {
        hover_ = hit();
        hover_seat_ = hover_.empty() && panel_ == Panel::none && phase_ != Phase::wagers && mouse_y_ < ph_ - panel_h_ ? cab_.pick(mouse_x_, mouse_y_, st_) : -1;
        set_cursor(!hover_.empty() ? gf::CursorKind::hand : gf::CursorKind::arrow);
    }
    if (e.action == gf::PointerAction::down && e.button == gf::PointerButton::primary) {
        activate();
        const std::string h = hit();
        if (!h.empty()) { pressed_ = h; e.handled = true; return; }
        if (phase_ == Phase::greet) { bubbles_.clear(); begin_round(); }
        e.handled = true;
    } else if (e.action == gf::PointerAction::up) {
        if (!pressed_.empty() && hit() == pressed_) action(pressed_);
        pressed_.clear();
        e.handled = true;
    }
}

void DiceView::on_key(gf::KeyEvent& e) {
    if (e.handled || e.action != gf::KeyAction::down) return;
    using K = gf::PhysicalKey;
    const std::uint32_t k = e.physical_key;
    if (panel_ != Panel::none) {
        if (k == K::escape || k == K::enter || k == K::space || (k == K::f1 && panel_ == Panel::help)) open(Panel::none);
        e.handled = true;
        return;
    }
    if (k == K::f1) { open(Panel::help); e.handled = true; return; }
    if (!opt_.hosted && k == K::m) { action("music"); e.handled = true; return; }
    if (phase_ == Phase::turn && m().turn == 0) {
        if (k >= 0x1F && k <= 0x23) { sel_face_ = static_cast<int>(k - 0x1F) + 2; layout_buttons(); e.handled = true; return; }  // keys 2..6
        if (k == 0x52 || k == K::equal) { action("qty+"); e.handled = true; return; }   // up
        if (k == 0x51 || k == K::minus) { action("qty-"); e.handled = true; return; }   // down
        if (k == K::enter) { player_bid(); e.handled = true; return; }
        if (k == K::l || k == K::backspace) { call_liar(); e.handled = true; return; }
    }
    if ((phase_ == Phase::result || phase_ == Phase::freedom) && (k == K::enter || k == K::space)) { action("again"); e.handled = true; return; }
    if (phase_ == Phase::greet && (k == K::enter || k == K::space)) { bubbles_.clear(); begin_round(); e.handled = true; }
}

void DiceView::action(const std::string& id) {
    play("ld_click", .4f);
    if (id == "close") open(Panel::none);
    else if (id == "help") open(Panel::help);
    else if (id == "logbook") open(Panel::logbook);
    else if (id == "records") open(Panel::records);
    else if (id == "music") { L_.music = !L_.music; dirty_ = true; layout_buttons(); }
    else if (id == "sound") { L_.sound = !L_.sound; dirty_ = true; layout_buttons(); }
    else if (id.rfind("take", 0) == 0) take_wager(std::atoi(id.c_str() + 4));
    else if (id.rfind("tab", 0) == 0) { sel_wager_ = std::atoi(id.c_str() + 3); layout_buttons(); }
    else if (id == "log_prev") { --log_page_; layout_buttons(); }
    else if (id == "log_next") { ++log_page_; layout_buttons(); }
    else if (id == "again") show_wagers();
    else if (id == "bid") player_bid();
    else if (id == "liar") call_liar();
    else if (id == "qty+") { sel_qty_ = std::min(m().total_dice(), sel_qty_ + 1); layout_buttons(); }
    else if (id == "qty-") { sel_qty_ = std::max(1, sel_qty_ - 1); layout_buttons(); }
    else if (id.rfind("face", 0) == 0) {
        sel_face_ = std::atoi(id.c_str() + 4);
        // keep the quantity legal for the new face, if it can be
        const Bid b = m().bid;
        if (!b.none() && !beats({sel_qty_, sel_face_}, b)) sel_qty_ = sel_face_ > b.face ? b.qty : b.qty + 1;
        layout_buttons();
    }
}

void DiceView::open(Panel p) {
    panel_ = p;
    pressed_.clear();
    layout_buttons();
}

// ------------------------------------------------------------------ drawing helpers
const Mask& DiceView::tmask(const std::string& s, int font, double size, int wrap_game) const {
    const Font f = font == 1 ? Font::speech_bold : font == 2 ? Font::title : font == 3 ? Font::ui : Font::speech;
    return text_mask(s, f, size, wrap_game > 0 ? wrap_game * pixel_ : 0);
}
int DiceView::text(const std::string& s, int x, int y, Col c, double size, int font, int wrap) {
    texts_.push_back({s, font, size, wrap, x, y, c});
    return text_w(s, size, font);
}
int DiceView::text_w(const std::string& s, double size, int font) const { return static_cast<int>(std::ceil(tmask(s, font, size, 0).w / static_cast<double>(pixel_))); }
int DiceView::text_h(const std::string& s, double size, int font, int wrap) const { return static_cast<int>(std::ceil(tmask(s, font, size, wrap).h / static_cast<double>(pixel_))); }

void DiceView::draw_die_icon(int x, int y, int size, int value, Col body, Col pip, bool lit) {
    if (lit) { frame_.begin(); frame_.rrect(x - 2, y - 2, size + 4, size + 4, 4); frame_.fill(hex(0xF8E070, .55f)); }
    frame_.fill_rect(x + 1, y + 2, size, size, hex(0x000000, .35f));
    frame_.begin(); frame_.rrect(x, y, size, size, size * .2); frame_.fill(body);
    frame_.begin(); frame_.rrect(x + .5, y + .5, size - 1, size - 1, size * .2); frame_.stroke(hex(0x000000, .5f), 1);
    static const double P[7][6][2] = {
        {}, {{.5, .5}}, {{.27, .27}, {.73, .73}}, {{.27, .27}, {.5, .5}, {.73, .73}}, {{.27, .27}, {.73, .27}, {.27, .73}, {.73, .73}},
        {{.27, .27}, {.73, .27}, {.5, .5}, {.27, .73}, {.73, .73}}, {{.27, .25}, {.73, .25}, {.27, .5}, {.73, .5}, {.27, .75}, {.73, .75}},
    };
    const double r = std::max(1.0, size * .09);
    for (int k = 0; k < value && value <= 6; ++k) frame_.fill_circle(x + P[value][k][0] * size, y + P[value][k][1] * size, value == 1 ? r * 1.5 : r, value == 1 ? hex(0xB02020) : pip);
}

void DiceView::layout_game_buttons() {
    buttons_.clear();
    if (pw_ <= 0) return;
    // a row of buttons sized to their labels, centred at y
    auto row = [&](const std::vector<std::pair<std::string, std::string>>& items, int y, int h) {
        int pad = 10, gap = 6, total = 0;
        for (int tries = 0; tries < 3; ++tries) {
            total = -gap;
            for (const auto& it : items) total += std::max(36, text_w(it.second, 11, 0) + 2 * pad) + gap;
            if (total <= pw_ - 12) break;
            pad -= 3; gap -= 2;
        }
        int x = (pw_ - total) / 2;
        for (const auto& [id, label] : items) {
            const int w = std::max(36, text_w(label, 11, 0) + 2 * pad);
            buttons_.push_back({id, label, x, y, w, h});
            x += w + gap;
        }
    };
    if (panel_ != Panel::none) {
        const Box b = overlay_box();
        buttons_.push_back({"close", "Close", pw_ / 2 - 36, b.y + b.h + 6, 72, 18});
        if (panel_ == Panel::logbook) {
            const int per = logbook_rows() * logbook_cols(), pages = (32 + per - 1) / per;
            log_page_ = std::clamp(log_page_, 0, pages - 1);
            if (pages > 1) {
                buttons_.push_back({"log_prev", "<", pw_ / 2 - 36 - 8 - 30, b.y + b.h + 6, 30, 18, log_page_ > 0});
                buttons_.push_back({"log_next", ">", pw_ / 2 + 36 + 8, b.y + b.h + 6, 30, 18, log_page_ < pages - 1});
            }
        }
        return;
    }
    const int y0 = ph_ - panel_h_;
    if (phase_ == Phase::wagers) {
        const int by = ph_ - (opt_.hosted ? 4 : 24);
        const bool small = pw_ < 420;
        row({{"logbook", small ? "Logbook" : "Crew logbook"}, {"records", "Records"}, {"help", small ? "Help" : "How to play"},
             {"music", small ? (L_.music ? "Music" : "Mute") : (L_.music ? "Music: on" : "Music: off")}}, by, 18);
        // three cards side by side if their words fit; otherwise tabs over one card
        const int cw = std::min(190, (pw_ - 40) / 3);
        int need = 0;
        for (const Wager& w : offers_) need = std::max(need, wager_card_h(w, cw));
        tabbed_ = pw_ < 480 || 64 + need + 30 > by - 4;
        if (!tabbed_) {
            const int gap = (pw_ - cw * 3) / 4, chh = std::min(by - 8 - 64, std::max(need + 30, 150));
            for (int i = 0; i < 3; ++i) buttons_.push_back({"take" + std::to_string(i), "Take this wager", gap + i * (cw + gap) + 12, 64 + chh - 26, cw - 24, 18, true, 0});
        } else {
            sel_wager_ = std::clamp(sel_wager_, 0, std::max(0, static_cast<int>(offers_.size()) - 1));
            const int tw = (pw_ - 16 - 8) / 3;
            for (int i = 0; i < static_cast<int>(offers_.size()) && i < 3; ++i)
                buttons_.push_back({"tab" + std::to_string(i), offers_[static_cast<size_t>(i)].title, 8 + i * (tw + 4), 28, tw, 30, true, 4});
            const int cy = 62, ch = by - 6 - cy;
            const int cwid = std::min(pw_ - 16, 460), cx = (pw_ - cwid) / 2;
            buttons_.push_back({"take" + std::to_string(sel_wager_), "Take this wager", cx + cwid - 10 - 110, cy + ch - 24, 110, 18, true, 0});
        }
        return;
    }
    if (phase_ == Phase::result || phase_ == Phase::freedom) {
        buttons_.push_back({"again", "Back to the Keeper", pw_ / 2 - 70, y0 + panel_h_ - 30, 140, 20});
        return;
    }
    const bool mine = phase_ == Phase::turn && m().turn == 0 && !auto_;
    const Bid b{sel_qty_, sel_face_};
    const bool legal = beats(b, m().bid) && b.qty <= m().total_dice();
    if (wide_) {
        // one row: your dice | the composer | Liar! and the small buttons
        const int cx = 150;
        const int ry = y0 + 36;
        const int rx = pw_ - 120;
        buttons_.push_back({"help", "Help", rx, y0 + 52, 53, 16});
        buttons_.push_back({"music", L_.music ? "Music" : "No music", rx + 57, y0 + 52, 53, 16});
        buttons_.push_back({"logbook", "Logbook", rx, y0 + 72, 110, 16});
        buttons_.push_back({"liar", "Liar!", rx, y0 + 10, 110, 34, mine && m().can_call(), 1});
        if (!mine) return;
        buttons_.push_back({"qty-", "-", cx + 28, ry, 18, 20, mine && sel_qty_ > 1});
        buttons_.push_back({"qty+", "+", cx + 76, ry, 18, 20, mine && sel_qty_ < m().total_dice()});
        for (int f = 2; f <= 6; ++f) buttons_.push_back({"face" + std::to_string(f), std::to_string(f), cx + 104 + (f - 2) * 24, ry, 20, 20, mine, 2});
        buttons_.push_back({"bid", "Bid " + bid_words(b), cx + 28, ry + 28, 196, 20, mine && legal});
        return;
    }
    // two rows: your dice and Liar!, then the composer; the small buttons go to the scene's top corner
    {
        int x = pw_ - 6;
        for (const auto& [id, label] : std::vector<std::pair<std::string, std::string>>{{"logbook", "Log"}, {"music", L_.music ? "Music" : "Mute"}, {"help", "?"}}) {
            const int w = std::max(20, text_w(label, 11, 0) + 10);
            x -= w;
            buttons_.push_back({id, label, x, 6, w, 16});
            x -= 4;
        }
    }
    buttons_.push_back({"liar", "Liar!", pw_ - 8 - 84, y0 + 5, 84, 24, mine && m().can_call(), 1});
    if (!mine) return;
    const int ry = y0 + 34;
    buttons_.push_back({"qty-", "-", 10, ry, 18, 20, sel_qty_ > 1});
    buttons_.push_back({"qty+", "+", 56, ry, 18, 20, sel_qty_ < m().total_dice()});
    for (int f = 2; f <= 6; ++f) buttons_.push_back({"face" + std::to_string(f), std::to_string(f), 80 + (f - 2) * 22, ry, 20, 20, true, 2});
    const int bx = 80 + 5 * 22 + 4;
    std::string bl = "Bid " + bid_words(b);
    if (text_w(bl, 11, 0) + 8 > pw_ - 8 - bx) bl = "Bid";
    buttons_.push_back({"bid", bl, bx, ry, pw_ - 8 - bx, 20, legal});
}

DiceView::Box DiceView::overlay_box() const {
    const int ww = std::min(pw_ - 16, panel_ == Panel::logbook ? 560 : 400);
    const int room = ph_ - 16 - 24;  // the Close button sits below
    int wh = 0;
    if (panel_ == Panel::logbook) {
        wh = std::min(room, 400);
    } else {
        // the words wrap to the width and step down a size until they fit
        for (double size : {11.0, 10.0, 9.0, 8.5}) {
            ov_size_ = size;
            wh = 30 + 8;
            for (const std::string& l : overlay_lines()) wh += text_h(l, size, 0, ww - 28) + 4;
            if (wh <= room) break;
        }
        wh = std::min(wh, room);
    }
    return {(pw_ - ww) / 2, std::max(6, (ph_ - 24 - wh) / 2), ww, wh};
}

std::vector<std::string> DiceView::overlay_lines() const {
    if (panel_ == Panel::help)
        return {"Everyone has five dice under a cup, seen only by their owner. In turn, each player raises the bid: a claim about all the dice on the table. \"Seven fours\" means at least seven dice show a four.",
                "Ones are wild: they count as every face, and nobody bids on ones. A raise is more dice, or the same number of a higher face.",
                "Instead of raising, call the last bid a lie. Every cup comes up and the dice are counted. If the bid stands, the caller loses a die; if not, the bidder does. Lost dice go in the Keeper's jar.",
                "Lose all your dice and you lose the wager. Be the last with dice and you win it.",
                "The crew bluff, some often, some hardly ever, and each has a tell that slips out more when they're lying. Some of them learn how often you bluff, too.",
                "Keys: 2-6 choose a face, up/down the number, Enter bids, L calls liar. Use the capsule for music and sound. F1 help."};
    if (panel_ == Panel::records) {
        std::vector<std::string> v;
        v.push_back("Owed to the locker: " + years(L_.owed) + (L_.freed ? "  (you have been free)" : ""));
        v.push_back("Wagers won: " + std::to_string(L_.won) + ".  Lost: " + std::to_string(L_.lost) + ".");
        v.push_back("Years served (added): " + std::to_string(L_.served) + ".  Years struck off: " + std::to_string(L_.struck) + ".");
        std::string j;
        for (size_t i = 0; i < L_.jar.size(); ++i) j += (i ? ", " : "") + L_.jar[i];
        v.push_back("In the Keeper's jar: " + (j.empty() ? std::string("nothing of yours") : j) + ".");
        std::string t;
        for (size_t i = 0; i < L_.trophies.size(); ++i) t += (i ? ", " : "") + L_.trophies[i];
        v.push_back("Your trophies: " + (t.empty() ? std::string("none yet") : t) + ".");
        const double est = L_.rec.estimate();
        v.push_back(std::string("How the crew see your bidding: ") + (L_.rec.bids < 5 ? "they haven't made up their minds." : est > .3 ? "a terrible bluffer. The sharp ones will call you." : est > .18 ? "a bit of a bluffer." : "straight as a mast. They believe you."));
        return v;
    }
    return {};
}

int DiceView::logbook_cols() const { return overlay_box().w >= 380 ? 2 : 1; }
int DiceView::logbook_rows() const {
    const Box b = overlay_box();
    const int rh = text_h("Mg", 9.5, 1) + 3;
    return std::clamp((b.h - 46) / rh, 1, 16);
}

// one wager's words: who sits down, what a loss costs, what a win brings. Returns nothing; see wager_card_h for the height.
int DiceView::wager_card_h(const Wager& w, int cw) const {
    int h = 8 + 18 + 16 + 13 + static_cast<int>(w.seats.size()) * 24 + 4;
    const std::string lose = "Lose: " + years_words(w.years_lose) + (w.forfeit.empty() ? "" : ", and " + w.forfeit + " goes in the Keeper's jar") + ".";
    const std::string win = "Win: " + years(w.years_win) + " struck off" + (w.prize.empty() ? "" : ", and " + prize_words(w.prize)) + ".";
    h += text_h(lose, 10, 0, cw - 20) + 5 + text_h(win, 10, 0, cw - 20);
    return h;
}

void DiceView::draw_button(const Button& b) {
    const bool down = pressed_ == b.id, over = hover_ == b.id && b.enabled;
    if (b.style == 2) {
        const int f = std::atoi(b.label.c_str());
        const bool sel = sel_face_ == f;
        if (sel) { frame_.begin(); frame_.rrect(b.x - 2, b.y - 2, b.w + 4, b.h + 4, 4); frame_.fill(b.enabled ? kBrass : hex(0x6A6A5A)); }
        draw_die_icon(b.x, b.y + (down ? 1 : 0), b.w, f, b.enabled ? (over ? hex(0xFFFFF4) : hex(0xF0E8D4)) : hex(0x8A8A80), kInk);
        return;
    }
    if (b.style == 4) {
        // a wager tab: its title and its skulls
        const int i = std::atoi(b.id.c_str() + 3);
        const bool sel = i == sel_wager_;
        frame_.begin(); frame_.rrect(b.x, b.y, b.w, b.h, 4); frame_.fill(sel ? kParch : over ? hex(0xD8CCAA) : hex(0xB8AA88));
        frame_.begin(); frame_.rrect(b.x + .5, b.y + .5, b.w - 1, b.h - 1, 4); frame_.stroke(i == 2 ? kRed : kBrass, sel ? 1.5 : 1);
        std::string t = b.label;
        while (t.size() > 4 && text_w(t, 10.5, 2) > b.w - 8) t = t.substr(0, t.size() - 2);
        if (t != b.label) t += ".";
        text(t, b.x + (b.w - text_w(t, 10.5, 2)) / 2, b.y + 3, kInk, 10.5, 2);
        const int tier = offers_[static_cast<size_t>(i)].tier;
        for (int k = 0; k < tier; ++k) {
            const int sx = b.x + b.w / 2 - tier * 6 + k * 12 + 1;
            frame_.fill_circle(sx + 5, b.y + 20, 3.8, kInk);
            frame_.fill_rect(sx + 2.5, b.y + 22, 5, 3, kInk);
        }
        return;
    }
    Col face = !b.enabled ? hex(0x6A6A62) : over ? hex(0xFFF6DA) : kParchDark;
    Col ink = b.enabled ? kInk : hex(0x3A3A36);
    if (b.style == 1) { face = !b.enabled ? hex(0x5A3A3A) : over ? hex(0xE85050) : kRed; ink = b.enabled ? hex(0xFFF4E0) : hex(0x9A8080); }
    frame_.fill_rect(b.x + 1, b.y + 2, b.w, b.h, hex(0x000000, .35f));
    frame_.begin(); frame_.rrect(b.x, b.y + (down ? 1 : 0), b.w, b.h, 3); frame_.fill(face);
    frame_.begin(); frame_.rrect(b.x + .5, b.y + .5 + (down ? 1 : 0), b.w - 1, b.h - 1, 3); frame_.stroke(b.style == 1 ? hex(0x6A1010) : kBrass, 1);
    const double size = b.style == 1 ? 15 : 11;
    const int font = b.style == 1 ? 2 : 0;
    const int tw = text_w(b.label, size, font), th = text_h(b.label, size, font);
    text(b.label, b.x + std::max(4, (b.w - tw) / 2), b.y + (b.h - th) / 2 + (down ? 1 : 0), ink, size, font);
}

void DiceView::draw_plates() {
    // under each opponent: their name, and a die for every die they still have
    const int n = std::min(static_cast<int>(st_.crew.size()), m().players);
    std::vector<double> xs(static_cast<size_t>(std::max(0, n))), ys(xs.size());
    for (int s = 1; s < n; ++s) {
        const CrewPose& p = st_.crew[static_cast<size_t>(s)];
        cab_.to_screen(V3{p.pos.x * .74, p.pos.y * .74, Cabin::kTop + .02}, xs[static_cast<size_t>(s)], ys[static_cast<size_t>(s)]);
    }
    // a small table squeezes the plates: each may be only as wide as the gap to its neighbours
    int room = 1000;
    for (int s = 1; s < n; ++s)
        for (int t = s + 1; t < n; ++t)
            if (std::abs(ys[static_cast<size_t>(s)] - ys[static_cast<size_t>(t)]) < 24) room = std::min(room, static_cast<int>(std::abs(xs[static_cast<size_t>(s)] - xs[static_cast<size_t>(t)])) - 4);
    const bool tight = room < kStartDice * 9 + 10 || room < 80;
    const double fs = tight ? 9 : 10.5;
    const int step = tight ? 6 : 9, die = tight ? 5 : 7, plate_h = tight ? 17 : 22;
    for (int s = 1; s < n; ++s) {
        double x = xs[static_cast<size_t>(s)], y = ys[static_cast<size_t>(s)];
        y -= 4;
        std::string nm = ch(s).name;
        const int maxw = std::max(kStartDice * step + 6, room);
        if (text_w(nm, fs, 1) + 8 > maxw) {
            // the name that fits: the last word ("Squidge", "Mackenzie"), cut if need be
            const size_t sp = nm.rfind(' ');
            if (sp != std::string::npos) nm = nm.substr(sp + 1);
            while (nm.size() > 3 && text_w(nm, fs, 1) + 8 > maxw) nm.pop_back();
        }
        const int w = std::min(maxw, std::max(text_w(nm, fs, 1), kStartDice * step) + 8);
        const bool turn = (phase_ == Phase::turn || phase_ == Phase::act) && m().turn == s;
        const bool out = !m().alive(s);
        const int px = static_cast<int>(x) - w / 2, py = static_cast<int>(y) - plate_h;
        frame_.fill_rect(px + 1, py + 1, w, plate_h, hex(0x000000, .35f));
        frame_.fill_rect(px, py, w, plate_h, out ? hex(0x2A3438, .8f) : turn ? hex(0x4A3A10, .92f) : hex(0x10202A, .85f));
        frame_.begin(); frame_.rect(px + .5, py + .5, w - 1, plate_h - 1); frame_.stroke(turn ? kBrass : hex(0x5A7A80), 1);
        text(nm, px + (w - text_w(nm, fs, 1)) / 2, py + 1, out ? hex(0x7A8A8A) : turn ? hex(0xFFE8A0) : kFoam, fs, 1);
        const int nd = m().count[static_cast<size_t>(s)];
        const int dx = px + (w - kStartDice * step) / 2, dy = py + plate_h - die - 2;
        for (int k = 0; k < kStartDice; ++k) {
            if (k < nd) draw_die_icon(dx + k * step, dy, die, 0, hex(0xE8E0CC), kInk);
            else frame_.fill_rect(dx + k * step + 1, dy + die / 2, die - 2, 1, hex(0x5A6A70));
        }
        if (turn && phase_ == Phase::turn) {
            const int dots = static_cast<int>(t_ * 3) % 4;
            text(std::string(static_cast<size_t>(dots), '.'), px + w + 2, py + 2, kBrass, 12, 1);
        }
    }
}

void DiceView::draw_bid_plaque() {
    if (phase_ == Phase::wagers || phase_ == Phase::greet || phase_ == Phase::shake) return;
    const Bid b = phase_ == Phase::reveal || phase_ == Phase::result || phase_ == Phase::freedom ? rv_.bid : m().bid;
    const int who = phase_ == Phase::reveal || phase_ == Phase::result || phase_ == Phase::freedom ? rv_.bidder : m().bidder;
    int corner = 0;  // the small buttons in the top corner, in two-row mode
    for (const Button& bt : buttons_) if (bt.y < 30 && bt.style == 0) corner = std::max(corner, pw_ - bt.x);
    if (!wide_) {
        // two-row mode: the bid is one line in the top band, the count and verdict just beneath it
        frame_.fill_rect(0, 0, pw_, top_h_, hex(0x0A1A20, .9f));
        frame_.fill_rect(0, top_h_ - 1, pw_, 1, kBrass);
        const int right = pw_ - corner - 8;
        if (b.none()) {
            text(m().turn == 0 ? "No bid yet. You open." : "No bid yet.", 8, 8, kFoam, 11, 1);
        } else {
            const std::string q = std::to_string(b.qty) + " x";
            text(q, 8, 5, hex(0xFFE8A0), 15, 2);
            const int qx = 8 + text_w(q, 15, 2) + 4;
            draw_die_icon(qx, 5, 17, b.face, hex(0xF4ECD8), kInk);
            std::string by = who == 0 ? "your bid" : who > 0 ? ch(who).name : "";
            const int bx = qx + 23;
            if (bx + text_w(by, 10.5, 1) > right) by = who == 0 ? "you" : by.substr(0, std::max<size_t>(3, by.size() / 2)) + ".";
            text(by, bx, 9, kFoam, 10.5, 1);
        }
        if (phase_ == Phase::reveal && counted_ > 0) {
            const std::string c = std::to_string(counted_);
            text(c, (pw_ - text_w(c, 22, 2)) / 2, top_h_ + 2, counted_ >= rv_.bid.qty ? hex(0x80F0A0) : hex(0xFFE8A0), 22, 2);
        }
        if (phase_ == Phase::reveal && !result_line_.empty()) {
            // the verdict sits at the foot of the scene, clear of the crew's plates
            const int tw = std::min(pw_ - 8, text_w(result_line_, 12, 1) + 16);
            const int th = text_h(result_line_, 12, 1, tw - 16) + 4;
            const int ry = ph_ - panel_h_ - th - 4;
            frame_.fill_rect((pw_ - tw) / 2, ry, tw, th, rv_.stood ? hex(0x1E4A2A, .9f) : hex(0x5A1A1A, .9f));
            text(result_line_, (pw_ - tw) / 2 + 8, ry + 2, hex(0xFFF4E0), 12, 1, tw - 16);
        }
        return;
    }
    const int w = std::min(220, pw_ - 12 - corner - 6), x = 6, y = 6;
    frame_.fill_rect(x + 2, y + 2, w, 34, hex(0x000000, .4f));
    frame_.begin(); frame_.rrect(x, y, w, 34, 5); frame_.fill(hex(0x10242A, .92f));
    frame_.begin(); frame_.rrect(x + .5, y + .5, w - 1, 33, 5); frame_.stroke(kBrass, 1);
    if (b.none()) {
        const std::string s = m().turn == 0 ? "No bid yet. You open." : "No bid yet.";
        text(s, x + (w - text_w(s, 12, 1)) / 2, y + 10, kFoam, 12, 1);
    } else {
        // the bid: "7 x [four]", who made it, and how many dice there are
        const std::string q = std::to_string(b.qty) + " x";
        text(q, x + 10, y + 5, hex(0xFFE8A0), 20, 2);
        draw_die_icon(x + 14 + text_w(q, 20, 2), y + 6, 22, b.face, hex(0xF4ECD8), kInk);
        std::string by = who == 0 ? "your bid" : who > 0 ? ch(who).name + "'s bid" : "";
        if (x + 76 + text_w(by, 10.5, 1) > x + w - 4 && who > 0) by = ch(who).name;
        text(by, x + 76, y + 4, kFoam, 10.5, 1);
        const int td = phase_ == Phase::reveal ? [&] { int k = 0; for (const auto& d : st_.dice) k += static_cast<int>(d.size()); return k + (sunk_ ? 1 : 0); }() : m().total_dice();
        std::string dl = std::to_string(td) + " dice on the table; ones are wild";
        if (x + 76 + text_w(dl, 9.5, 0) > x + w - 4) dl = std::to_string(td) + " dice; ones wild";
        text(dl, x + 76, y + 18, hex(0x9AB8B8), 9.5, 0);
    }
    // the reveal's count
    if (phase_ == Phase::reveal && counted_ > 0) {
        const std::string c = std::to_string(counted_);
        const Col cc = counted_ >= rv_.bid.qty ? hex(0x80F0A0) : hex(0xFFE8A0);
        text(c, (pw_ - text_w(c, 30, 2)) / 2, 8, cc, 30, 2);
    }
    if (phase_ == Phase::reveal && !result_line_.empty()) {
        const int tw = text_w(result_line_, 13, 1) + 20;
        frame_.fill_rect((pw_ - tw) / 2, 52, tw, 20, rv_.stood ? hex(0x1E4A2A, .9f) : hex(0x5A1A1A, .9f));
        text(result_line_, (pw_ - tw) / 2 + 10, 54, hex(0xFFF4E0), 13, 1);
    }
    // this round's bids so far, small, under the plaque
    int hy = 46;
    const auto& hist = m().history;
    if (phase_ == Phase::turn || phase_ == Phase::act) {
        const size_t fit = static_cast<size_t>(std::clamp((ph_ - panel_h_ - hy - 8) / 12, 0, 6));
        const size_t from = hist.size() > fit ? hist.size() - fit : 0;
        for (size_t i = from; i < hist.size(); ++i) {
            const std::string who2 = hist[i].who == 0 ? "You" : ch(hist[i].who).name;
            const std::string s = who2 + ": " + bid_words(hist[i].bid);
            frame_.fill_rect(6, hy - 1, text_w(s, 9.5, 0) + 8, 12, hex(0x000000, .35f));
            text(s, 10, hy, i + 1 == hist.size() ? hex(0xFFE8A0) : hex(0xA8C0C0), 9.5, 0);
            hy += 12;
        }
    }
}

void DiceView::draw_panel_match() {
    const int y0 = ph_ - panel_h_;
    frame_.fill_rect(0, y0, pw_, panel_h_, hex(0x0A1A20, .96f));
    frame_.fill_rect(0, y0, pw_, 1, kBrass);
    const bool mine = phase_ == Phase::turn && m().turn == 0 && !auto_;
    const bool showing = phase_ == Phase::reveal || phase_ == Phase::result || phase_ == Phase::freedom;
    // your dice (in two rows the label goes, the dice stay)
    const int ds = wide_ ? 22 : 20, step = wide_ ? 25 : 22, dy = wide_ ? y0 + 22 : y0 + 7;
    if (wide_) text("Your dice", 12, y0 + 6, kFoam, 11, 1);
    if (showing) {
        int k = 0;
        for (const DieShow& d : st_.dice[0]) draw_die_icon(10 + (k++) * step, dy, ds, d.value, d.fade > .5 ? hex(0x8A8A80) : hex(0xF4ECD8), kInk, d.glow > .5);
    } else if (phase_ != Phase::shake && phase_ != Phase::greet) {
        int k = 0;
        for (int v : m().dice[0]) {
            const bool match = phase_ == Phase::turn && m().turn == 0 && (v == sel_face_ || v == 1);
            draw_die_icon(10 + (k++) * step, dy, ds, v, hex(0xF4ECD8), kInk, match);
        }
    } else if (wide_) {
        text(phase_ == Phase::shake ? "Shaking..." : "", 12, y0 + 28, hex(0x9AB8B8), 11, 0);
    }
    const int ld = kStartDice - m().count[0];
    const std::string lost = "Lost " + std::to_string(ld) + " to the jar";
    const std::string st = "At stake: " + years(L_.wager.years_lose) + " if you lose; " + years(L_.wager.years_win) + " off if you win.";
    if (wide_) {
        if (ld > 0) text(lost, 12, y0 + 50, hex(0x9AB8B8), 9.5, 0);
        const int cx = 150;
        frame_.fill_rect(cx, y0 + 6, 1, panel_h_ - 12, hex(0x2A4A50));
        if (mine) {
            text("Your bid", cx + 28, y0 + 6, kFoam, 11, 1);
            const std::string q = std::to_string(sel_qty_);
            frame_.fill_rect(cx + 48, y0 + 36, 26, 20, hex(0xF0E8D4));
            text(q, cx + 48 + (26 - text_w(q, 14, 2)) / 2, y0 + 38, kInk, 14, 2);
            int hold = 0;
            for (int v : m().dice[0]) hold += v == sel_face_ || v == 1;
            const int hidden = m().total_dice() - m().count[0];
            text("You hold " + std::to_string(hold) + " (ones count); " + std::to_string(hidden) + " dice are hidden.", cx + 28, y0 + 20, hex(0x9AB8B8), 9.5, 0);
        } else {
            const std::string w = !status_.empty() ? status_ : phase_ == Phase::reveal ? "Counting..." : phase_ == Phase::shake ? "Shaking the cups..." : "";
            text(w, cx + 28, y0 + 30, kFoam, 14, 1, pw_ - 120 - cx - 36);
        }
        text(st, cx + 28, y0 + panel_h_ - 16, hex(0x7A9AA0), 9, 0);
        return;
    }
    // two rows: dice and Liar! above, the composer (or what's happening) below, a line of small print
    const int dice_end = 10 + kStartDice * step + 4;
    if (ld > 0 && !showing) text(lost, dice_end, y0 + 11, hex(0x9AB8B8), 9, 0, std::max(30, pw_ - 8 - 84 - 6 - dice_end));
    const int ry = y0 + 34;
    if (mine) {
        const std::string q = std::to_string(sel_qty_);
        frame_.fill_rect(30, ry, 24, 20, hex(0xF0E8D4));
        text(q, 30 + (24 - text_w(q, 14, 2)) / 2, ry + 2, kInk, 14, 2);
        int hold = 0;
        for (int v : m().dice[0]) hold += v == sel_face_ || v == 1;
        const int hidden = m().total_dice() - m().count[0];
        text("You hold " + std::to_string(hold) + " (ones count); " + std::to_string(hidden) + " hidden.", 10, ry + 24, hex(0x9AB8B8), 9, 0, pw_ - 20);
    } else {
        const std::string w = !status_.empty() ? status_ : phase_ == Phase::reveal ? "Counting..." : phase_ == Phase::shake ? "Shaking the cups..." : "";
        text(w, 10, ry + 2, kFoam, 12, 1, pw_ - 20);
        text(st, 10, ry + 24, hex(0x7A9AA0), 9, 0, pw_ - 20);
    }
}

void DiceView::wager_details(const Wager& w, int x, int y, int cw, bool brief) {
    // who sits down, then what a loss costs and what a win brings
    text("At the table:", x, y, hex(0x5A4A30), 10, 1);
    y += 13;
    for (int s : w.seats) {
        const Character& c = cast()[static_cast<size_t>(s)];
        const CrewNotes& nt = L_.notes[static_cast<size_t>(s)];
        const std::string d = a_an(species_name(c.species)) + ", " + danger_word(c.danger) + (nt.games ? "" : " (new)");
        if (brief) {
            text(c.name + ", " + d, x + 4, y, kInk, 9.5, 0, cw - 4);
            y += text_h(c.name + ", " + d, 9.5, 0, cw - 4) + 1;
        } else {
            text(c.name, x + 4, y, kInk, 10.5, 1);
            y += 12;
            text(d, x + 8, y, hex(0x5A5040), 9, 0);
            y += 12;
        }
    }
}

void DiceView::draw_wagers() {
    // the Keeper's barrel: three wagers
    frame_.fill_rect(0, 0, pw_, ph_, hex(0x041014, .45f));
    const std::string owed = L_.owed > 0 ? "You owe the locker " + years(L_.owed) + "." : "Your debt is paid. You are free.";
    bool keeper = false;  // when tabbed, the Keeper's words sit over the header for as long as he speaks
    for (const Bubble& bb : bubbles_) keeper |= bb.seat < 0;
    const bool header = !(tabbed_ && keeper);
    const double os = tabbed_ ? 11 : 13;
    const int ow = text_w(owed, os, 1);
    const double ts = tabbed_ ? 15 : 22;
    if (header) {
        text(owed, pw_ - 10 - ow, tabbed_ ? 8 : 10, L_.owed > 0 ? hex(0xFFC8A0) : hex(0x90F0B0), os, 1);
        if (text_w("Liar's Dice", ts, 2) + 24 < pw_ - ow - 10) text("Liar's Dice", 12, tabbed_ ? 5 : 8, hex(0xFFE8A0), ts, 2);
    }
    if (!tabbed_) {
        text("in Davy Jones' locker", 16, 32, kFoam, 11, 0);
        if (!L_.jar.empty()) {
            std::string j = "In the Keeper's jar: ";
            for (size_t i = 0; i < L_.jar.size(); ++i) j += (i ? ", " : "") + L_.jar[i];
            text(j, pw_ - 14 - std::min(pw_ / 2, text_w(j, 9.5, 0)), 28, hex(0xA8C0C0), 9.5, 0, pw_ / 2);
        }
    }
    auto stakes = [&](const Wager& w, int x, int y, int cw) {
        const std::string lose = "Lose: " + years_words(w.years_lose) + (w.forfeit.empty() ? "" : ", and " + w.forfeit + " goes in the Keeper's jar") + ".";
        text(lose, x, y, hex(0x8A2020), 10, 0, cw);
        y += text_h(lose, 10, 0, cw) + 5;
        const std::string win = "Win: " + years(w.years_win) + " struck off" + (w.prize.empty() ? "" : ", and " + prize_words(w.prize)) + ".";
        text(win, x, y, hex(0x1E6A30), 10, 0, cw);
    };
    if (tabbed_) {
        if (offers_.empty()) return;
        const Wager& w = offers_[static_cast<size_t>(sel_wager_)];
        const int cy = 62, ch = ph_ - (opt_.hosted ? 4 : 24) - 6 - cy;
        const int cw = std::min(pw_ - 16, 460), cx = (pw_ - cw) / 2;
        frame_.fill_rect(cx + 3, cy + 3, cw, ch, hex(0x000000, .4f));
        frame_.begin(); frame_.rrect(cx, cy, cw, ch, 6); frame_.fill(kParch);
        frame_.begin(); frame_.rrect(cx + .5, cy + .5, cw - 1, ch - 1, 6); frame_.stroke(sel_wager_ == 2 ? kRed : kBrass, 1.5);
        // side by side when the card is wide enough, else one above the other
        if (cw >= 260) {
            const int half = (cw - 30) / 2;
            if (ch < 120) {
                // In the compact card, names remain complete; species and tells
                // are available in the capsule's Crew panel. Stakes never hide.
                int y = cy + 7;
                text("At the table:", cx + 10, y, hex(0x5A4A30), 10, 1);
                y += 12;
                for (int seat : w.seats) {
                    const std::string name = cast()[static_cast<size_t>(seat)].name;
                    text(name, cx + 10, y, kInk, 9.5, 0, half);
                    y += text_h(name, 9.5, 0, half) + 2;
                }
            } else wager_details(w, cx + 10, cy + 7, half, true);
            stakes(w, cx + 20 + half, cy + 7, half);
        } else {
            wager_details(w, cx + 10, cy + 7, cw - 20, true);
            stakes(w, cx + 10, cy + 7 + 13 + static_cast<int>(w.seats.size()) * 12 + 4, cw - 20);
        }
        return;
    }
    const int cw = std::min(190, (pw_ - 40) / 3), gap = (pw_ - cw * 3) / 4;
    int need = 0;
    for (const Wager& w : offers_) need = std::max(need, wager_card_h(w, cw));
    const int cy = 64, chh = std::min(ph_ - 24 - 8 - cy, std::max(need + 30, 150));
    for (int i = 0; i < static_cast<int>(offers_.size()); ++i) {
        const Wager& w = offers_[static_cast<size_t>(i)];
        const int x = gap + i * (cw + gap);
        const bool over = hover_ == "take" + std::to_string(i);
        frame_.fill_rect(x + 3, cy + 3, cw, chh, hex(0x000000, .4f));
        frame_.begin(); frame_.rrect(x, cy, cw, chh, 6); frame_.fill(over ? hex(0xF8EED4) : kParch);
        frame_.begin(); frame_.rrect(x + .5, cy + .5, cw - 1, chh - 1, 6); frame_.stroke(i == 2 ? kRed : kBrass, 1.5);
        int y = cy + 8;
        text(w.title, x + (cw - text_w(w.title, 13, 2)) / 2, y, kInk, 13, 2);
        y += 18;
        for (int k = 0; k < w.tier; ++k) {
            const int sx = x + cw / 2 - w.tier * 7 + k * 14 + 2;
            frame_.fill_circle(sx + 5, y + 4, 4.5, kInk);
            frame_.fill_rect(sx + 2, y + 7, 7, 4, kInk);
            frame_.fill_circle(sx + 3.5, y + 4, 1.3, kParch);
            frame_.fill_circle(sx + 6.5, y + 4, 1.3, kParch);
        }
        y += 16;
        wager_details(w, x + 10, y, cw - 20, false);
        y += 13 + static_cast<int>(w.seats.size()) * 24 + 4;
        stakes(w, x + 10, y, cw - 20);
    }
}

void DiceView::draw_bubbles() {
    for (const Bubble& b : bubbles_) {
        const int wrap = std::min(b.seat < 0 ? 300 : 150, pw_ - 30);
        const double size = b.seat < 0 ? 12 : 11;
        const std::string s = b.text.substr(0, static_cast<size_t>(std::max(1, b.shown)));
        const int font = b.seat < 0 ? 1 : 0;
        const int tw = std::min(wrap, text_w(b.text, size, font)), th = text_h(b.text, size, font, wrap);
        const int bw = tw + 14, bh = th + 8;
        int bx, by;
        double hx = pw_ / 2.0, hy = 40;
        if (b.seat > 0 && b.seat < static_cast<int>(st_.crew.size())) {
            cab_.to_screen(crew_head(st_.crew[static_cast<size_t>(b.seat)]) + V3{0, 0, .2}, hx, hy);
            bx = static_cast<int>(hx - bw / 2.0);
            by = static_cast<int>(hy - bh - 30);
        } else if (b.seat == 0) {
            bx = (pw_ - bw) / 2;
            by = ph_ - panel_h_ - bh - (wide_ ? 46 : 14);
            hx = pw_ / 2.0;
            hy = ph_ - panel_h_;
        } else {
            bx = (pw_ - bw) / 2;
            by = phase_ == Phase::wagers ? 44 : 46;
            if (phase_ == Phase::wagers) by = tabbed_ ? 2 : ph_ - 24 - 6 - bh;
        }
        bx = std::clamp(bx, 4, pw_ - bw - 4);
        // in two-row mode the top band (the bid, the corner buttons) stays clear
        const int ceiling = phase_ == Phase::wagers ? 4 : top_h_ + 2;
        by = std::clamp(by, ceiling, std::max(ceiling, ph_ - bh - 4));
        const float a = static_cast<float>(std::clamp((b.life - b.age) * 3, 0.0, 1.0));
        Col fill = b.seat < 0 ? hex(0x0A2A2A, .94f * a) : b.seat == 0 ? hex(0xFFF0C8, a) : hex(0xF4FAF8, a);
        if (b.seat > 0) {
            frame_.begin();
            const double ax = std::clamp(hx, bx + 10.0, bx + bw - 10.0);
            frame_.move(ax - 5, by + bh - 1); frame_.line(hx, by + bh + 12); frame_.line(ax + 5, by + bh - 1); frame_.close(); frame_.fill(fill);
        }
        frame_.begin(); frame_.rrect(bx, by, bw, bh, 7); frame_.fill(fill);
        frame_.begin(); frame_.rrect(bx + .5, by + .5, bw - 1, bh - 1, 7); frame_.stroke(alpha(b.seat < 0 ? hex(0x60E0C0) : kInk, a * .8f), 1);
        text(s, bx + 7, by + 4, alpha(b.seat < 0 ? hex(0xC8FFF0) : kInk, a), size, font, wrap);
    }
}

void DiceView::draw_overlay_panel() {
    if (panel_ == Panel::none) return;
    frame_.fill_rect(0, 0, pw_, ph_, hex(0x000000, .55f));
    const Box bx = overlay_box();
    const int ww = bx.w, wh = bx.h, wx = bx.x, wy = bx.y;
    frame_.begin(); frame_.rrect(wx, wy, ww, wh, 6); frame_.fill(kParch);
    frame_.begin(); frame_.rrect(wx + .5, wy + .5, ww - 1, wh - 1, 6); frame_.stroke(kBrass, 1.5);
    auto title = [&](const std::string& s) { text(s, wx + (ww - text_w(s, 15, 2)) / 2, wy + 8, kInk, 15, 2); };
    if (panel_ == Panel::help || panel_ == Panel::records) {
        title(panel_ == Panel::help ? "How to play" : "Records");
        int ly = wy + 30;
        const std::vector<std::string> lines = overlay_lines();
        for (size_t i = 0; i < lines.size(); ++i) {
            const bool keys = panel_ == Panel::help && i + 1 == lines.size();
            const double size = keys ? ov_size_ - 1 : ov_size_;
            text(lines[i], wx + 14, ly, keys ? hex(0x6A5A40) : kInk, size, keys ? 1 : 0, ww - 28);
            ly += text_h(lines[i], size, keys ? 1 : 0, ww - 28) + 4;
        }
        return;
    }
    title("The crew logbook");
    const int cols = logbook_cols(), rows = logbook_rows(), per = cols * rows;
    const int colw = (ww - 28 - (cols - 1) * 8) / cols;
    int met = 0;
    for (int i = 0; i < 32; ++i) met += L_.notes[static_cast<size_t>(i)].games > 0;
    const std::string head = "Met " + std::to_string(met) + " of 32. A tell is noted once you've caught it three times.";
    text(head, wx + 14, wy + 28, hex(0x6A5A40), 9.5, 0, ww - 28);
    const int rh = text_h("Mg", 9.5, 1) + 3;
    for (int k = 0; k < per; ++k) {
        const int i = log_page_ * per + k;
        if (i >= 32) break;
        const Character& c = cast()[static_cast<size_t>(i)];
        const CrewNotes& n = L_.notes[static_cast<size_t>(i)];
        const int col = k / rows, row = k % rows;
        const int x = wx + 14 + col * (colw + 8), y = wy + 42 + row * rh;
        if (n.games == 0) { text("? ? ?   (" + a_an(species_name(c.species)) + ")", x, y, hex(0x9A8A70), 9.5, 0); continue; }
        text(c.name, x, y, kInk, 9.5, 1);
        std::string s = "won " + std::to_string(n.beaten) + " of " + std::to_string(n.games);
        if (n.bids_seen) s += ", bluffed " + std::to_string(n.bluffs_seen) + " of " + std::to_string(n.bids_seen);
        s += n.tells_caught >= 3 ? std::string("; tell: ") + tell_name(c.tell) : "";
        text(s, x + 6 + text_w(c.name, 9.5, 1), y, hex(0x5A5040), 9, 0, colw - 10 - text_w(c.name, 9.5, 1));
    }
}

void DiceView::compose() {
    texts_.clear();
    cab_.r.present(frame_, 1, 0, 0, true);
    if (phase_ == Phase::wagers) {
        draw_wagers();
    } else {
        draw_plates();
        draw_bid_plaque();
        draw_panel_match();
        if (phase_ == Phase::result || phase_ == Phase::freedom) {
            const std::string s = phase_ == Phase::freedom ? "Your debt is paid. You're free!" : won_ ? "You win the wager!" : "You lose the wager.";
            const int tw = text_w(s, 20, 2) + 30;
            const int y = ph_ / 2 - 60;
            frame_.fill_rect((pw_ - tw) / 2, y, tw, 34, won_ ? hex(0x1E4A2A, .92f) : hex(0x4A1A1A, .92f));
            frame_.begin(); frame_.rect((pw_ - tw) / 2 + .5, y + .5, tw - 1, 33); frame_.stroke(kBrass, 1);
            text(s, (pw_ - tw) / 2 + 15, y + 6, hex(0xFFF4E0), 20, 2);
        }
    }
    draw_bubbles();
    if (panel_ != Panel::none) {
        // a panel covers the table: none of the table's crisp text may show through it
        texts_.clear();
        draw_overlay_panel();
    }
    for (const Button& b : buttons_) draw_button(b);
}

void DiceView::blit_texts(std::uint32_t* dst, size_t stride_px, double k) {
    const int sc = std::max(1, static_cast<int>(std::lround(bs_)));
    for (const HiText& h : texts_) {
        const Mask& m = tmask(h.s, h.font, h.size, h.wrap);
        const int ox = static_cast<int>(h.x * k), oy = static_cast<int>(h.y * k);
        const float pr = h.c.r * h.c.a, pg = h.c.g * h.c.a, pb = h.c.b * h.c.a;
        for (int my = 0; my < m.h * sc; ++my) {
            const int dy = oy + my;
            if (dy < 0 || dy >= phys_h_) continue;
            const std::uint8_t* srow = m.a.data() + static_cast<size_t>(my / sc) * m.w;
            std::uint32_t* drow = dst + static_cast<size_t>(dy) * stride_px;
            for (int mx = 0; mx < m.w * sc; ++mx) {
                const int dx = ox + mx;
                if (dx < 0 || dx >= phys_w_) continue;
                const float cov = srow[mx / sc] * (1.f / 255.f);
                if (cov <= 0) continue;
                const std::uint32_t d = drow[dx];
                const float a = h.c.a * cov, kk = 1 - a;
                const auto chn = [&](int sh, float src) { return static_cast<std::uint32_t>(std::min(255.f, src * cov * 255 + static_cast<float>((d >> sh) & 255) * kk + .5f)) << sh; };
                drow[dx] = chn(0, pb) | chn(8, pg) | chn(16, pr) | (0xFFu << 24);
            }
        }
    }
}

}  // namespace ld

namespace ld {
void DiceView::set_cabinet(bool foreground, bool music, bool sound, bool reduced) {
    cab_front_ = foreground; cab_music_ = music; cab_sound_ = sound; cab_reduced_ = reduced;
    audio_cabinet(foreground, music, sound);
    if (!foreground) { pressed_.clear(); }
}
}

namespace ld {
void DiceView::layout_buttons() {
    layout_game_buttons();
    if (!opt_.hosted) return;
    for (std::size_t i = buttons_.size(); i > 0; --i) {
        const std::string& id = buttons_[i-1].id;
        if (id == "help" || id == "logbook" || id == "records" || id == "music") buttons_.erase(buttons_.begin() + static_cast<std::ptrdiff_t>(i-1));
    }
}
std::vector<games::GameCommand> DiceView::commands() const { return {{"help", "Help", true, panel_ == Panel::help}, {"logbook", "Crew", true, panel_ == Panel::logbook}, {"records", "Records", true, panel_ == Panel::records}}; }
void DiceView::run_command(std::string_view id) {
    for (const games::GameCommand& cmd : commands()) {
        if (cmd.id == id && cmd.enabled) { action(cmd.checked ? "close" : std::string(id)); return; }
    }
}
}
