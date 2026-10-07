#pragma once
// The voices of the table. Every statement is a little formula (logic.hpp);
// this turns it into something a particular parrot would say, in their own
// manner: the posh macaw, the salty sea-dog, the nervous budgie, the gossip,
// the grump, the scholar, the drama queen. Wording is drawn at random each
// time, so the same logic never reads the same way twice, but every phrasing
// means exactly the formula and nothing else.
#include "logic.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pt {

enum class Manner : std::uint8_t { posh, salty, nervous, gossip, grump, scholar, drama, sunny };
constexpr int kManners = 8;

struct Crime {
    std::string what;        // "the last cracker has vanished"
    std::string did;         // "took the last cracker"     ("Bram took the last cracker")
    std::string noun;        // "the cracker thief"         ("Bram is the cracker thief")
    std::string prop;        // what is on the table: "plate", "teapot", "cake", "jar", "button", "mirror"
};
int crime_count();
const Crime& crime(int i);

class Script {
public:
    explicit Script(std::uint64_t seed = 1) : rng_(seed ? seed : 1) {}
    void cast(std::vector<std::string> names, std::vector<Manner> manners, int twins_a, int twins_b, int crime);
    std::string statement(const Statement& s);
    std::string question(const Question& q);      // the player's words, as offered
    std::string reply(int who, bool yes);          // a yes or a no, in their manner
    std::string refuse(int who);                   // a silent parrot, before being asked
    std::string opener(int liars, int parrots);    // the host's introduction: the crime and the count
    std::string guilty(int who);                   // the culprit, caught
    std::string gloat(int who);                    // the culprit, getting away with it
    std::string wrongly(int who);                  // an innocent, wrongly accused
    std::string cheer(int who);
    const std::string& name(int i) const { return names_[static_cast<size_t>(i)]; }

private:
    std::uint64_t rng_;
    std::vector<std::string> names_;
    std::vector<Manner> manners_;
    int twins_a_ = -1, twins_b_ = -1, crime_ = 0;
    int pick(int n);
    bool plain_ = false;  // while phrasing a question: the plainest wording, nothing exclaimed
    const std::string& any(const std::vector<std::string>& v) { return plain_ ? v.front() : v[static_cast<size_t>(pick(static_cast<int>(v.size())))]; }
    std::string liar_word();     // "a liar", "a fibber"...
    std::string honest_word();   // "honest", "truthful"...
    std::string pair(int a, int b);   // "Ada and Bram", or "the twins"
    std::string core(const Formula& f, int speaker);  // the bare claim, in the speaker's grammar
    std::string dress(int who, const std::string& claim, bool lead = true);  // wrapped in their manner
};

const char* manner_name(Manner m);

}  // namespace pt
