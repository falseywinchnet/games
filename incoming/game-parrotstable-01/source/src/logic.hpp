#pragma once
// The logic of the Parrot's Table. Each parrot at the table is honest (every
// word true) or a liar (every word false), and exactly one of them did the
// small crime. Statements are little formulas over who is honest and who did
// it. A world is consistent when every speaker's statement is true exactly
// when the speaker is honest, the number of liars is the number announced,
// and every answer to a question asked fits the same rule.
//
// The generator hides a truth, deals statements that are true or false as the
// speaker demands, and keeps a few parrots tight-beaked: they say nothing until
// asked. The solver enumerates every world (at most 7 parrots: 128 honesty
// patterns times 7 culprits), so uniqueness is proven, not hoped for. A second,
// human-like solver grades the deduction: how many single steps of inference
// it takes, and how many times it has to suppose something and find it absurd.
// Pure and deterministic: no UI, no clock.
#include <cstdint>
#include <string>
#include <vector>

namespace pt {

constexpr int kMaxParrots = 7;

struct World {
    std::uint8_t liars = 0;   // bit i set: parrot i lies
    int culprit = 0;
    bool honest(int i) const { return !(liars >> i & 1); }
};

// A formula. Kinds name the statement templates; a, b are parrots; n is a number.
enum class Kind : std::uint8_t {
    is_liar,          // a lies
    is_honest,        // a tells the truth
    both_liars,       // a and b both lie
    both_honest,      // a and b both tell the truth
    exactly_one_liar, // exactly one of a, b lies
    same_kind,        // a and b are the same (both honest or both liars)
    did_it,           // a did it
    didnt_do_it,      // a did not do it
    one_of,           // a did it, or b did
    if_honest_did,    // if a is honest, then b did it
    culprit_lies,     // whoever did it is a liar
    culprit_honest,   // whoever did it is honest
    count_liars,      // exactly n of us lie
    at_least_liars,   // at least n of us lie
    neighbour_did,    // one of the birds sitting either side of a did it (they sit in a row; the ends have one neighbour)
    not_me,           // "I didn't do it" (a = the speaker)
};

struct Formula {
    Kind kind = Kind::is_liar;
    int a = 0, b = 0, n = 0;
    bool eval(const World& w, int parrots) const;
};

struct Statement {
    int speaker = 0;
    Formula f;
};

// A question the player may put to a silent parrot: "Is it true that ...?"
struct Question {
    int to = 0;       // who is asked
    Formula f;        // what is asked
};
// The answer a parrot gives in world w: honest parrots say yes when it is true, liars when it is false.
bool answer(const Question& q, const World& w, int parrots);

struct Puzzle {
    int parrots = 4;
    int liar_count = 1;
    std::vector<Statement> said;           // statements volunteered
    std::vector<int> silent;               // parrots who wait to be asked
    std::vector<std::vector<Question>> offers;  // per silent parrot: the multiple-choice questions on offer
    int asks = 0;                          // how many questions the player may ask in all
    World truth;
    int twins_a = -1, twins_b = -1;        // a pair of twins, if the table has them (names only)
    int difficulty = 0;                    // graded deduction effort
    int steps = 0, suppositions = 0;       // what the grade is made of
};

struct Asked {
    Question q;
    bool yes = false;
};

// Every world consistent with the statements, the liar count and the answers so far.
std::vector<World> consistent(const Puzzle& p, const std::vector<Asked>& asked);

// Grade the deduction with a human-like solver: single steps of inference over
// facts ("X is honest", "Y didn't do it") and, when stuck, a supposition that is
// carried to a contradiction. Returns false if it cannot finish.
bool grade(const Puzzle& p, const std::vector<Asked>& asked, int& steps, int& suppositions);

struct GenParams {
    int parrots = 4;
    int silent = 0;          // parrots who must be asked
    int asks = 0;            // questions allowed
    int choices = 3;         // options offered per silent parrot
    bool twins = false;
    int tier = 0;            // which statement kinds may appear (0 plain .. 3 everything)
    std::uint64_t seed = 1;
};
// A puzzle whose truth is unique once the right question(s) are asked (if any
// are needed), and not before. At least one offered question leads to the
// unique answer; at least one offered question does not.
Puzzle generate(const GenParams& g);

// For the hint and the tests: does asking `q` (given the answers so far) leave exactly one world?
bool settles(const Puzzle& p, const std::vector<Asked>& asked, const Question& q);

}  // namespace pt
