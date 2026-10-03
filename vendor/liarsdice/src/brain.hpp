#pragma once
// The crew of the locker: a fixed cast of 32 drowned and briny gamblers, and
// how they play. Every character is generated once, deterministically, from
// their name: how often they bluff, how readily they call, how greedy their
// raises are, how well they reckon odds, how quickly they catch on to *your*
// bluffing, and their tell: a small habit they show far more often when
// they're lying than when they're not (a glance at their own cup, a flush of
// colour, a nervous tap). Faint, but distinct, and theirs alone.
#include "rules.hpp"

#include <string>
#include <vector>

namespace ld {

enum class Species : std::uint8_t { crab, octopus, skeleton, grouper, eel, turtle, shark, ghost };
constexpr int kSpeciesCount = 8;
enum class Tell : std::uint8_t { glance, blink, lean, tap, flush, bubbles, sway, twitch };
constexpr int kTellCount = 8;
enum class Voice : std::uint8_t { gruff, posh, eerie, chirpy, slow, sly };

struct Character {
    std::string name;
    Species species = Species::crab;
    int look = 0;            // 0..3: colours and what they wear, within the species
    Voice voice = Voice::gruff;
    Tell tell = Tell::glance;
    double bluff = .2;       // how often they bid on a face they don't hold
    double nerve = .4;       // they call when they think the bid is likelier false than this
    double greed = .3;       // how hard they push their raises
    double skill = .5;       // how well they reckon the odds and read the others' bids
    double adapt = .5;       // how quickly they catch on to your bluffing
    double tell_bluff = .5;  // chance of showing the tell when bluffing...
    double tell_honest = .1; // ...and when not
    double rating = .333;    // their measured win rate among the crew, three-handed
    int danger = 1;          // 1..3, from the rating: for choosing tables
    std::string blurb;       // a line for the wager card
};
const std::vector<Character>& cast();     // 32 of them, always the same
const char* species_name(Species s);
const char* tell_name(Tell t);             // "glances at their own cup"

constexpr double kTypicalBluff = .12;  // how often a bid is a bluff, before anyone knows better

// What a player at the table believes about the others: how far to trust each one's bids.
struct Reading {
    std::vector<double> bluff;   // per seat: estimated bluff rate
};
// The record of the human's bidding, kept across games: every bid seen at a reveal, and the bluffs among them.
struct PlayerRecord {
    double bids = 0, bluffs = 0;  // decayed counts
    double estimate() const { return (kTypicalBluff * 8 + bluffs) / (8.0 + bids); }
    void observe(bool bluffed) { bids = bids * .97 + 1; bluffs = bluffs * .97 + (bluffed ? 1 : 0); }
};
// How a character reads the human: their record, weighed by how quickly this character catches on.
inline double read_player(const Character& c, const PlayerRecord& rec) { return kTypicalBluff + c.adapt * (rec.estimate() - kTypicalBluff) * 1.5; }
// Was a bid a bluff, judged after the reveal from the bidder's own dice?
bool was_bluff(const std::vector<int>& own, Bid b, int total_dice);

// The chance a bid is true, seen from seat `me` (their own dice known, the rest guessed, the others' bids read).
double chance_true(const Match& m, int me, Bid b, const Reading& r, double skill);

struct Decision {
    bool call = false;
    Bid bid;
    bool bluffing = false;   // a raise they don't believe
    bool tell = false;       // whether the tell shows on this move
    double confidence = 0;   // their belief in what they did
};
Decision decide(const Match& m, int me, const Character& c, const Reading& r, Rng& rng);

// Their voice: what they say as they bid, call, lose a die, go out, win.
std::string line_bid(const Character& c, Bid b, Rng& rng);
std::string line_open(const Character& c, Bid b, Rng& rng);
std::string line_call(const Character& c, Rng& rng);
std::string line_caught(const Character& c, Rng& rng);     // their bid was false
std::string line_wrong_call(const Character& c, Rng& rng); // their call was wrong
std::string line_gloat(const Character& c, Rng& rng);      // someone else lost the die to them
std::string line_out(const Character& c, Rng& rng);        // their last die is gone
std::string line_win(const Character& c, Rng& rng);        // they won the match
std::string line_greet(const Character& c, Rng& rng);

}  // namespace ld
