#include "script.hpp"

#include <algorithm>

namespace fp {

const char* number_word(int n, bool capital) {
    static const char* lower[] = {"none", "one", "two", "three", "four"};
    static const char* upper[] = {"None", "One", "Two", "Three", "Four"};
    return (capital ? upper : lower)[n < 0 ? 0 : n > 4 ? 4 : n];
}

std::uint64_t Script::next() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 7; rng_ ^= rng_ << 17;
    return rng_;
}

const char* Script::pick(const std::vector<const char*>& v) { return v[next() % v.size()]; }

std::string Script::target() {
    // a grand deed, an absurd target, sometimes a dramatic flourish; never about harming anyone
    static const std::vector<const char*> deeds = {
        "fold {} into a small paper swan", "replace {} with a cheaper replica", "auction off {} to the highest bidder",
        "seal {} in a jar and label it 'Do Not Open'", "teach {} to bow whenever I enter a room", "rename {} after myself",
        "shrink {} to the size of a teacup", "turn {} inside out", "file {} under 'miscellaneous'", "dismiss {} without a reference",
        "put {} on a strict curfew", "lock {} in the wine cellar", "repaint {} a disappointing beige", "dim {} by thirty percent",
        "unplug {} and plug it back in, slightly wrong", "hang {} above my fireplace", "play {} backwards", "dissolve {} in a cup of tea",
        "polish {} until it apologises", "stretch {} until it squeaks", "replace {} with a strongly worded letter", "iron {} perfectly flat",
        "tuck {} into a drawer and lose the key", "evict {}", "serve {} at dinner, on toast", "outlaw {}", "erase {} from every encyclopedia",
        "send {} to its room", "wrap {} in brown paper and post it to the wrong address", "make {} sign a full confession",
        "steal {} and leave a polite note", "extinguish {}", "cancel {}", "tune {} half a step flat", "photocopy {} until it fades",
        "set {} to the wrong time zone", "knit a cosy over {} so no one can ever see it again", "rearrange {} in alphabetical order",
        "trade {} for a handful of magic beans", "replace {} with a very convincing painting of it",
    };
    static const std::vector<const char*> things = {
        "the Moon", "the Sun", "outer space", "gravity", "every Tuesday", "the colour blue", "the Milky Way", "the Atlantic Ocean",
        "all of next week", "the letter E", "all music", "the North Pole", "every rainbow", "the concept of breakfast", "the weekend",
        "Saturn's rings", "the speed of light", "the Northern Lights", "all the world's umbrellas", "the number seven", "the sky",
        "the alphabet", "every clock on Earth", "the Equator", "the planet Mercury", "the very idea of Wednesday", "the dawn chorus",
        "the tides", "all birthdays", "every left sock", "the horizon", "Mount Everest", "the summer holidays", "the stars",
        "every lemon in existence", "every piano in the world", "the continents", "the smell of rain", "the Arctic", "every sunset",
        "the Big Dipper", "lunchtime", "the colour of the sea", "the word 'please'", "the Pacific Ocean",
    };
    static const std::vector<const char*> flourish = {
        "", "", "", "", " at the stroke of midnight", " while a full orchestra plays", " using nothing but a teaspoon",
        ", and no one will notice until Thursday", " for my own amusement", " with a single press of this console",
        " before a live audience", ", slowly, so that everyone can watch", " before tea", " twice, to be certain",
        " from the comfort of this chair", " purely out of spite", " in the name of tidiness", ", as my father did before me",
        " and then deny everything", " with seventeen trained pigeons",
    };
    for (int tries = 0; tries < 8; ++tries) {
        std::string deed = pick(deeds);
        const size_t k = deed.find("{}");
        deed.replace(k, 2, pick(things));
        std::string fl = pick(flourish);
        if (!fl.empty() && fl[0] == ' ') fl = "," + fl;  // a breath before the flourish
        deed += fl;
        if (std::find(recent_.begin(), recent_.end(), deed) == recent_.end()) {
            recent_.push_back(deed);
            if (recent_.size() > 40) recent_.erase(recent_.begin());
            return deed;
        }
    }
    return recent_.back();
}

