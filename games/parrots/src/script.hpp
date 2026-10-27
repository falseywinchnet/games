#pragma once
// The voices of the aviary. Every claim is a little formula (logic.hpp); this turns it
// into something a particular parrot would say, in their own manner: the posh macaw, the
// salty sea-dog, the nervous budgie, the gossip, the grump, the scholar, the drama queen,
// the sunny one. Wording is drawn afresh every time, so the same claim never reads the
// same way twice, but every phrasing means exactly the formula and nothing else.
//
// And the chatter (chatter.cpp): what every bird says when it has nothing to say about
// the case, which is most of the time for most of them. Itself, its perch and its
// likings (always true), its kind of parrot, the weather beyond the glass, what day it
// is, the aviary, food, jokes, idle thoughts: a large library and generators that put
// its parts together, so a bird rarely repeats itself.
#include "logic.hpp"
#include "perching.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pt {

enum class Manner : std::uint8_t { posh, salty, nervous, gossip, grump, scholar, drama, sunny };
constexpr int kManners = 8;
const char* manner_name(Manner m);

struct Crime {
    std::string what;        // "Someone has eaten the keeper's mango"
    std::string did;         // "ate the mango"                 ("Bram ate the mango")
    std::string noun;        // "the mango thief"               ("Bram is the mango thief")
};
int crime_count();
const Crime& crime(int i);

class Script {
public:
    explicit Script(std::uint64_t seed = 1) : rng_(seed ? seed : 1) {}
    // The table's birds: names, manners, species, likings and where they sit.
    void cast(std::vector<std::string> names, std::vector<Manner> manners, std::vector<int> species, std::vector<Bird> birds, Scene scene, int crime);
    // Which birds lie: a liar's talk of itself is false too (it claims likings its seat
    // does not meet), so a sharp eye can sometimes catch one.
    void set_liars(std::vector<bool> liars) { liars_ = std::move(liars); }
    std::string claim(int who, const Formula& f);      // a claim, in the speaker's manner
    std::string question(int to, const Formula& f);    // the player's words, as offered
    std::string reply(int who, bool yes);              // a yes or a no, in their manner
    std::string ask_me(int who);                       // the question bird, before being asked
    std::string chatter(int who, int weekday);         // something about nothing (chatter.cpp)
    std::string opener(int liars, int birds);          // the keeper's notice: the crime and the counts
    std::string guilty(int who);                       // the culprit, caught
    std::string gloat(int who);                        // the culprit, getting away with it
    std::string wrongly(int who);                      // an innocent, wrongly accused
    std::string cheer(int who);
    const std::string& name(int i) const { return names_[static_cast<size_t>(i)]; }
    int pick(int n);

private:
    std::uint64_t rng_;
    std::vector<std::string> names_;
    std::vector<Manner> manners_;
    std::vector<int> species_;
    std::vector<Bird> birds_;
    Scene scene_;
    std::vector<bool> liars_;
    int crime_ = 0;
    bool plain_ = false;  // while phrasing a question: the plainest wording, nothing exclaimed
    const std::string& any(const std::vector<std::string>& v) { return plain_ ? v.front() : v[static_cast<size_t>(pick(static_cast<int>(v.size())))]; }
    Manner manner(int who) const { return manners_.empty() ? Manner::sunny : manners_[static_cast<size_t>(who) % manners_.size()]; }
    std::string liar_word();
    std::string truthful_word();
    std::string where(Stuff s);                         // "on the rope", "on the brass"...
    std::string core(const Formula& f, int speaker);    // the bare claim, in the speaker's grammar
    std::string dress(int who, const std::string& claim, bool lead = true);
    // chatter.cpp
    std::string about_self(int who);
    std::string about_kind(int who);
    std::string about_weather();
    std::string about_day(int weekday);
    std::string about_aviary();
    std::string about_food();
    std::string joke();
    std::string musing();
    std::string story();
    std::string about_others(int who);   // idle words about another bird: never about the case
    std::string ask_player();
    std::string song();
};

}  // namespace pt
