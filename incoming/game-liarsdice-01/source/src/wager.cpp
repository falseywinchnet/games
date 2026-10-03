#include "wager.hpp"

#include <algorithm>

namespace ld {

namespace {
const std::vector<std::string> kForfeits = {
    "your shadow", "your singing voice", "the colour of your eyes", "your left boot", "every Tuesday for a decade",
    "your sense of direction", "your fondest memory of summer", "your reflection", "the taste of rum", "your sea legs",
    "your laugh", "your good hat", "the last page of your logbook", "your snoring", "your name, for ten years",
    "the feeling in your little toe", "your ability to whistle", "your fear of sharks", "your sweet tooth", "your best knot",
};
const std::vector<std::string> kPrizes = {
    "a pearl the size of your fist", "a ship in a bottle, with a real crew", "the Keeper's spare key", "a map to somewhere",
    "a compass that points to regret", "a tricorn with a magnificent feather", "a jar of genuine mermaid tears",
    "a doubloon that always lands heads", "a conch that hums sea shanties", "a lantern that never goes out",
    "a bottle of two-hundred-year rum", "a kraken's tooth", "a spyglass that sees yesterday", "a parrot that only tells the truth",
    "a pocket watch that runs backwards", "a cutlass with a polite edge",
};
const char* kTitles[3][4] = {
    {"A friendly hand", "Pennies and pebbles", "A gentle game", "Warm-up at the barrel"},
    {"A proper wager", "Stakes worth sweating", "The middle watch", "A sailor's bet"},
    {"Everything on the barrel", "The long dark", "Davy's own table", "Bones and broadsides"},
};
}  // namespace

std::string years_words(int y) {
    if (y == 1) return "a year in Davy Jones' locker";
    return std::to_string(y) + " years in Davy Jones' locker";
}

std::string prize_words(const std::string& prize) {
    if (prize.rfind("back:", 0) == 0) return prize.substr(5) + ", back";
    return prize;
}

std::vector<Wager> offer(std::uint64_t seed, int owed, const std::vector<std::string>& jar) {
    Rng r(seed ^ 0x5EA5EAULL);
    const auto& c = cast();
    std::vector<Wager> out;
    std::vector<int> used;
    for (int tier = 1; tier <= 3; ++tier) {
        Wager w;
        w.tier = tier;
        w.title = kTitles[tier - 1][r.range(4)];
        // who sits down: the higher the stakes, the sharper the company
        const int n = tier == 1 ? 2 : tier == 2 ? 2 + r.range(2) : 3;
        std::vector<int> pool;
        for (int i = 0; i < static_cast<int>(c.size()); ++i) {
            const int dg = c[static_cast<size_t>(i)].danger;
            const bool fits = tier == 1 ? dg <= 2 : tier == 2 ? dg >= 1 : dg >= 2;
            if (fits && std::find(used.begin(), used.end(), i) == used.end()) pool.push_back(i);
        }
        for (int k = 0; k < n && !pool.empty(); ++k) {
            const int pick = r.range(static_cast<int>(pool.size()));
            w.seats.push_back(pool[static_cast<size_t>(pick)]);
            used.push_back(pool[static_cast<size_t>(pick)]);
            pool.erase(pool.begin() + pick);
        }
        // the years
        if (tier == 1) { w.years_lose = 1 + r.range(3); w.years_win = 1 + r.range(2); }
        else if (tier == 2) { w.years_lose = 5 + r.range(11); w.years_win = 5 + r.range(6); }
        else { w.years_lose = 25 + 5 * r.range(16); w.years_win = 20 + 5 * r.range(7); }
        w.years_win = std::min(w.years_win, std::max(1, owed));
        // the stranger part
        if (tier >= 2 || r.unit() < .3) {
            std::vector<std::string> fresh;
            for (const std::string& f : kForfeits)
                if (std::find(jar.begin(), jar.end(), f) == jar.end()) fresh.push_back(f);
            if (!fresh.empty() && r.unit() < (tier == 1 ? .5 : .8)) w.forfeit = fresh[static_cast<size_t>(r.range(static_cast<int>(fresh.size())))];
        }
        if (!jar.empty() && tier >= 2 && r.unit() < .6) w.prize = "back:" + jar[static_cast<size_t>(r.range(static_cast<int>(jar.size())))];
        else if (r.unit() < (tier == 1 ? .4 : .85)) w.prize = kPrizes[static_cast<size_t>(r.range(static_cast<int>(kPrizes.size())))];
        // the Keeper's patter
        const std::string lose = years_words(w.years_lose) + (w.forfeit.empty() ? "" : ", and " + w.forfeit + " in my jar");
        const std::string win = (w.years_win == 1 ? std::string("a year") : std::to_string(w.years_win) + " years") + " off what you owe" +
                                (w.prize.empty() ? "" : ", and " + prize_words(w.prize));
        switch (tier) {
            case 1: w.pitch = "Nothing too dear. Lose, and it's " + lose + ". Win, and it's " + win + "."; break;
            case 2: w.pitch = "Now we're talking. Lose: " + lose + ". Win: " + win + "."; break;
            default: w.pitch = "All of it, on one barrel. Lose, and you'll serve " + lose + ". Win, and I'll strike " + win + "."; break;
        }
        out.push_back(w);
    }
    return out;
}

}  // namespace ld
