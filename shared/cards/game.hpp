#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace games {
enum class Kind { solitaire, spider, freecell, hearts, tripeaks };
constexpr int kind_count = 5;
struct Card {
    int rank = 1;
    int suit = 0;
    int id = 0;
    bool up = true;
};
using Pile = std::vector<Card>;
// Slots 0..9 tableau; 10..13 foundations; 14 stock; 15 waste;
// 16..19 free cells. Hearts uses 0..3 hands, 10 trick, 11 captured.
// TriPeaks lays its 28 cards one to a pile, by place (tripeaks_pile), around the stock
// and waste: piles 0..13 and 16..29.
constexpr int pile_count = 30;
struct State {
    Kind kind = Kind::solitaire;
    std::array<Pile, pile_count> piles{};
    std::uint32_t seed = 1;
    int moves = 0, score = 0, completed = 0, draw_count = 1, spider_suits = 1;
    int run = 0;  // TriPeaks: cards played from the peaks since the stock was last turned
    int turn = 0, leader = 0, trick_number = 0, round = 0;
    bool hearts_broken = false, passing = true, over = false;
    std::array<int, 4> points{}, totals{};
};
struct Move {
    int from = -1, index = -1, to = -1;
};
class Game {
  public:
    State state;
    std::vector<State> history;
    std::string message;
    void deal(Kind kind, std::uint32_t seed, int option = 1);
    bool legal(Move move) const;
    // The cards from `index` to the end of `pile` may be picked up together: a pile play
    // starts from, and a run in order (alternating colours, or one suit in Spider).
    bool liftable(int pile, int index) const;
    bool move(Move move);
    bool draw();
    bool undo();
    Move hint() const;
    bool pass(const std::vector<int>& indices);
    bool play(int index);
    bool legal_heart(int player, int index) const;
    int computer_choice() const;
    bool advance_trick();
    void next_round();
    bool invariant() const;

  private:
    void remember();
    void settle();
};
bool red(Card card);
std::string card_name(Card card);
// Hearts opponents. Each match seats three players named after US presidents. Every hand,
// one of them is sharp (remembers every card and thinks further ahead), one is forgetful,
// and one plays a steady middle game. Player 0 is "You"; hints use the sharp player's eye.
std::string hearts_name(const State& state, int player);
enum class HeartsSkill { forgetful, steady, sharp };
HeartsSkill hearts_skill(const State& state, int player);
// The three cards a computer player passes.
std::vector<int> hearts_pass_choice(const State& state, int player);
const char* game_name(Kind kind);

// TriPeaks: three peaks of 28 cards in four rows (3, 6, 9 and 10), each card resting on the
// two below it. Places 0..2 are the peaks' tops, 18..27 the open bottom row.
constexpr int tripeaks_places = 28;
[[nodiscard]] int tripeaks_pile(int place);
[[nodiscard]] int tripeaks_place(int pile);  // -1 for the stock, the waste and unused piles
[[nodiscard]] int tripeaks_row(int place);
// Where a place lies across the table, in steps of the bottom row's spacing (0..9).
[[nodiscard]] double tripeaks_column(int place);
// The two places resting on `place`, or -1 for the open bottom row.
[[nodiscard]] std::array<int, 2> tripeaks_covers(int place);
// Ranks one apart go on the waste, king and ace too.
[[nodiscard]] bool tripeaks_next(Card a, Card b);
// A line that clears the peaks from `state`: each step a pile played to the waste, or -1 for a
// turn of the stock. Empty when there is none within `budget` positions looked at (`solved`
// says which; `exhausted` that the search finished without one).
struct TriPeaksLine {
    std::vector<int> steps;
    bool solved = false, exhausted = false;
};
[[nodiscard]] TriPeaksLine tripeaks_solve(const State& state, int budget);
// The first seed from `seed` whose deal can be won, within `tries` seeds.
[[nodiscard]] std::uint32_t tripeaks_winnable_seed(std::uint32_t seed, int tries = 400);
} // namespace games
