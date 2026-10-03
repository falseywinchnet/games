#include "koikoi.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace games::koi {

namespace {
constexpr Type B = Type::bright, A = Type::animal, R = Type::ribbon, P = Type::plain;
const CardInfo kCards[48] = {
    {0, B, Ribbon::none, "Crane and Sun"}, {0, R, Ribbon::poem, "Pine Poem Ribbon"}, {0, P, Ribbon::none, "Pine"}, {0, P, Ribbon::none, "Pine"},
    {1, A, Ribbon::none, "Bush Warbler"}, {1, R, Ribbon::poem, "Plum Poem Ribbon"}, {1, P, Ribbon::none, "Plum"}, {1, P, Ribbon::none, "Plum"},
    {2, B, Ribbon::none, "Curtain"}, {2, R, Ribbon::poem, "Cherry Poem Ribbon"}, {2, P, Ribbon::none, "Cherry"}, {2, P, Ribbon::none, "Cherry"},
    {3, A, Ribbon::none, "Cuckoo"}, {3, R, Ribbon::red, "Wisteria Ribbon"}, {3, P, Ribbon::none, "Wisteria"}, {3, P, Ribbon::none, "Wisteria"},
    {4, A, Ribbon::none, "Eight-Plank Bridge"}, {4, R, Ribbon::red, "Iris Ribbon"}, {4, P, Ribbon::none, "Iris"}, {4, P, Ribbon::none, "Iris"},
    {5, A, Ribbon::none, "Butterflies"}, {5, R, Ribbon::blue, "Peony Blue Ribbon"}, {5, P, Ribbon::none, "Peony"}, {5, P, Ribbon::none, "Peony"},
    {6, A, Ribbon::none, "Boar"}, {6, R, Ribbon::red, "Bush Clover Ribbon"}, {6, P, Ribbon::none, "Bush Clover"}, {6, P, Ribbon::none, "Bush Clover"},
    {7, B, Ribbon::none, "Full Moon"}, {7, A, Ribbon::none, "Geese"}, {7, P, Ribbon::none, "Pampas Grass"}, {7, P, Ribbon::none, "Pampas Grass"},
    {8, A, Ribbon::none, "Sake Cup"}, {8, R, Ribbon::blue, "Chrysanthemum Blue Ribbon"}, {8, P, Ribbon::none, "Chrysanthemum"}, {8, P, Ribbon::none, "Chrysanthemum"},
    {9, A, Ribbon::none, "Deer"}, {9, R, Ribbon::blue, "Maple Blue Ribbon"}, {9, P, Ribbon::none, "Maple"}, {9, P, Ribbon::none, "Maple"},
    {10, B, Ribbon::none, "Rain Man"}, {10, A, Ribbon::none, "Swallow"}, {10, R, Ribbon::red, "Willow Ribbon"}, {10, P, Ribbon::none, "Lightning"},
    {11, B, Ribbon::none, "Phoenix"}, {11, P, Ribbon::none, "Paulownia"}, {11, P, Ribbon::none, "Paulownia"}, {11, P, Ribbon::none, "Paulownia"},
};

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 0x6A09E667F3BCC909ULL) { if (!s) s = 1; next(); }
    std::uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    int range(int n) { return n <= 1 ? 0 : static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
};

bool has(const std::vector<int>& v, int c) { return std::find(v.begin(), v.end(), c) != v.end(); }
void erase(std::vector<int>& v, int c) { v.erase(std::remove(v.begin(), v.end(), c), v.end()); }
bool plain_like(int c) { return info(c).type == Type::plain || c == card::sake; }  // the sake cup also counts as a plain
bool animal(int c) { return info(c).type == Type::animal; }

std::uint64_t hash_state(const KoiState& s) {
    std::uint64_t h = s.seed * 1099511628211ULL + static_cast<std::uint64_t>(s.round) * 31 + static_cast<std::uint64_t>(s.turn);
    for (int c : s.field) h = (h ^ static_cast<std::uint64_t>(c + 1)) * 1099511628211ULL;
    for (int c : s.hand[0]) h = (h ^ static_cast<std::uint64_t>(c + 71)) * 1099511628211ULL;
    for (int c : s.hand[1]) h = (h ^ static_cast<std::uint64_t>(c + 151)) * 1099511628211ULL;
    return h;
}

