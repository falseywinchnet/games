#pragma once
// Who lives in the maze now: the meme residents. Three come by to talk (the Shiba, Apu
// looking for his mommy, and the Doomer, who would like to know if you've seen the exit);
// the rest keep to their places: Ceiling Cat and the Goose upside down on the ceiling,
// Keyboard Cat just beyond a portal, Philosoraptor in the reward room staring at the
// trophy, and Monke, sometimes, in the elevator. The giant marble may be a monocled frog on
// a penny-farthing, the Thinking Face, or Mazeball instead. Their pictures are painted in
// authoring/maze_posters (residents.py) and loaded from the assets; without them the
// residents stay home (nothing is drawn, and the old marble rolls).
#include "soft3d.hpp"

#include <string>
#include <vector>

namespace mz {

enum class Resident {
    shiba, apu, doomer, monke, ceiling_cat, keyboard_cat, goose, raptor_think, raptor_fold, thinker, hamster, frog0, frog1, frog2, frog3, googly, count
};

void use_resident_art(const std::string& dir);  // the residents folder; call before the first is drawn
const Tex32* resident_tex(Resident r);          // null without the art

struct Talker {
    std::string name;
    Resident look;
    std::vector<std::string> lines;
    double w, h;   // size in the maze (world units)
    double lift;   // how far above the floor the picture's foot is
};
const std::vector<Talker>& visitors();  // the ones who come by: the Shiba, Apu, the Doomer

// the things the stay-at-homes say
const std::vector<std::string>& keyboard_cat_lines();
const std::vector<std::string>& raptor_lines();
const std::vector<std::string>& frog_lines();     // when you stand in his way
const std::vector<std::string>& thinker_lines();  // the Thinking Face, rolling by or stopped
const std::vector<std::string>& monke_lines();

// The giant ball: what it is this level.
enum class Ball { rock, frog, thinker, mazeball, count };

struct Credit {
    std::string name, note;
    Resident look;
};
const std::vector<Credit>& resident_credits();
const Tex32* portrait_of(const std::string& who);  // a speaker's picture, for the speech bubble; null if none

}  // namespace mz
