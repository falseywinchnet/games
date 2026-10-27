#pragma once
// Koi-Koi, the hanafuda game, for the PlaySuite card table. Portable rules
// only: no rendering, no platform code.
//
// The deck is 48 cards, four for each month, each month a flower: pine,
// plum, cherry, wisteria, iris, peony, bush clover, pampas grass,
// chrysanthemum, maple, willow, paulownia. Cards are brights, animals,
// ribbons and plains. Two players; eight cards each and eight face up on the
// field. On a turn you play a card from your hand: if a field card shares
// its month you take both (choosing which if there are two; all four if
// there are three); otherwise it joins the field. Then the top of the draw
// pile is turned and matched the same way. Collect sets (yaku). The moment
// you make a new set you choose: stop, and score the round, or call
// "koi-koi" and play on for more, risking that your opponent makes a set
// first. A match is twelve rounds, one for each month.
//
// Card ids are 0..47: month = id / 4 (0 = January); within a month the
// special cards come first (see info()).
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace games::koi {

enum class Type : std::uint8_t { bright, animal, ribbon, plain };
enum class Ribbon : std::uint8_t { none, poem, blue, red };

struct CardInfo {
    int month = 0;          // 0..11
    Type type = Type::plain;
    Ribbon ribbon = Ribbon::none;
    const char* name = "";  // "Crane and Sun", "Pine"...
};
const CardInfo& info(int id);
const char* month_name(int month);   // "January"
const char* plant_name(int month);   // "Pine"
inline int month_of(int id) { return id / 4; }

// card ids of the named cards
namespace card {
constexpr int crane = 0, warbler = 4, curtain = 8, cuckoo = 12, bridge = 16, butterflies = 20, boar = 24,
              moon = 28, geese = 29, sake = 32, deer = 36, rain_man = 40, swallow = 41, lightning = 43, phoenix = 44;
}

struct Yaku {
    std::string name;       // "Five Brights"
    int points = 0;
    std::vector<int> cards; // the cards that make it
};
// Every set a pile of captured cards makes (the standard list; the sake cup counts as an animal and a plain).
std::vector<Yaku> yaku(const std::vector<int>& captured);
int yaku_points(const std::vector<int>& captured);

enum class Phase : std::uint8_t {
    play,          // the player to move chooses a card from their hand
    choose_hand,   // their card matches two field cards: which one?
    draw,          // the hand card is placed: turn over the top of the draw pile (draw())
    choose_draw,   // the drawn card matches two field cards: which one?
    decide,        // they made a new set: stop, or koi-koi?
    round_over,    // the round is scored (or drawn); next_round() deals the next
    match_over,    // twelve rounds played
};

// Who sits across the table. Each has a temperament the AI plays to.
struct Opponent {
    const char* name;
    const char* blurb;
    double boldness;   // 0..1 how readily they call koi-koi
    double greed;      // 0..1 how much they chase big sets over safe points
    double care;       // 0..1 how carefully they avoid feeding you
    int foresight = 0; // >0: plays each choice out this many times with the unseen cards guessed, and takes the best
};
const std::vector<Opponent>& opponents();

struct KoiState {
    std::uint32_t seed = 1;
    int opponent = 0;                      // index into opponents()
    int round = 0;                         // 0..11; the month of the round
    int dealer = 0;                        // 0 you, 1 the opponent: the dealer moves first
    int turn = 0;
    Phase phase = Phase::play;
    std::array<std::vector<int>, 2> hand, captured;
    std::vector<int> field, deck;          // deck: back() is the top
    std::array<int, 2> totals{};           // match points
    std::array<int, 2> koi{};              // koi-koi calls this round
    std::array<int, 2> banked{};           // set points at each player's last decision (a new set must beat it)
    int pending = -1;                      // a card waiting for its match choice
    std::vector<int> choices;              // the field cards it could take
    bool drawn_this_turn = false;
    // the round's outcome
    int round_winner = -1;                 // -1 drawn
    int round_points = 0;
    std::vector<Yaku> round_yaku;
    std::string note;                      // what just happened, for the table to show
    std::vector<int> rounds_won_points;    // per round: + you, - them, 0 drawn
};

// Shuffle and deal round `round` (redealing a field that holds all four of a month).
void deal(KoiState& s, std::uint32_t seed, int round, int dealer);
// A new twelve-round match against an opponent.
void new_match(KoiState& s, std::uint32_t seed, int opponent);
// The player to move plays the card at `index` of their hand.
bool play(KoiState& s, int index);
// Turn over the top of the draw pile and match it (phase draw).
bool draw(KoiState& s);
// Choose which of two matching field cards to take (a field card id from s.choices).
bool choose(KoiState& s, int field_card);
// After a new set: stop (score the round) or call koi-koi (play on).
bool decide(KoiState& s, bool koikoi);
// Deal the next round (or end the match after the twelfth).
void next_round(KoiState& s);

// Which field cards a card would match.
std::vector<int> matches(const KoiState& s, int card);
// The value of a round's sets for a player, with the doublings: x2 at seven points or more,
// and x2 if the other player had called koi-koi.
int round_value(const KoiState& s, int player);

// The computer's choices (for the opponent, and for hints when asked for player 0).
int ai_play(const KoiState& s);                // hand index
int ai_play_deep(const KoiState& s, int samples); // the same with foresight, whoever is to move (hints use it)
int ai_choose(const KoiState& s);              // field card id
bool ai_koikoi(const KoiState& s);             // call koi-koi?

bool invariant(const KoiState& s);             // all 48 cards accounted for, once each
std::string save(const KoiState& s);
bool load(const std::string& text, KoiState& s);

}  // namespace games::koi