void advance(KoiState& s);
void finish_capture(KoiState& s, int card, int target);
void after_draw(KoiState& s);

// a card arrives on the field (from a hand or the deck): match, choose, or lay it down
void land(KoiState& s, int card, bool from_hand) {
    const auto m = matches(s, card);
    auto& cap = s.captured[static_cast<size_t>(s.turn)];
    if (m.empty()) {
        s.field.push_back(card);
    } else if (m.size() == 1) {
        finish_capture(s, card, m[0]);
        return;
    } else if (m.size() == 2) {
        s.pending = card;
        s.choices = m;
        s.phase = from_hand ? Phase::choose_hand : Phase::choose_draw;
        return;
    } else {
        // three on the field: take all four
        cap.push_back(card);
        for (int c : m) { cap.push_back(c); erase(s.field, c); }
    }
    if (from_hand) s.phase = Phase::draw;
    else after_draw(s);
}

void finish_capture(KoiState& s, int card, int target) {
    auto& cap = s.captured[static_cast<size_t>(s.turn)];
    cap.push_back(card);
    cap.push_back(target);
    erase(s.field, target);
    const bool from_hand = !s.drawn_this_turn;
    s.pending = -1;
    s.choices.clear();
    if (from_hand) s.phase = Phase::draw;
    else after_draw(s);
}

// after the hand card: turn over the top of the draw pile
void advance(KoiState& s) {
    s.drawn_this_turn = true;
    if (s.deck.empty()) { after_draw(s); return; }
    const int d = s.deck.back();
    s.deck.pop_back();
    s.phase = Phase::play;
    land(s, d, false);
}

void end_round(KoiState& s, int winner, int points) {
    s.round_winner = winner;
    s.round_points = points;
    if (winner >= 0) {
        s.totals[static_cast<size_t>(winner)] += points;
        s.round_yaku = yaku(s.captured[static_cast<size_t>(winner)]);
    } else {
        s.round_yaku.clear();
    }
    s.rounds_won_points.push_back(winner < 0 ? 0 : winner == 0 ? points : -points);
    s.phase = Phase::round_over;
}

void pass_turn(KoiState& s) {
    if (s.hand[0].empty() && s.hand[1].empty()) {
        s.note = "No more cards: the round is a draw.";
        end_round(s, -1, 0);
        return;
    }
    s.turn = 1 - s.turn;
    s.drawn_this_turn = false;
    s.phase = Phase::play;
}

// after the drawn card has been placed: a new set?
void after_draw(KoiState& s) {
    s.pending = -1;
    s.choices.clear();
    const int pts = yaku_points(s.captured[static_cast<size_t>(s.turn)]);
    if (pts > s.banked[static_cast<size_t>(s.turn)]) {
        if (s.hand[static_cast<size_t>(s.turn)].empty()) {
            // no cards left to play on with: the round is theirs
            s.note = "A new set with the last card: the round is scored.";
            end_round(s, s.turn, round_value(s, s.turn));
            return;
        }
        s.phase = Phase::decide;
        return;
    }
    pass_turn(s);
}

// how promising a pile of captures is: sets made, and sets in the making (for the computer's judgement)
double potential(const std::vector<int>& cap, double greed) {
    int brights = 0, rain = 0, poem = 0, blue = 0, ibc = 0, animals = 0, ribbons = 0, plains = 0;
    bool curtain = false, moon = false, sake = false;
    for (int c : cap) {
        const CardInfo& i = info(c);
        if (i.type == Type::bright) { ++brights; rain += c == card::rain_man; }
        if (i.ribbon == Ribbon::poem) ++poem;
        if (i.ribbon == Ribbon::blue) ++blue;
        if (i.type == Type::ribbon) ++ribbons;
        if (animal(c)) ++animals;
        if (plain_like(c)) ++plains;
        ibc += c == card::boar || c == card::deer || c == card::butterflies;
        curtain = curtain || c == card::curtain;
        moon = moon || c == card::moon;
        sake = sake || c == card::sake;
    }
    double p = yaku_points(cap) * 14.0;
    p += (brights - rain) * (brights - rain) * (2.5 + 2 * greed) + rain * 1.5;
    p += poem * poem * (1.5 + greed) + blue * blue * (1.5 + greed) + ibc * ibc * (1.5 + greed);
    if (sake) p += (curtain || moon ? 8 : 4) * (.6 + greed * .6);
    if (curtain || moon) p += sake ? 0 : 2;
    p += animals * .8 + ribbons * .7 + plains * .35;
    return p;
}
}  // namespace

