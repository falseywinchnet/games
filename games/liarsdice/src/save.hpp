#pragma once
// The ledger: what you owe the locker, what the Keeper holds of yours, what
// you've won, your notes on the crew, how you bid (as the crew remember it),
// the wagers on offer, and any game in progress, dice and all (so quitting
// never re-rolls a bad hand). A versioned, checksummed text file, replaced
// atomically.
#include "brain.hpp"
#include "wager.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ld {

constexpr int kStartOwed = 100;

struct CrewNotes {
    int games = 0;          // games you've sat down with them
    int beaten = 0;         // games you won with them at the table
    int bids_seen = 0;      // their bids you saw revealed
    int bluffs_seen = 0;    // ...that were bluffs
    int tells_caught = 0;   // bluffs revealed on which they'd shown their tell
};

struct Ledger {
    int owed = kStartOwed;
    std::vector<std::string> jar;        // what the Keeper holds of yours
    std::vector<std::string> trophies;   // what you've won
    int won = 0, lost = 0;
    int served = 0, struck = 0;          // years added, years struck off, all told
    bool freed = false;                  // the debt has been paid at least once
    PlayerRecord rec;
    std::array<CrewNotes, 32> notes{};
    std::uint64_t offer_seed = 1;        // the wagers now on the barrel
    bool sound = true, music = true;
    // a game in progress
    bool in_match = false;
    Wager wager;
    Match match;
    std::uint64_t rng = 1;
};

std::string serialize(const Ledger& l);
bool parse(const std::string& text, Ledger& l);
bool save_ledger(const std::string& path, const Ledger& l);
bool load_ledger(const std::string& path, Ledger& l);

}  // namespace ld
