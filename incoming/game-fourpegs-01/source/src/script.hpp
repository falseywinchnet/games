#pragma once
// What he says, in the measured tones of a gentleman's gentleman: generated
// plans and orations, and verdicts that state the counts the pins show. Seeded, so every line can be reproduced.
#include "board.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fp {

class Script {
public:
    explicit Script(std::uint64_t seed = 1) : rng_(seed * 0x9E3779B97F4A7C15ULL | 1) {}

    std::string target();                         // something he means to destroy, freshly chosen per game
    std::vector<std::string> oration(const std::string& target);  // the new-game speech, 3-4 lines
    std::string verdict(Score s, int turns_left); // the counts, in his words
    std::vector<std::string> foiled(const std::string& target);  // "curses, foiled again": exclamation, lament, vow
    std::string stakes(const std::string& target);  // a reminder of what failure means
    // a subtle jab when a guess is no better than the player's best so far; `streak`
    // counts guesses in a row without improvement and sharpens the tone
    std::string jab(int streak);
    std::string lose(const std::string& target);  // triumph
    std::string gloat();                          // the quiet satisfaction after
    std::string muse(const std::string& target);  // an idle threat while you think
    std::string poke();                           // touched by the player
    std::string nervous();                        // three exact
    std::string taunt_low(int turns_left);        // running out of turns

private:
    std::uint64_t rng_;
    std::vector<std::string> recent_;  // plans already used, so they do not repeat soon
    std::vector<std::string> recent_jabs_;
    const char* pick(const std::vector<const char*>& v);
    std::uint64_t next();
};

// "one", "two"... for counts in speech
const char* number_word(int n, bool capital = false);

}  // namespace fp
