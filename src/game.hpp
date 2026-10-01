#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace games {
enum class Kind { solitaire, spider, freecell, hearts };
struct Card {
    int rank = 1;
    int suit = 0;
    int id = 0;
    bool up = true;
};
using Pile = std::vector<Card>;
// Slots 0..9 tableau; 10..13 foundations; 14 stock; 15 waste;
// 16..19 free cells. Hearts uses 0..3 hands, 10 trick, 11 captured.
struct State {
    Kind kind = Kind::solitaire;
    std::array<Pile, 20> piles{};
    std::uint32_t seed = 1;
    int moves = 0, score = 0, completed = 0, draw_count = 1, spider_suits = 1;
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
const char* game_name(Kind kind);
} // namespace games