std::vector<std::string> Script::oration(const std::string& t) {
    static const std::vector<const char*> openers = {
        "Silence, please. The hour of reckoning has arrived.", "Ah. A witness. How fortunate; history requires one.",
        "Behold the instrument of the world's undoing. Mind the buttons.", "I have waited eleven years for this evening. Do sit.",
        "Tremble, if you would. Quietly. This is a formal occasion.", "Good evening. I am afraid tonight the world ends. Partially.",
        "You find me at the summit of my life's work. Wipe your feet.", "Welcome. You are just in time for the end of everything.",
        "The candles are lit, the silver is polished, and the world is doomed.", "Tonight, the world learns my name. It will not care for it.",
        "Please, be seated. Destiny does not like to be kept waiting.",
    };
    static const std::vector<const char*> threats = {
        "Should you fail to name my code within ten attempts, I shall ", "Fail, and at the stroke of midnight I shall ",
        "If you cannot name my code, then by dawn I shall ", "Fail me, and I shall ", "Name my code, or I shall ",
        "Know what hangs upon your failure: I shall ", "Should you fail, history will record that on this night I chose to ",
    };
    static const std::vector<const char*> challenges = {
        "Four pegs. Six colours. Ten attempts. Begin.", "You may have ten attempts. Do take your time. Or don't.",
        "The code is locked within this console. Ten attempts. I shall be watching.", "Ten attempts, if you please. I have every confidence in your failure.",
        "The fate of all things rests on four small pegs. Begin.", "Proceed. The clock, I assure you, is already running.",
    };
    return {pick(openers), std::string(pick(threats)) + t + ".", pick(challenges)};
}

std::string Script::verdict(Score s, int turns_left) {
    const int e = s.exact, n = s.near;
    if (e == 0 && n == 0) {
        static const std::vector<const char*> zero = {"None of those colours are in my code. Not one.", "Nothing at all. How very thorough of you.",
                                                      "I regret to inform you: not a single peg.", "Wrong in every respect. Remarkable."};
        return pick(zero);
    }
    std::string out;
    if (e == 1) out = "One peg sits exactly where it belongs";
    if (e > 1) out = std::string(number_word(e, true)) + " pegs sit exactly where they belong";
    if (n > 0) {
        if (!out.empty()) out += ". ";
        out += std::string(number_word(n, true)) + (n == 1 ? " is in my code, but misplaced" : " are in my code, but misplaced");
    }
    out += ".";
    if (e == 3) {
        static const std::vector<const char*> close = {" ...How unusual.", " That is all.", " Do not be encouraged.", " A coincidence, I'm sure."};
        out += pick(close);
    } else if (e == 0) {
        static const std::vector<const char*> lost = {" A pity.", " So near, and yet.", " The right colours, poorly arranged.", " Hm."};
        out += pick(lost);
    } else if (turns_left <= 2) {
        out += " Time is short.";
    } else {
        static const std::vector<const char*> mid = {" Interesting.", " Hmm.", " Do continue.", " Adequate.", ""};
        out += pick(mid);
    }
    return out;
}

std::vector<std::string> Script::foiled(const std::string& t) {
    static const std::vector<const char*> exclaim = {
        "Curses! Foiled! FOILED!", "Confound it! CONFOUND IT ALL!", "Blast! Blast and DOUBLE blast!", "Rats! Rats, rats, RATS!",
        "No! NO! Thwarted by a GUEST!", "Drat! Foiled again, by four miserable pegs!", "Impossible! My code! My beautiful code!",
        "Treachery! Who told you? WHO TOLD YOU?", "Curses upon your excellent deductive reasoning!",
    };
    static const std::vector<const char*> lament = {
        "Eleven years of planning, ", "I had the invitations printed, ", "The orchestra was already tuning, ", "I polished the console for this, ",
        "Mother said I would amount to something, ", "The pigeons were already trained, ", "I had chosen the perfect cape for the occasion, ",
        "I had rehearsed my victory speech in the mirror, ",
    };
    static const std::vector<const char*> undone = {
        "and now I shall not ", "and now I may never ", "all so that I could ", "and still I cannot ",
    };
    static const std::vector<const char*> vow = {
        "You have not seen the last of me!", "I shall return, and next time there shall be FIVE pegs!", "This is not over. This is merely... paused.",
        "Mark my words: I shall have my revenge. Politely, but I shall have it.", "Enjoy your victory. It will be brief. Tea is at seven.",
        "Next time, the code shall be in a colour you have never even seen!", "I shall be back. I always come back. It is in my contract.",
        "Very well. Go. Before I compose myself and change my mind.",
    };
    return {pick(exclaim), std::string(pick(lament)) + pick(undone) + t + "!", pick(vow)};
}

