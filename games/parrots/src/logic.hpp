#pragma once
// The logic of the Paradoxiary. Five to ten parrots sit about the aviary. One or two of
// them are liars (every claim they make is false), two know the truth and tell it, and
// the rest are chatterers who know nothing of the matter and only pass the time. One
// bird did the small crime. The player is told how many lie and that two know the
// truth, but not who is playing: they find out by talking to the birds.
//
// A claim is a little formula over who lies, who did it, and where everyone sits (what
// they sit on, in shade or sun, how high, beside whom), so some claims can be checked by
// eye. The generator hides a truth, gives each liar and truth-teller two or three claims
// that are false or true as they must be, and on later tables makes one of them a
// question bird: it says nothing until asked, and of the questions on offer exactly one
// settles the case. The solver enumerates every world (which of the playing birds lie,
// and who did it), so uniqueness is proven, not hoped for.
// Pure and deterministic: no UI, no clock.
#include "perching.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pt {

constexpr int kMaxBirds = 10;

enum class Role : std::uint8_t { chatter, liar, truthful };

// What anyone can see of a bird where it sits.
struct Seat {
    int place = 0;
    int bar = 0;
    Stuff stuff = Stuff::wood;
    bool shade = false;
    bool high = false, low = false;
    int rank = 0;          // height order among the places
    bool red = false;      // a scarlet bird
};

struct Scene {
    std::vector<Seat> seats;
    [[nodiscard]] int birds() const { return static_cast<int>(seats.size()); }
    [[nodiscard]] bool beside(int a, int b) const { return a != b && seats[static_cast<size_t>(a)].bar == seats[static_cast<size_t>(b)].bar; }
    [[nodiscard]] bool alone(int a) const;
    [[nodiscard]] bool higher(int a, int b) const { return seats[static_cast<size_t>(a)].rank > seats[static_cast<size_t>(b)].rank; }
};

// The scene of birds settled on a layout: bird i at place at[i], red where red[i].
Scene scene_of(const Layout& layout, const std::vector<int>& at, const std::vector<bool>& red);

struct World {
    std::uint16_t liars = 0;   // bit i: bird i lies
    int culprit = 0;
    [[nodiscard]] bool lies(int i) const { return (liars >> i & 1) != 0; }
};

enum class Kind : std::uint8_t {
    // about who lies
    is_liar,          // a lies
    is_truthful,      // a does not lie
    exactly_one_liar, // exactly one of a, b lies
    same_kind,        // a and b both lie, or neither does
    // about the crime
    did_it,           // a did it
    didnt_do_it,      // a didn't
    not_me,           // "I didn't do it" (a = the speaker)
    one_of,           // a or b did it
    if_truthful_did,  // if a doesn't lie, b did it
    culprit_lies,     // whoever did it is a liar
    culprit_truthful, // whoever did it doesn't lie
    // the crime and the aviary
    culprit_on,       // whoever did it sits on stuff n
    culprit_shade,    // ... sits in the shade
    culprit_sun,      // ... sits in the sun
    culprit_high,     // ... sits high up
    culprit_low,      // ... sits low down
    culprit_red,      // ... is a red bird
    culprit_not_red,  // ... isn't red
    culprit_beside,   // ... sits beside a
    culprit_alone,    // ... has a bar to themself
    culprit_above,    // ... sits higher than a
    culprit_below,    // ... sits lower than a
    // the aviary alone: true or false for anyone to see
    sits_on,          // a sits on stuff n
    sits_shade,       // a sits in the shade
    sits_sun,         // a sits in the sun
    sits_above,       // a sits higher than b
    sits_beside,      // a sits beside b
};
constexpr int kKinds = static_cast<int>(Kind::sits_beside) + 1;
// Can its truth be seen by looking, whatever the world?
bool by_eye(Kind k);

struct Formula {
    Kind kind = Kind::is_liar;
    int a = 0, b = 0, n = 0;
    [[nodiscard]] bool eval(const World& w, const Scene& s) const;
    bool operator==(const Formula&) const = default;
};

struct Puzzle {
    int birds = 5;
    int liar_count = 1;
    std::vector<Role> roles;                    // the truth
    int culprit = 0;
    std::vector<std::vector<Formula>> claims;   // per bird: what it claims (empty: chatter, or the question bird)
    int asker = -1;                             // the question bird, or -1
    std::vector<Formula> offers;                // the questions it may be asked
    int good = -1;                              // the offer that settles the case
    Scene scene;
    int difficulty = 0;
    [[nodiscard]] std::vector<int> playing() const;   // the liars and truth-tellers
    [[nodiscard]] World truth() const;
};

struct Asked {
    int offer = -1;
    bool yes = false;
};

// The answer the question bird gives to offer `q` in world w.
bool answer(const Puzzle& p, int q, const World& w);

// Every world consistent with what the birds claim, the counts, and the answer if asked.
// The player who has found out which birds are playing reasons over exactly these.
std::vector<World> consistent(const Puzzle& p, const Asked* asked = nullptr);
// The birds who could have done it in those worlds.
std::vector<int> suspects(const Puzzle& p, const Asked* asked = nullptr);

struct GenParams {
    int birds = 6;              // 5..10
    int liars = 1;              // 1 or 2
    bool question = false;      // one playing bird must be asked
    int choices = 3;            // questions on offer
    int tier = 0;               // 0 plain .. 3 everything
    std::uint64_t seed = 1;
};
// A puzzle on `scene` (one seat per bird) whose culprit is certain from the claims (and
// the right answer, when there is a question bird), and not before the question.
Puzzle generate(const GenParams& g, const Scene& scene);

}  // namespace pt
