#pragma once
// The Keeper's wagers. You're a drowned sailor with years owed to the locker;
// before every game the Keeper of the locker lays out three wagers, from a
// friendly hand to everything on the barrel, and you choose. Each names who
// sits down with you, how many years a loss adds and a win strikes off, and
// often something stranger: lose and the Keeper keeps your shadow, or your
// singing voice, in a jar; win and you take home a pearl, a ship in a bottle,
// or one of your lost things back. Generated fresh each time from a seed.
#include "brain.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ld {

struct Wager {
    std::string title;       // "A friendly hand"
    int tier = 1;            // 1..3
    std::vector<int> seats;  // cast indices of the opponents, 2 or 3
    int years_lose = 1;      // added to what you owe if you lose
    int years_win = 1;       // struck off if you win
    std::string forfeit;     // also lost to the Keeper's jar on a loss ("" for none)
    std::string prize;       // also won ("" for none); "back:<thing>" returns a forfeit
    std::string pitch;       // the Keeper's patter
};

// Three wagers, tiers 1, 2 and 3. `jar` is what the Keeper already holds of yours (forfeits can be won back).
std::vector<Wager> offer(std::uint64_t seed, int owed, const std::vector<std::string>& jar);

// "ten years in Davy Jones' locker"
std::string years_words(int y);
std::string prize_words(const std::string& prize);  // "your shadow, back" for a returned forfeit

}  // namespace ld