std::string Script::jab(int streak) {
    // four themes, three degrees of sharpness; picked so the same jab is not heard twice in a while
    static const std::vector<std::vector<const char*>> gentle = {
        {"Are you quite sure you are up to this?", "Hm. No better, I see.", "Perhaps think before you press the button. Just a suggestion."},       // intelligence
        {"Your fingers are working admirably. The rest, less so.", "A brave attempt. Bravery is not the same as skill.", "I had expected a little more."},  // ability
        {"Do keep trying. It is very touching.", "Such persistence.", "One admires your effort, if not its results."},                          // determination
        {"There is no hurry. The world will end on schedule regardless.", "Patience. Accuracy will follow. Possibly.", "Take your time. I have until midnight."},  // patience
    };
    static const std::vector<std::vector<const char*>> pointed = {
        {"Is it the colours you find confusing, or the counting?", "I could explain the rules again. More slowly, perhaps.", "Remarkable. A new way to be wrong."},
        {"I keep a jigsaw puzzle for guests who find this too demanding.", "Perhaps something with fewer pegs would suit you.", "My late aunt played this better. She was a cat."},
        {"Such persistence. Such... misplaced persistence.", "Do not give up now. Although, perhaps you should.", "Determination is admirable. Direction would be better."},
        {"Patience is a virtue. So, I am told, is thinking.", "I am a patient man. You are testing that.", "You may take as long as you like. It will not help."},
    };
    static const std::vector<std::vector<const char*>> cutting = {
        {"I have met houseplants with a firmer grasp of logic.", "Are you guessing, or simply pressing things?", "Every attempt worse than the last. It is almost a talent."},
        {"I would ask if you are trying, but I fear the answer.", "Truly, you are making this easy for me.", "My pocket watch could do better, and it only knows one thing."},
        {"Still here? Admirable. Futile, but admirable.", "You do not give up. You also do not improve.", "Must we go on? Yes. We must. You must."},
        {"I begin to wonder if you are on my side.", "My patience is endless. My pity is not.", "The pegs are not going to arrange themselves. Though I wish they would."},
    };
    const auto& tier = streak <= 1 ? gentle : streak == 2 ? pointed : cutting;
    for (int tries = 0; tries < 10; ++tries) {
        const auto& theme = tier[next() % tier.size()];
        const std::string j = pick(theme);
        if (std::find(recent_jabs_.begin(), recent_jabs_.end(), j) == recent_jabs_.end()) {
            recent_jabs_.push_back(j);
            if (recent_jabs_.size() > 12) recent_jabs_.erase(recent_jabs_.begin());
            return j;
        }
    }
    return pick(tier[0]);
}

std::string Script::stakes(const std::string& t) {
    static const std::vector<const char*> s = {"Remember what is at stake. Fail, and I shall ", "Do not forget: should you fail, I shall ",
                                               "Every wrong peg brings me closer. I shall ", "Tick, tock. Soon I shall "};
    return std::string(pick(s)) + t + ".";
}

std::string Script::lose(const std::string& t) {
    static const std::vector<const char*> l = {"You have failed. And so, as promised, I shall ", "That concludes your ten. With your leave, I shall now ",
                                               "Time, I'm afraid. Here is the code. And now, as promised, I shall ", "Ten attempts, ten failures. Behold: I shall now "};
    return std::string(pick(l)) + t + ".";
}

std::string Script::gloat() {
    static const std::vector<const char*> g = {"Everything is proceeding EXACTLY as planned!", "Behold! BEHOLD what your failure has wrought!",
                                               "Do not take it personally. I certainly shall not!", "The world is mine! Well. Most of it. The tidy parts!",
                                               "Look upon my works and DESPAIR! Then wipe your feet!", "Tremble! The age of order begins NOW!"};
    return pick(g);
}

std::string Script::muse(const std::string& t) {
    // sometimes a reminder of tonight's plan; sometimes a further scheme entirely
    static const std::vector<const char*> m = {"Take your time. I am only going to ", "While you consider, I shall prepare to ",
                                               "Did I mention I intend to ", "Presently, I shall "};
    static const std::vector<const char*> more = {"And afterwards, I believe I shall also ", "Tomorrow, I think I shall ",
                                                  "Should time allow, I may also ", "For an encore, I shall "};
    static const std::vector<const char*> after = {".", ". It will be very tidy.", ". Then supper."};
    if (next() % 2) return std::string(pick(more)) + target() + ".";
    return std::string(pick(m)) + t + pick(after);
}

std::string Script::poke() {
    static const std::vector<const char*> p = {"Please do not touch the staff.", "I would thank you to keep your hands on the console.",
                                               "Is something the matter?", "I am not a bell to be rung.", "The code, if you please.",
                                               "That will be all."};
    return pick(p);
}

std::string Script::nervous() {
    static const std::vector<const char*> n = {"Three. Who has been talking?", "I am entirely calm. Entirely.", "It remains improbable. Highly."};
    return pick(n);
}

std::string Script::taunt_low(int turns_left) {
    if (turns_left == 1) return "One attempt remains. I suggest you make it count.";
    static const std::vector<const char*> t = {"The hour is getting late.", "Your attempts are running rather low.",
                                               "I do hope you are not keeping me from my plans."};
    return pick(t);
}

}  // namespace fp