const CardInfo& info(int id) { return kCards[std::clamp(id, 0, 47)]; }

const char* month_name(int m) {
    static const char* n[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    return n[std::clamp(m, 0, 11)];
}
const char* plant_name(int m) {
    static const char* n[12] = {"Pine", "Plum", "Cherry", "Wisteria", "Iris", "Peony", "Bush Clover", "Pampas Grass", "Chrysanthemum", "Maple", "Willow", "Paulownia"};
    return n[std::clamp(m, 0, 11)];
}

std::vector<Yaku> yaku(const std::vector<int>& cap) {
    std::vector<Yaku> out;
    auto pick = [&](auto pred) { std::vector<int> v; for (int c : cap) if (pred(c)) v.push_back(c); return v; };
    const auto brights = pick([](int c) { return info(c).type == Type::bright; });
    const bool rain = has(cap, card::rain_man);
    const int nb = static_cast<int>(brights.size());
    if (nb == 5) out.push_back({"Five Brights", 10, brights});
    else if (nb == 4 && !rain) out.push_back({"Four Brights", 8, brights});
    else if (nb == 4) out.push_back({"Rainy Four Brights", 7, brights});
    else if (nb == 3 && !rain) out.push_back({"Three Brights", 5, brights});
    if (has(cap, card::curtain) && has(cap, card::sake)) out.push_back({"Viewing the Blossoms", 5, {card::curtain, card::sake}});
    if (has(cap, card::moon) && has(cap, card::sake)) out.push_back({"Viewing the Moon", 5, {card::moon, card::sake}});
    const auto animals = pick([](int c) { return animal(c); });
    if (has(cap, card::boar) && has(cap, card::deer) && has(cap, card::butterflies))
        out.push_back({"Boar, Deer and Butterflies", 5 + static_cast<int>(animals.size()) - 3, animals});
    if (animals.size() >= 5) out.push_back({"Animals", 1 + static_cast<int>(animals.size()) - 5, animals});
    const auto ribbons = pick([](int c) { return info(c).type == Type::ribbon; });
    const auto poem = pick([](int c) { return info(c).ribbon == Ribbon::poem; });
    const auto blue = pick([](int c) { return info(c).ribbon == Ribbon::blue; });
    const int extra = static_cast<int>(ribbons.size());
    if (poem.size() == 3 && blue.size() == 3) out.push_back({"Red and Blue Poem Ribbons", 10 + extra - 6, ribbons});
    else if (poem.size() == 3) out.push_back({"Red Poem Ribbons", 5 + extra - 3, ribbons});
    else if (blue.size() == 3) out.push_back({"Blue Ribbons", 5 + extra - 3, ribbons});
    if (ribbons.size() >= 5) out.push_back({"Ribbons", 1 + extra - 5, ribbons});
    const auto plains = pick([](int c) { return plain_like(c); });
    if (plains.size() >= 10) out.push_back({"Plains", 1 + static_cast<int>(plains.size()) - 10, plains});
    return out;
}

int yaku_points(const std::vector<int>& cap) {
    // the same sets as yaku(), counted without building their names (this runs constantly in the computer's thinking)
    int brights = 0, animals = 0, ribbons = 0, poem = 0, blue = 0, plains = 0;
    bool rain = false, curtain = false, moon = false, sake = false, boar = false, deer = false, fly = false;
    for (int c : cap) {
        const CardInfo& i = info(c);
        brights += i.type == Type::bright;
        animals += i.type == Type::animal;
        ribbons += i.type == Type::ribbon;
        poem += i.ribbon == Ribbon::poem;
        blue += i.ribbon == Ribbon::blue;
        plains += i.type == Type::plain || c == card::sake;
        rain = rain || c == card::rain_man;
        curtain = curtain || c == card::curtain;
        moon = moon || c == card::moon;
        sake = sake || c == card::sake;
        boar = boar || c == card::boar;
        deer = deer || c == card::deer;
        fly = fly || c == card::butterflies;
    }
    int p = 0;
    if (brights == 5) p += 10;
    else if (brights == 4) p += rain ? 7 : 8;
    else if (brights == 3 && !rain) p += 5;
    if (curtain && sake) p += 5;
    if (moon && sake) p += 5;
    if (boar && deer && fly) p += 5 + animals - 3;
    if (animals >= 5) p += 1 + animals - 5;
    if (poem == 3 && blue == 3) p += 10 + ribbons - 6;
    else if (poem == 3 || blue == 3) p += 5 + ribbons - 3;
    if (ribbons >= 5) p += 1 + ribbons - 5;
    if (plains >= 10) p += 1 + plains - 10;
    return p;
}

const std::vector<Opponent>& opponents() {
    static const std::vector<Opponent> o = {
        {"Grandmother Hana", "keeps a tidy hand, and stops while she's ahead", .2, .3, .85},
        {"Captain Ishida", "never met a koi-koi he didn't call", .85, .6, .35},
        {"Little Momo", "wants the brights, all of them, now", .55, .95, .2},
        {"Master Kiri", "patient, careful, and very hard to beat", .45, .55, 1.0, 60},
    };
    return o;
}

std::vector<int> matches(const KoiState& s, int c) {
    std::vector<int> m;
    for (int f : s.field) if (month_of(f) == month_of(c)) m.push_back(f);
    return m;
}

int round_value(const KoiState& s, int player) {
    int p = yaku_points(s.captured[static_cast<size_t>(player)]);
    if (p >= 7) p *= 2;
    if (s.koi[static_cast<size_t>(1 - player)] > 0) p *= 2;
    return p;
}

void deal(KoiState& s, std::uint32_t seed, int round, int dealer) {
    for (std::uint32_t attempt = 0;; ++attempt) {
        Rng r(static_cast<std::uint64_t>(seed) * 977 + static_cast<std::uint64_t>(round) * 131 + attempt);
        std::vector<int> d(48);
        for (int i = 0; i < 48; ++i) d[static_cast<size_t>(i)] = i;
        for (int i = 47; i > 0; --i) std::swap(d[static_cast<size_t>(i)], d[static_cast<size_t>(r.range(i + 1))]);
        s.hand = {};
        s.captured = {};
        s.field.clear();
        // dealt in twos, as at the table: two to the other player, two to the field, two to the dealer
        for (int k = 0; k < 4; ++k)
            for (int who = 0; who < 3; ++who)
                for (int q = 0; q < 2; ++q) {
                    const int c = d.back();
                    d.pop_back();
                    if (who == 0) s.hand[static_cast<size_t>(1 - dealer)].push_back(c);
                    else if (who == 1) s.field.push_back(c);
                    else s.hand[static_cast<size_t>(dealer)].push_back(c);
                }
        // all four of a month on the field can't be played: deal again
        std::array<int, 12> fm{};
        for (int c : s.field) ++fm[static_cast<size_t>(month_of(c))];
        if (*std::max_element(fm.begin(), fm.end()) == 4) continue;
        s.deck = d;
        break;
    }
    for (auto& h : s.hand) std::sort(h.begin(), h.end());
    s.round = round;
    s.dealer = dealer;
    s.turn = dealer;
    s.phase = Phase::play;
    s.koi = {};
    s.banked = {};
    s.pending = -1;
    s.choices.clear();
    s.drawn_this_turn = false;
    s.round_winner = -1;
    s.round_points = 0;
    s.round_yaku.clear();
    s.note.clear();
    // four of a month in hand (or four pairs) wins the round at once, for six points
    for (int p = 0; p < 2; ++p) {
        std::array<int, 12> hm{};
        for (int c : s.hand[static_cast<size_t>(p)]) ++hm[static_cast<size_t>(month_of(c))];
        int pairs = 0;
        bool four = false;
        for (int v : hm) { pairs += v == 2; four = four || v == 4; }
        if (four || pairs == 4) {
            s.note = four ? "Four of a month in hand: an instant win." : "Four pairs in hand: an instant win.";
            s.round_winner = p;
            s.round_points = 6;
            s.totals[static_cast<size_t>(p)] += 6;
            s.rounds_won_points.push_back(p == 0 ? 6 : -6);
            s.phase = Phase::round_over;
            return;
        }
    }
}

void new_match(KoiState& s, std::uint32_t seed, int opponent) {
    s = KoiState{};
    s.seed = seed;
    s.opponent = std::clamp(opponent, 0, static_cast<int>(opponents().size()) - 1);
    deal(s, seed, 0, static_cast<int>(seed % 2));
}

bool play(KoiState& s, int index) {
    if (s.phase != Phase::play) return false;
    auto& h = s.hand[static_cast<size_t>(s.turn)];
    if (index < 0 || index >= static_cast<int>(h.size())) return false;
    const int c = h[static_cast<size_t>(index)];
    h.erase(h.begin() + index);
    s.drawn_this_turn = false;
    s.note.clear();
    land(s, c, true);
    return true;
}

bool draw(KoiState& s) {
    if (s.phase != Phase::draw) return false;
    advance(s);
    return true;
}

bool choose(KoiState& s, int field_card) {
    if ((s.phase != Phase::choose_hand && s.phase != Phase::choose_draw) || !has(s.choices, field_card)) return false;
    const int c = s.pending;
    s.phase = Phase::play;
    finish_capture(s, c, field_card);
    return true;
}

bool decide(KoiState& s, bool koikoi) {
    if (s.phase != Phase::decide) return false;
    const int p = s.turn;
    if (!koikoi) {
        s.note = p == 0 ? "You stop and score the round." : std::string(opponents()[static_cast<size_t>(s.opponent)].name) + " stops and scores the round.";
        end_round(s, p, round_value(s, p));
        return true;
    }
    ++s.koi[static_cast<size_t>(p)];
    s.banked[static_cast<size_t>(p)] = yaku_points(s.captured[static_cast<size_t>(p)]);
    s.note = p == 0 ? "Koi-koi! You play on." : std::string(opponents()[static_cast<size_t>(s.opponent)].name) + " calls koi-koi!";
    pass_turn(s);
    return true;
}

void next_round(KoiState& s) {
    if (s.phase != Phase::round_over) return;
    const int dealer = s.round_winner >= 0 ? s.round_winner : s.dealer;  // the winner deals; a draw keeps the dealer
    if (s.round + 1 >= 12) { s.phase = Phase::match_over; return; }
    deal(s, s.seed, s.round + 1, dealer);
}

// ------------------------------------------------------------------ the computer
namespace {
int heuristic_play(const KoiState& s);
bool quick_koikoi(const KoiState& s);
// One guess at the hidden cards: everything this player hasn't seen, dealt into the other hand and the pile.
KoiState guess(const KoiState& s, int me, Rng& r) {
    KoiState g = s;
    std::vector<int> unseen = g.hand[static_cast<size_t>(1 - me)];
    unseen.insert(unseen.end(), g.deck.begin(), g.deck.end());
    for (int i = static_cast<int>(unseen.size()) - 1; i > 0; --i) std::swap(unseen[static_cast<size_t>(i)], unseen[static_cast<size_t>(r.range(i + 1))]);
    const size_t nh = g.hand[static_cast<size_t>(1 - me)].size();
    g.hand[static_cast<size_t>(1 - me)].assign(unseen.begin(), unseen.begin() + static_cast<long>(nh));
    g.deck.assign(unseen.begin() + static_cast<long>(nh), unseen.end());
    return g;
}
// Play the round out with the quick judgement on both sides; the result for `me`.
double rollout(KoiState g, int me) {
    for (int guard = 0; guard < 200; ++guard) {
        switch (g.phase) {
            case Phase::play: play(g, heuristic_play(g)); break;
            case Phase::draw: draw(g); break;
            case Phase::choose_hand:
            case Phase::choose_draw: choose(g, ai_choose(g)); break;
            case Phase::decide: decide(g, quick_koikoi(g)); break;
            default:
                // the round's points, and for a drawn round a little credit for the better captures
                if (g.round_winner < 0) return .03 * (potential(g.captured[static_cast<size_t>(me)], .5) - potential(g.captured[static_cast<size_t>(1 - me)], .5));
                return g.round_winner == me ? g.round_points : -g.round_points;
        }
    }
    return 0;
}
}  // namespace

int ai_play(const KoiState& s) {
    return ai_play_deep(s, s.turn == 1 ? opponents()[static_cast<size_t>(s.opponent)].foresight : 0);
}

int ai_play_deep(const KoiState& s, int depth) {
    const int me = s.turn;
    if (depth <= 0 || s.hand[static_cast<size_t>(me)].size() <= 1) return heuristic_play(s);
    // every candidate is tried against the same guesses at the hidden cards, so the comparison is fair
    const std::uint64_t base = hash_state(s) ^ 0xF0E5ULL;
    std::vector<KoiState> guesses;
    guesses.reserve(static_cast<size_t>(depth));
    for (int k = 0; k < depth; ++k) { Rng r(base + static_cast<std::uint64_t>(k) * 7919); guesses.push_back(guess(s, me, r)); }
    const auto& hand = s.hand[static_cast<size_t>(me)];
    const int quick = heuristic_play(s);
    int best = quick;
    double bs = -1e18;
    for (size_t i = 0; i < hand.size(); ++i) {
        double total = 0;
        for (const KoiState& g0 : guesses) {
            KoiState g = g0;
            play(g, static_cast<int>(i));
            total += rollout(g, me);
        }
        total += static_cast<int>(i) == quick ? depth * .2 : 0;  // a nudge toward the quick judgement on a near tie
        if (total > bs) { bs = total; best = static_cast<int>(i); }
    }
    return best;
}

namespace {
int heuristic_play(const KoiState& s) {
    // runs thousands of times a move inside the foresight: no allocations in the loop, every constant hoisted
    const int me = s.turn;
    const Opponent& o = opponents()[static_cast<size_t>(s.opponent)];
    const double greed = me == 1 ? o.greed : .6, care = me == 1 ? o.care : 1.0;
    const auto& hand = s.hand[static_cast<size_t>(me)];
    const auto& mine = s.captured[static_cast<size_t>(me)];
    const auto& theirs = s.captured[static_cast<size_t>(1 - me)];
    const double base = potential(mine, greed), their_base = potential(theirs, .6);
    // per month, the cards the other player might hold: everything this player hasn't seen
    std::array<int, 12> unseen{};
    {
        std::array<bool, 48> seen{};
        for (int c : hand) seen[static_cast<size_t>(c)] = true;
        for (int c : s.field) seen[static_cast<size_t>(c)] = true;
        for (int c : mine) seen[static_cast<size_t>(c)] = true;
        for (int c : theirs) seen[static_cast<size_t>(c)] = true;
        for (int c = 0; c < 48; ++c) unseen[static_cast<size_t>(month_of(c))] += !seen[static_cast<size_t>(c)];
    }
    std::vector<int> mine_plus(mine), theirs_plus(theirs);  // reused: the base pile, then a card or few added and removed
    mine_plus.reserve(mine.size() + 4);
    theirs_plus.reserve(theirs.size() + 1);
    auto their_gain = [&](int c) { theirs_plus.push_back(c); const double v = potential(theirs_plus, .6) - their_base; theirs_plus.pop_back(); return v; };
    const std::uint64_t variety = hash_state(s);
    int best = 0;
    double bs = -1e18;
    int m[4];
    for (size_t i = 0; i < hand.size(); ++i) {
        const int c = hand[i];
        int nm = 0;
        for (int f : s.field) if (month_of(f) == month_of(c) && nm < 4) m[nm++] = f;
        double score;
        if (nm == 0) {
            // laid on the field: what could the other player make of it?
            const double chance = std::min(1.0, unseen[static_cast<size_t>(month_of(c))] * .45);
            score = -care * their_gain(c) * chance - .5;
            // keeping a big card back can be wise: laying a bright is costly
            if (info(c).type == Type::bright) score -= 4 * care;
        } else {
            double take = -1e18;
            if (nm == 3) {
                mine_plus.push_back(c);
                for (int k = 0; k < 3; ++k) mine_plus.push_back(m[k]);
                take = potential(mine_plus, greed) - base;
                mine_plus.resize(mine.size());
            } else {
                for (int k = 0; k < nm; ++k) {
                    mine_plus.push_back(c);
                    mine_plus.push_back(m[k]);
                    take = std::max(take, potential(mine_plus, greed) - base);
                    mine_plus.resize(mine.size());
                }
            }
            // denying them: what the field card would have been worth to the other player
            double deny = 0;
            for (int k = 0; k < nm; ++k) deny = std::max(deny, their_gain(m[k]));
            score = take + care * .5 * deny + 2;
        }
        // a whisper of variety, the same every time for the same table
        score += static_cast<double>((variety >> (i * 5)) % 7) * .01;
        if (score > bs) { bs = score; best = static_cast<int>(i); }
    }
    return best;
}
}  // namespace

int ai_choose(const KoiState& s) {
    const int me = s.turn;
    const auto& mine = s.captured[static_cast<size_t>(me)];
    const Opponent& o = opponents()[static_cast<size_t>(s.opponent)];
    int best = s.choices.empty() ? -1 : s.choices[0];
    double bs = -1e18;
    for (int f : s.choices) {
        auto t = mine;
        t.push_back(s.pending);
        t.push_back(f);
        const double v = potential(t, me == 1 ? o.greed : .6) + (info(f).type == Type::bright ? .5 : 0);
        if (v > bs) { bs = v; best = f; }
    }
    return best;
}

bool ai_koikoi(const KoiState& s) {
    const int me = s.turn;
    const Opponent& o = opponents()[static_cast<size_t>(s.opponent)];
    if (me == 1 && o.foresight > 0 && s.hand[1].size() >= 2) {
        // weigh it: what stopping is worth now, against playing on (averaged over guesses at the hidden cards)
        const double stop = round_value(s, me);
        double on = 0;
        const int n = o.foresight;
        for (int k = 0; k < n; ++k) {
            Rng r((hash_state(s) ^ 0xC0C0ULL) + static_cast<std::uint64_t>(k) * 7919);
            KoiState g = guess(s, me, r);
            ++g.koi[static_cast<size_t>(me)];
            g.banked[static_cast<size_t>(me)] = yaku_points(g.captured[static_cast<size_t>(me)]);
            g.turn = 1 - me;
            g.drawn_this_turn = false;
            g.phase = Phase::play;
            if (g.hand[0].empty() && g.hand[1].empty()) { on += 0; continue; }
            on += rollout(g, me);
        }
        return on / n > stop * 1.15;
    }
    return quick_koikoi(s);
}

namespace {
bool quick_koikoi(const KoiState& s) {
    const int me = s.turn;
    const Opponent& o = opponents()[static_cast<size_t>(s.opponent)];
    const double bold = me == 1 ? o.boldness : .4;
    const int left = static_cast<int>(s.hand[static_cast<size_t>(me)].size());
    if (left < 2) return false;
    const int now = round_value(s, me);
    // the other player's threat: how close are they to a set?
    const auto& theirs = s.captured[static_cast<size_t>(1 - me)];
    const double threat = std::min(1.0, potential(theirs, .5) / 40.0) + (s.koi[static_cast<size_t>(1 - me)] > 0 ? .1 : 0);
    // already doubled (seven or more)? Then the stakes are high: hold
    const double want = bold + left * .05 - threat * .9 - (now >= 7 ? .35 : 0) - (s.round == 11 && s.totals[static_cast<size_t>(me)] > s.totals[static_cast<size_t>(1 - me)] ? .3 : 0);
    return want > .45;
}
}  // namespace

bool invariant(const KoiState& s) {
    std::array<int, 48> seen{};
    auto mark = [&](const std::vector<int>& v) { for (int c : v) { if (c < 0 || c > 47) return false; ++seen[static_cast<size_t>(c)]; } return true; };
    if (!mark(s.hand[0]) || !mark(s.hand[1]) || !mark(s.captured[0]) || !mark(s.captured[1]) || !mark(s.field) || !mark(s.deck)) return false;
    if (s.pending >= 0) ++seen[static_cast<size_t>(s.pending)];
    for (int v : seen) if (v != 1) return false;
    return true;
}

// ------------------------------------------------------------------ saving
namespace {
std::string list(const std::vector<int>& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) o += (i ? "," : "") + std::to_string(v[i]);
    return o;
}
std::vector<int> parse_list(const std::string& s) {
    std::vector<int> out;
    std::stringstream ss(s);
    std::string x;
    while (std::getline(ss, x, ',')) if (!x.empty()) out.push_back(std::stoi(x));
    return out;
}
}  // namespace

