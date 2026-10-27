#include "residents.hpp"

#include "png.hpp"

#include <array>
#include <utility>
#include <memory>

namespace mz {
namespace {

const char* const kFiles[] = {"shiba", "apu", "doomer", "monke", "ceiling_cat", "keyboard_cat", "goose", "raptor_think", "raptor_fold", "thinker",
                              "hamster", "frog_0", "frog_1", "frog_2", "frog_3", "googly"};
constexpr int kCount = static_cast<int>(Resident::count);
static_assert(sizeof kFiles / sizeof kFiles[0] == kCount);

std::string& art_dir() {
    static std::string dir;
    return dir;
}

}  // namespace

void use_resident_art(const std::string& dir) { art_dir() = dir; }

const Tex32* resident_tex(Resident r) {
    static std::array<std::unique_ptr<Tex32>, kCount> loaded;
    static std::array<bool, kCount> tried{};
    const int i = static_cast<int>(r);
    if (i < 0 || i >= kCount || art_dir().empty()) return nullptr;
    if (!tried[static_cast<size_t>(i)]) {
        tried[static_cast<size_t>(i)] = true;
        std::unique_ptr<Tex32> t = std::make_unique<Tex32>();
        if (load_png(art_dir() + "/" + kFiles[i] + ".png", *t)) loaded[static_cast<size_t>(i)] = std::move(t);
    }
    return loaded[static_cast<size_t>(i)].get();
}

const std::vector<Talker>& visitors() {
    static const std::vector<Talker> v = {
        {"Shiba",
         Resident::shiba,
         {"wow.", "such maze. very lost. wow.", "much corridor. so wall.", "very turn. many left. wow.", "so carpet. much pattern.", "such {R}. very want.",
          "many door. much pad. wow.", "plz no bonk.", "wow. such ceiling.", "much exit. very not here.", "so snail. such paint. wow.",
          "very explore. much proud. wow."},
         .78, .78, 0},
        {"Apu",
         Resident::apu,
         {"have you seen my mommy?", "my mommy said wait here. i don't know where here is.", "i brought her a spatula. for pancakes.",
          "teddy is scared. i'm not scared. teddy is.", "mommy? MOMMY?", "is this the way home?", "she has a blue shirt like mine. have you seen her?",
          "i made pancakes. they're somewhere in the maze.", "teddy says left. i say right. we are lost.",
          "will you help me look? ...okay. thank you anyway."},
         .62, .62, 0},
        {"Doomer",
         Resident::doomer,
         {"Have you seen the exit?", "Have you seen the exit? ...Yeah. Me neither.", "I've been walking these halls since 2012.",
          "Seen the exit? Doesn't matter. There's always another maze.", "It's 3 a.m. in here. It's always 3 a.m. in here.",
          "Have you seen the exit? Asking for a friend. I don't have friends.", "Another corridor. Another night in.",
          "Still looking for the exit? Must be nice, having hope."},
         .7, .7, .28},
    };
    return v;
}

const std::vector<std::string>& keyboard_cat_lines() {
    static const std::vector<std::string> v = {"Come closer.", "Closer...", "A little closer.", "Step through. I'll play you off.", "Come on. I don't bite. I play.",
                                               "Closer. I have a song for you.", "You're almost here.", "Just one more step."};
    return v;
}

const std::vector<std::string>& thinker_lines() {
    static const std::vector<std::string> v = {
        "Hmm.",
        "If I roll all the way round, have I been anywhere?",
        "Which wall did they build first? Hmm.",
        "Am I rolling, or is the maze going by?",
        "A dead end is just a beginning facing the other way.",
        "Hmm. Hmm. ...Hmm.",
        "I had a thought. It rolled off.",
        "Every corridor here is the same length, if you don't measure them.",
        "Do the bricks know they're a maze?",
        "If you follow me, and I follow the walls, who leads?",
        "I'm not round. I'm thinking very hard in every direction.",
        "What if the exit is the entrance, and we are just early?",
        "Somebody painted that snail. Who taught the snail?",
        "I could stop. But then I'd only be a face.",
        "Left is right, from the other side. Hmm.",
        "You look like someone who has thought about mazes.",
        "The trophy room is always the last place you look. Why is that?",
        "Hmm. Let me think about that while I roll.",
        "If the lights go out, is the maze still there?",
        "Excuse me. I'm in the middle of a thought.",
    };
    return v;
}

const std::vector<std::string>& raptor_lines() {
    static const std::vector<std::string> v = {
        "If the reward is in the room, and I am in the room, am I the reward?",
        "Is a maze with one exit a maze, or just a long corridor, folded?",
        "If you always turn left, are you lost, or merely consistent?",
        "Does the trophy want to be found?",
        "If a snail paints a wall and no one sees it, has the maze been redecorated?",
        "Is the shortest path still a path if no one takes it?",
        "If I stare at the trophy long enough, does it stare back?",
        "Who mazes the maze?",
        "If you leave the maze, is the maze still lost?",
        "Is a dead end the end, or just the maze being honest?",
        "When the maze flips, which of us is upside down?",
        "Can a wall be in the way if it was there first?",
        "If every corridor looks the same, did you ever move?",
        "Is an exit still an exit if it is also an entrance?",
        "If I cannot reach the trophy with these arms, is it mine to want?",
        "Is getting lost the walker's fault, or the map's?",
        "If the marble rolls forever, is it going anywhere?",
        "Does the cat on the ceiling think we are on the ceiling?",
        "If you find the reward, what were you before?",
        "Is wandering just arriving very slowly?",
        "If the maze is new every time, have you ever been here?",
        "Why a reward room? Why not a reward reward?",
        "I think, therefore I am. But why am I a velociraptor?",
        "Is the door open, or is the wall just absent?",
        "If the goose honks and no one hears it, is it still a threat?",
        "If the trophy is golden, is the room the gilded part?",
        "Am I guarding the trophy, or is the trophy guarding me?",
    };
    return v;
}

const std::vector<std::string>& frog_lines() {
    static const std::vector<std::string> v = {"I say! Do you mind?", "Ahem. Right of way, if you please.", "Kindly step aside. I'm on a penny-farthing.",
                                               "Out of the way, you absolute scoundrel.", "Ding ding! DING DING!", "One does not simply stop a penny-farthing.",
                                               "Good heavens. Move."};
    return v;
}

const std::vector<std::string>& monke_lines() {
    static const std::vector<std::string> v = {"Return to monke.", "Ooh ooh.", "Reject maze. Return to monke.", "Monke ride elevator. Monke happy.",
                                               "Up. Monke like up.", "Monke has seen the exit. Monke does not care."};
    return v;
}

const std::vector<Credit>& resident_credits() {
    static const std::vector<Credit> v = {
        {"Shiba", "after the doge meme; drawn for PlaySuite", Resident::shiba},
        {"Apu", "after Apu Apustaja; drawn for PlaySuite", Resident::apu},
        {"Monke", "after Return to Monke; drawn for PlaySuite", Resident::monke},
        {"Doomer", "after the Doomer Wojak", Resident::doomer},
        {"Ceiling Cat", "after the lolcat", Resident::ceiling_cat},
        {"Keyboard Cat", "after the meme; a tune of our own", Resident::keyboard_cat},
        {"The Goose", "honk", Resident::goose},
        {"Philosoraptor", "after the meme", Resident::raptor_think},
        {"The frog", "on a penny-farthing, after Dat Boi", Resident::frog0},
        {"The Thinking Face", "after the emoji", Resident::thinker},
    };
    return v;
}

const Tex32* portrait_of(const std::string& who) {
    for (const Talker& t : visitors())
        if (t.name == who) return resident_tex(t.look);
    static const std::pair<const char*, Resident> others[] = {
        {"Ceiling Cat", Resident::ceiling_cat}, {"The Goose", Resident::goose}, {"Keyboard Cat", Resident::keyboard_cat}, {"Philosoraptor", Resident::raptor_think},
        {"The frog", Resident::frog0}, {"Monke", Resident::monke}, {"The Thinking Face", Resident::thinker}, {"Mazeball", Resident::googly}};
    for (const std::pair<const char*, Resident>& o : others)
        if (who == o.first) return resident_tex(o.second);
    return nullptr;
}

}  // namespace mz
