#include "game.hpp"
#include <iostream>
#include <stdexcept>
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    for (int kind = 0; kind < 4; ++kind)
        for (unsigned seed = 1; seed <= 80; ++seed) {
            games::Game game;
            game.deal(static_cast<games::Kind>(kind), seed, kind == 1 ? 4 : 3);
            require(game.invariant(), "deal conservation");
            if (kind == 3) {
                require(game.pass({0, 1, 2}), "pass");
                for (int n = 0; n < 52; ++n) {
                    int i = game.computer_choice();
                    require(i >= 0, "Hearts must have legal play");
                    require(game.play(i), "Hearts play");
                    if (game.state.piles[10].size() == 4)
                        game.advance_trick();
                    require(game.invariant(), "Hearts conservation");
                }
                int sum = 0;
                for (int p : game.state.points)
                    sum += p;
                require(sum == 26 || sum == 78, "Hearts points");
                require(game.state.trick_number == 13, "13 tricks");
                const std::string west = games::hearts_name(game.state, 1);
                game.next_round();
                require(game.state.round == 1, "next round");
                require(games::hearts_name(game.state, 1) == west, "seats keep their names");
                require(west != games::hearts_name(game.state, 2) &&
                            west != games::hearts_name(game.state, 3) &&
                            games::hearts_name(game.state, 2) != games::hearts_name(game.state, 3),
                        "three different presidents");
                int sharp = 0, forgetful = 0;
                for (int p = 1; p < 4; ++p) {
                    sharp += games::hearts_skill(game.state, p) == games::HeartsSkill::sharp;
                    forgetful +=
                        games::hearts_skill(game.state, p) == games::HeartsSkill::forgetful;
                }
                require(sharp == 1 && forgetful == 1, "one sharp and one forgetful seat per hand");
            } else {
                for (int n = 0; n < 100; ++n) {
                    games::Move m = game.hint();
                    if (m.from >= 0)
                        require(game.move(m), "hint legal");
                    else
                        game.draw();
                    require(game.invariant(), "move conservation");
                }
                while (game.undo())
                    require(game.invariant(), "undo conservation");
                require(game.state.moves == 0, "undo to initial");
            }
        }
    games::Game game;
    game.deal(games::Kind::freecell, 42);
    for (games::Pile& p : game.state.piles)
        p.clear();
    game.state.piles[0] = {{7, 0, 0, true}, {6, 1, 1, true}, {5, 0, 2, true}};
    game.state.piles[1] = {{8, 1, 3, true}};
    for (int i = 16; i < 20; ++i)
        game.state.piles[i] = {{1, 0, i, true}};
    for (int i = 2; i < 8; ++i)
        game.state.piles[i] = {{10, 0, i, true}};
    require(!game.legal({0, 0, 1}), "FreeCell capacity rejects unavailable scratch");
    game.state.piles[16].clear();
    game.state.piles[17].clear();
    require(game.legal({0, 0, 1}), "FreeCell capacity accepts three");
    game.deal(games::Kind::spider, 9, 1);
    game.state.piles[0].clear();
    require(!game.draw(), "Spider empty column prevents deal");
    game.deal(games::Kind::solitaire, 9);
    require(!game.legal({-1, 0, 0}), "invalid source");
    require(!game.legal({0, 1000, 1}), "invalid index");
    // An earlier waste card must not become a movable tableau run.
    game.state.piles[15] = {{7, 0, 0, true}, {6, 1, 1, true}};
    game.state.piles[0] = {{8, 1, 2, true}};
    require(!game.legal({15, 0, 0}), "only the top waste card may move");
    game.deal(games::Kind::hearts, 7);
    game.state.passing = false;
    game.state.turn = 0;
    game.state.piles[0] = {{2, 0, 0, true}, {1, 0, 1, true}, {12, 3, 2, true}};
    require(game.legal_heart(0, 0), "two clubs opens first trick");
    require(!game.legal_heart(0, 1), "ace cannot open first trick");
    game.state.piles[10] = {{2, 0, 0, true}};
    game.state.piles[0] = {{12, 3, 2, true}, {7, 1, 3, true}};
    require(!game.legal_heart(0, 0), "first trick cannot dump queen with alternative");
    require(game.legal_heart(0, 1), "first trick nonpoint discard");
    game.state.trick_number = 1;
    game.state.piles[10].clear();
    game.state.piles[0] = {{3, 2, 0, true}, {8, 0, 1, true}};
    require(!game.legal_heart(0, 0), "unbroken hearts cannot lead with other suit");
    game.state.hearts_broken = true;
    require(game.legal_heart(0, 0), "broken hearts may lead");
    game.state.trick_number = 12;
    game.state.leader = 2;
    game.state.points = {0, 0, 22, 0};
    game.state.piles[10] = {{1, 2, 0, true}, {13, 2, 1, true}, {12, 2, 2, true}, {11, 2, 3, true}};
    require(game.advance_trick(), "last trick advances");
    require(game.state.points[2] == 0 && game.state.points[0] == 26 && game.state.points[1] == 26 &&
                game.state.points[3] == 26,
            "ace high and shooting moon distribution");
    std::cout << "Passed: 320 deals, conservation, legal hint actions, complete Hearts hands, "
                 "scoring, undo, FreeCell capacity, Spider stock restrictions.\n";
}