std::string save(const KoiState& s) {
    std::ostringstream o;
    o << "koikoi=1\nseed=" << s.seed << "\nopponent=" << s.opponent << "\nround=" << s.round << "\ndealer=" << s.dealer << "\nturn=" << s.turn
      << "\nphase=" << static_cast<int>(s.phase) << "\nhand0=" << list(s.hand[0]) << "\nhand1=" << list(s.hand[1]) << "\ncap0=" << list(s.captured[0])
      << "\ncap1=" << list(s.captured[1]) << "\nfield=" << list(s.field) << "\ndeck=" << list(s.deck) << "\ntotals=" << s.totals[0] << "," << s.totals[1]
      << "\nkoi=" << s.koi[0] << "," << s.koi[1] << "\nbanked=" << s.banked[0] << "," << s.banked[1] << "\npending=" << s.pending << "\nchoices=" << list(s.choices)
      << "\ndrawn=" << s.drawn_this_turn << "\nwinner=" << s.round_winner << "\npoints=" << s.round_points << "\nhistory=" << list(s.rounds_won_points) << "\n";
    return o.str();
}

bool load(const std::string& text, KoiState& out) {
    KoiState s;
    bool tagged = false;
    try {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            auto pair = [&](std::array<int, 2>& a) { const auto p = parse_list(v); if (p.size() == 2) a = {p[0], p[1]}; };
            if (k == "koikoi") tagged = v == "1";
            else if (k == "seed") s.seed = static_cast<std::uint32_t>(std::stoul(v));
            else if (k == "opponent") s.opponent = std::clamp(std::stoi(v), 0, static_cast<int>(opponents().size()) - 1);
            else if (k == "round") s.round = std::clamp(std::stoi(v), 0, 11);
            else if (k == "dealer") s.dealer = std::clamp(std::stoi(v), 0, 1);
            else if (k == "turn") s.turn = std::clamp(std::stoi(v), 0, 1);
            else if (k == "phase") s.phase = static_cast<Phase>(std::clamp(std::stoi(v), 0, 6));
            else if (k == "hand0") s.hand[0] = parse_list(v);
            else if (k == "hand1") s.hand[1] = parse_list(v);
            else if (k == "cap0") s.captured[0] = parse_list(v);
            else if (k == "cap1") s.captured[1] = parse_list(v);
            else if (k == "field") s.field = parse_list(v);
            else if (k == "deck") s.deck = parse_list(v);
            else if (k == "totals") pair(s.totals);
            else if (k == "koi") pair(s.koi);
            else if (k == "banked") pair(s.banked);
            else if (k == "pending") s.pending = std::stoi(v);
            else if (k == "choices") s.choices = parse_list(v);
            else if (k == "drawn") s.drawn_this_turn = v == "1";
            else if (k == "winner") s.round_winner = std::stoi(v);
            else if (k == "points") s.round_points = std::stoi(v);
            else if (k == "history") s.rounds_won_points = parse_list(v);
        }
    } catch (...) {
        return false;
    }
    if (!tagged || !invariant(s)) return false;
    if (s.round_winner >= 0) s.round_yaku = yaku(s.captured[static_cast<size_t>(s.round_winner)]);
    out = s;
    return true;
}

}  // namespace games::koi
