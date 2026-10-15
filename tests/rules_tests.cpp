#include "game.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
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
    require(game.liftable(0, 0), "a run in order can be picked up");
    game.state.piles[2] = {{10, 0, 2, true}, {9, 0, 40, true}};
    require(!game.liftable(2, 0) && game.liftable(2, 1),
            "cards not in order cannot be picked up together; the top one can");
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
    // TriPeaks: each place rests on the two below it, half a step to either side.
    for (int place = 0; place < games::tripeaks_places; ++place) {
        require(games::tripeaks_place(games::tripeaks_pile(place)) == place, "TriPeaks places and piles");
        for (int cover : games::tripeaks_covers(place))
            if (cover >= 0)
                require(games::tripeaks_row(cover) == games::tripeaks_row(place) + 1 &&
                            std::abs(games::tripeaks_column(cover) - games::tripeaks_column(place)) == .5,
                        "a card rests on the two below it");
    }
    require(games::tripeaks_place(14) < 0 && games::tripeaks_place(15) < 0, "the stock and the waste are no places");
    require(games::tripeaks_next({13, 0, 0, true}, {1, 1, 1, true}) && games::tripeaks_next({5, 0, 0, true}, {6, 2, 1, true}) &&
                !games::tripeaks_next({5, 0, 0, true}, {7, 2, 1, true}) && !games::tripeaks_next({5, 0, 0, true}, {5, 2, 1, true}),
            "ranks one apart go on the waste, king and ace too");
    int won = 0, longest = 0, unsettled = 0;
    for (unsigned seed = 1; seed <= 60; ++seed) {
        game.deal(games::Kind::tripeaks, seed);
        int open = 0;
        for (int place = 0; place < games::tripeaks_places; ++place)
            open += game.state.piles[games::tripeaks_pile(place)].at(0).up;
        require(game.invariant() && open == 10 && game.state.piles[14].size() == 23 && game.state.piles[15].size() == 1,
                "TriPeaks deal: 28 in the peaks, the bottom row open, one on the waste");
        const games::TriPeaksLine line = games::tripeaks_solve(game.state, 200000);
        // A deal without a win can take more than the budget to prove so; dealing needs only
        // the wins it finds.
        unsettled += !line.solved && !line.exhausted;
        if (!line.solved)
            continue;
        ++won;
        for (int step : line.steps) {
            require(step < 0 ? game.draw() : game.move({step, 0, 15}), "the winning line replays through Game");
            require(game.invariant(), "TriPeaks conservation");
            longest = std::max(longest, game.state.run);
        }
        require(game.state.over, "the line clears the peaks");
        while (game.undo())
            require(game.invariant(), "TriPeaks undo conservation");
        require(game.state.moves == 0 && game.state.run == 0 && !game.state.over, "undo to the deal");
    }
    require(won >= 40, "most deals can be won");
    {
        // As the table deals: the slowest of a hundred winnable deals found is reported.
        double slowest = 0;
        for (std::uint32_t start = 1000; start < 1100; ++start) {
            const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
            const std::uint32_t seed = games::tripeaks_winnable_seed(start);
            slowest = std::max(slowest, std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count());
            game.deal(games::Kind::tripeaks, seed);
            require(games::tripeaks_solve(game.state, 200000).solved, "a winnable seed is winnable");
        }
        std::cout << "TriPeaks: the slowest winnable deal found in " << slowest * 1000 << " ms.\n";
    }
    // A run up the ranks, and a card turning over as the two on it go.
    game.deal(games::Kind::tripeaks, 3);
    for (games::Pile& pile : game.state.piles)
        pile.clear();
    game.state.piles[games::tripeaks_pile(18)] = {{5, 0, 0, true}};
    game.state.piles[games::tripeaks_pile(19)] = {{6, 0, 1, true}};
    game.state.piles[games::tripeaks_pile(9)] = {{7, 0, 2, false}};
    game.state.piles[15] = {{4, 1, 3, true}};
    game.state.piles[14] = {{10, 1, 4, true}};
    require(!game.liftable(games::tripeaks_pile(9), 0), "a covered card cannot be played");
    require(!game.legal({games::tripeaks_pile(19), 0, 15}), "a six does not go on a four");
    require(game.move({games::tripeaks_pile(18), 0, 15}) && game.state.run == 1 && game.state.score == 1, "the run's first card");
    require(!game.state.piles[games::tripeaks_pile(9)][0].up, "still covered by one card");
    require(game.move({games::tripeaks_pile(19), 0, 15}) && game.state.run == 2 && game.state.score == 3,
            "each card of a run worth one more");
    require(game.state.piles[games::tripeaks_pile(9)][0].up, "uncovered, it turns over");
    {
        games::Game turned = game;
        require(turned.draw() && turned.state.run == 0 && turned.state.piles[15].back().rank == 10 && turned.state.piles[14].empty(),
                "turning the stock ends the run");
        require(!turned.draw(), "the stock is turned once");
    }
    require(game.move({games::tripeaks_pile(9), 0, 15}) && game.state.over && game.state.score == 1 + 2 + 3 + 50 + 10,
            "the peaks cleared: 50 more and 10 for the card left in the stock");
    std::cout << "Passed: 320 deals, conservation, legal hint actions, complete Hearts hands, "
                 "scoring, undo, FreeCell capacity, Spider stock restrictions; TriPeaks: " << won
              << " of 60 deals won through Game (longest run " << longest << ", " << unsettled
              << " unsettled in the budget), its rules and scoring.\n";
}
