#include "brain.hpp"

#include <algorithm>
#include <cmath>

namespace ld {

namespace {
std::uint64_t fnv(const std::string& s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }

struct Seed {
    const char* name;
    Species species;
    Voice voice;
    const char* blurb;
};
// The crew, four of each kind. Their numbers come from their names; their natures lean by kind.
const Seed kSeeds[32] = {
    {"Pinchy Pete", Species::crab, Voice::gruff, "a crab who counts every die twice"},
    {"Old Clack", Species::crab, Voice::slow, "older than the wreck, and twice as stubborn"},
    {"Duchess Shellby", Species::crab, Voice::posh, "nobility, of a sort, from a very small rock"},
    {"Scuttle Jack", Species::crab, Voice::chirpy, "never sits still, never stops grinning"},
    {"Inkwell Ida", Species::octopus, Voice::sly, "eight arms, and an ace up every one"},
    {"Eight-Arm Ezra", Species::octopus, Voice::chirpy, "plays four games at once, and wins three"},
    {"Madame Mollusca", Species::octopus, Voice::posh, "reads palms. All eight of them"},
    {"Squidge", Species::octopus, Voice::slow, "soft-spoken, and softer still in the head"},
    {"Rattlebones Rook", Species::skeleton, Voice::gruff, "lost his flesh at cards; still plays"},
    {"Grinning Gil", Species::skeleton, Voice::chirpy, "can't stop smiling; has no lips to stop it with"},
    {"Lady Marrow", Species::skeleton, Voice::posh, "a duchess once, and bones now"},
    {"Captain No-Nose", Species::skeleton, Voice::eerie, "went down with his ship, and stayed"},
    {"Big Gus Grouper", Species::grouper, Voice::slow, "large, friendly, and easily muddled"},
    {"Mrs. Haddock", Species::grouper, Voice::posh, "keeps a tidy reef and a tidier score"},
    {"Codsworth", Species::grouper, Voice::gruff, "the locker's butler, off duty"},
    {"Flounder Finch", Species::grouper, Voice::chirpy, "both eyes on one side, both on your cup"},
    {"Slick Nell", Species::eel, Voice::sly, "slippery in every sense"},
    {"Zap Malone", Species::eel, Voice::chirpy, "electric company"},
    {"Wriggles", Species::eel, Voice::eerie, "nobody knows where Wriggles came from"},
    {"Sir Eelington", Species::eel, Voice::posh, "knighted by a mermaid, he says"},
    {"Grandpa Shell", Species::turtle, Voice::slow, "has seen every bluff there is, twice"},
    {"Slow Moe", Species::turtle, Voice::slow, "takes his time, and usually your dice"},
    {"Tortuga Tess", Species::turtle, Voice::gruff, "ran a tavern on her back for a century"},
    {"Barnaby Barnacle", Species::turtle, Voice::chirpy, "more barnacle than turtle by now"},
    {"Toothy Tom", Species::shark, Voice::gruff, "smells a bluff from a league away"},
    {"Fin Mackenzie", Species::shark, Voice::sly, "circles, and circles, and calls"},
    {"Hammerhead Hal", Species::shark, Voice::chirpy, "thinks sideways"},
    {"Smiling Sal", Species::shark, Voice::posh, "all teeth and good manners"},
    {"Pale Percy", Species::ghost, Voice::eerie, "a cabin boy who never left"},
    {"The Widow Wail", Species::ghost, Voice::eerie, "still waiting at the window"},
    {"Misty Molly", Species::ghost, Voice::chirpy, "here one moment, gone the next"},
    {"Old Fog", Species::ghost, Voice::slow, "mostly fog, a little old man"},
};
// Each one's measured strength: their win rate in 40,000 random three-handed games among the crew (tools/ladder
// "bake"; the tests check it still holds after any tuning).
const double kRating[32] = {
    0.288,  // Pinchy Pete
    0.282,  // Old Clack
    0.336,  // Duchess Shellby
    0.382,  // Scuttle Jack
    0.352,  // Inkwell Ida
    0.357,  // Eight-Arm Ezra
    0.295,  // Madame Mollusca
    0.369,  // Squidge
    0.355,  // Rattlebones Rook
    0.333,  // Grinning Gil
    0.366,  // Lady Marrow
    0.359,  // Captain No-Nose
    0.321,  // Big Gus Grouper
    0.291,  // Mrs. Haddock
    0.323,  // Codsworth
    0.331,  // Flounder Finch
    0.345,  // Slick Nell
    0.381,  // Zap Malone
    0.308,  // Wriggles
    0.323,  // Sir Eelington
    0.389,  // Grandpa Shell
    0.380,  // Slow Moe
    0.307,  // Tortuga Tess
    0.404,  // Barnaby Barnacle
    0.322,  // Toothy Tom
    0.331,  // Fin Mackenzie
    0.218,  // Hammerhead Hal
    0.374,  // Smiling Sal
    0.306,  // Pale Percy
    0.345,  // The Widow Wail
    0.260,  // Misty Molly
    0.339,  // Old Fog
};
// four different tells for each kind: no two characters share both kind and tell
const Tell kTells[8][4] = {
    {Tell::tap, Tell::twitch, Tell::bubbles, Tell::glance},   // crab
    {Tell::flush, Tell::tap, Tell::glance, Tell::sway},       // octopus
    {Tell::tap, Tell::lean, Tell::twitch, Tell::glance},      // skeleton
    {Tell::bubbles, Tell::blink, Tell::sway, Tell::lean},     // grouper
    {Tell::sway, Tell::flush, Tell::blink, Tell::glance},     // eel
    {Tell::blink, Tell::lean, Tell::bubbles, Tell::tap},      // turtle
    {Tell::twitch, Tell::glance, Tell::lean, Tell::blink},    // shark
    {Tell::flush, Tell::sway, Tell::blink, Tell::bubbles},    // ghost
};
// leanings by kind: bluff, nerve, greed, skill, adapt (added to a hashed spread)
const double kLean[8][5] = {
    {-.06, .02, -.05, .12, .05},   // crab: tight and careful
    {.02, .0, .0, .18, .2},        // octopus: clever, and quick to learn you
    {.06, -.04, .15, -.02, -.05},  // skeleton: bold
    {-.04, -.06, -.05, -.18, -.15},// grouper: muddled, trusting
    {.15, .0, .05, .05, .05},      // eel: bluffs a lot
    {-.08, -.08, -.12, .08, .1},   // turtle: patient, seldom calls
    {.0, .1, .1, .08, .1},         // shark: calls hard
    {.08, .0, .0, -.05, -.05},     // ghost: erratic
};
}  // namespace

const std::vector<Character>& cast() {
    static const std::vector<Character> c = [] {
        std::vector<Character> v;
        for (int i = 0; i < 32; ++i) {
            const Seed& s = kSeeds[i];
            Rng r(fnv(s.name));
            const int k = static_cast<int>(s.species);
            const double* L = kLean[k];
            Character ch;
            ch.name = s.name;
            ch.species = s.species;
            ch.look = i % 4;
            ch.voice = s.voice;
            ch.tell = kTells[k][i % 4];
            ch.bluff = std::clamp(.18 + L[0] + (r.unit() - .5) * .2, .04, .45);
            ch.nerve = std::clamp(.42 + L[1] + (r.unit() - .5) * .16, .28, .6);
            ch.greed = std::clamp(.3 + L[2] + (r.unit() - .5) * .3, .05, .7);
            ch.skill = std::clamp(.55 + L[3] + (r.unit() - .5) * .55, .1, .95);
            ch.adapt = std::clamp(.45 + L[4] + (r.unit() - .5) * .4, .05, .95);
            ch.tell_bluff = .38 + r.unit() * .3;
            ch.tell_honest = .05 + r.unit() * .07;
            ch.blurb = s.blurb;
            ch.rating = kRating[i];
            ch.danger = ch.rating < .31 ? 1 : ch.rating < .355 ? 2 : 3;
            v.push_back(ch);
        }
        return v;
    }();
    return c;
}

const char* species_name(Species s) {
    static const char* n[] = {"crab", "octopus", "skeleton", "grouper", "eel", "turtle", "shark", "ghost"};
    return n[static_cast<int>(s)];
}

const char* tell_name(Tell t) {
    static const char* n[] = {"glances down at their own cup", "blinks twice, quickly", "leans back", "taps their cup",
                              "flushes a shade darker", "lets slip a few bubbles", "sways a little", "twitches"};
    return n[static_cast<int>(t)];
}

bool was_bluff(const std::vector<int>& own, Bid b, int total_dice) {
    if (b.none()) return false;
    int support = 0;
    for (int v : own) support += v == b.face || v == 1;
    if (support == 0) return true;
    const double others = (total_dice - static_cast<int>(own.size())) / 3.0;
    return support <= 1 && b.qty > support + others + 1.5;
}

double chance_true(const Match& m, int me, Bid b, const Reading& r, double skill) {
    if (b.none()) return 1;
    int known = 0;
    for (int v : m.dice[static_cast<size_t>(me)]) known += v == b.face || v == 1;
    const int need = b.qty - known;
    if (need <= 0) return 1;
    // the unknown dice, seat by seat: a third show the face (or a one), more if that seat has bid on it and is trusted
    std::vector<double> dist{1.0};
    for (int j = 0; j < m.players; ++j) {
        if (j == me || !m.alive(j)) continue;
        double p = 1.0 / 3;
        bool bid_it = false;
        for (const BidRec& h : m.history) bid_it = bid_it || (h.who == j && h.bid.face == b.face);
        if (bid_it) {
            const double bl = j < static_cast<int>(r.bluff.size()) ? r.bluff[static_cast<size_t>(j)] : kTypicalBluff;
            p += clamp01(1 - bl) * skill * .22;
        }
        const int n = m.count[static_cast<size_t>(j)];
        for (int d = 0; d < n; ++d) {
            std::vector<double> nx(dist.size() + 1, 0.0);
            for (size_t k = 0; k < dist.size(); ++k) {
                nx[k] += dist[k] * (1 - p);
                nx[k + 1] += dist[k] * p;
            }
            dist.swap(nx);
        }
    }
    double tail = 0;
    for (size_t k = static_cast<size_t>(need); k < dist.size(); ++k) tail += dist[k];
    return tail;
}

Decision decide(const Match& m, int me, const Character& c, const Reading& r, Rng& rng) {
    const auto& own = m.dice[static_cast<size_t>(me)];
    const int own_n = static_cast<int>(own.size());
    // a less skilled player misjudges the odds a little, every time
    const double wobble = (1 - c.skill) * .3;
    auto noisy = [&](double p) { return clamp01(p + (rng.unit() + rng.unit() - 1) * wobble); };
    auto support = [&](int f) { int s = 0; for (int v : own) s += v == f || v == 1; return s; };

    struct Cand { Bid b; double p; int sup; };
    std::vector<Cand> cands;
    for (int f = 2; f <= 6; ++f) {
        Bid b{m.bid.none() ? 1 : (f > m.bid.face ? m.bid.qty : m.bid.qty + 1), f};
        if (m.bid.none()) {
            // an opening: about what's likely, shaded by greed
            const double expect = support(f) + (m.total_dice() - own_n) / 3.0;
            b.qty = std::max(1, static_cast<int>(std::floor(expect * (.5 + .3 * c.greed))));
        }
        if (b.qty > m.total_dice()) continue;
        cands.push_back({b, noisy(chance_true(m, me, b, r, c.skill)), support(f)});
    }
    Decision d;
    // the standing bid: likely enough to let stand?
    if (!m.bid.none()) {
        const double pt = noisy(chance_true(m, me, m.bid, r, c.skill));
        double best = 0;
        for (const Cand& k : cands) best = std::max(best, k.p);
        // a novice calls on gut feeling (their nerve); a sharp player weighs the call against their best raise
        double thresh = (1 - c.skill) * c.nerve + c.skill * std::clamp(best * .8, .3, .55);
        const double bb = m.bidder >= 0 && m.bidder < static_cast<int>(r.bluff.size()) ? r.bluff[static_cast<size_t>(m.bidder)] : kTypicalBluff;
        thresh += (bb - kTypicalBluff) * .8;  // a known bluffer gets called sooner
        if (cands.empty() || pt < thresh || (best < .3 && pt < .55)) {
            d.call = true;
            d.confidence = 1 - pt;
            return d;
        }
    }
    // the honest raise: the likeliest, favouring a face they hold
    const Cand* pick = nullptr;
    for (const Cand& k : cands)
        if (!pick || k.p + .03 * k.sup > pick->p + .03 * pick->sup) pick = &k;
    Cand chosen = *pick;
    // sometimes a bluff: a face they barely hold, if it's still believable
    if (rng.unit() < c.bluff) {
        const Cand* bl = nullptr;
        const double floor = .3 + .08 * (1 - c.greed);
        for (const Cand& k : cands)
            if (k.sup < chosen.sup && k.sup <= own_n / 4 && k.p >= floor && (!bl || k.p > bl->p)) bl = &k;
        if (bl) { chosen = *bl; d.bluffing = true; }
    }
    // a greedy player jumps the quantity when the odds allow
    if (rng.unit() < c.greed * .5) {
        const double target = .62 - .2 * c.greed;
        for (int up = 0; up < 3; ++up) {
            Bid nb{chosen.b.qty + 1, chosen.b.face};
            if (nb.qty > m.total_dice()) break;
            const double p = noisy(chance_true(m, me, nb, r, c.skill));
            if (p < target) break;
            chosen.b = nb;
            chosen.p = p;
        }
    }
    d.bid = chosen.b;
    d.confidence = chosen.p;
    if (chosen.p < .45) d.bluffing = true;
    d.tell = rng.unit() < (d.bluffing ? c.tell_bluff : c.tell_honest);
    return d;
}

// ------------------------------------------------------------------ voices
namespace {
const std::string& any(Rng& r, const std::vector<std::string>& v) { return v[static_cast<size_t>(r.range(static_cast<int>(v.size())))]; }
std::string cap(std::string s) { if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 32); return s; }
}  // namespace

std::string line_bid(const Character& c, Bid b, Rng& r) {
    const std::string w = bid_words(b), W = cap(w);
    switch (c.voice) {
        case Voice::gruff: return any(r, {W + ".", W + ", and I'll hear no lip about it.", "Make it " + w + ".", W + ". Your move, barnacle."});
        case Voice::posh: return any(r, {"I believe " + w + ", if you please.", W + ", I should think.", "Let us say " + w + "."});
        case Voice::eerie: return any(r, {W + "...", "Ooooh... " + w + ".", "The deep whispers... " + w + "."});
        case Voice::chirpy: return any(r, {W + "! Ha!", "Ooh, " + w + "!", W + ", easy!", "Let's go " + w + "!"});
        case Voice::slow: return any(r, {"Mmm... " + w + ".", "Let me see... " + w + ".", W + ". I think. Yes."});
        case Voice::sly: return any(r, {W + ". Or am I?", "Oh, " + w + ", I'd wager.", W + ". Trust me."});
    }
    return W + ".";
}

std::string line_open(const Character& c, Bid b, Rng& r) {
    const std::string w = bid_words(b), W = cap(w);
    switch (c.voice) {
        case Voice::gruff: return any(r, {"I'll start. " + W + ".", W + " to begin."});
        case Voice::posh: return any(r, {"Shall we? " + W + ".", "To open: " + w + "."});
        case Voice::eerie: return any(r, {"It begins... " + w + ".", W + ", to start the long night."});
        case Voice::chirpy: return any(r, {"Me first! " + W + "!", "Opening with " + w + "!"});
        case Voice::slow: return any(r, {"Well now... " + w + ".", "Mmm. " + W + ", to start."});
        case Voice::sly: return any(r, {"Let's warm up. " + W + ".", W + ". Just to get us going."});
    }
    return W + ".";
}

std::string line_call(const Character& c, Rng& r) {
    switch (c.voice) {
        case Voice::gruff: return any(r, {"Liar!", "Bilge! Lift 'em!", "Liar, I say!"});
        case Voice::posh: return any(r, {"I'm afraid that's a fib.", "Liar, I dare say.", "Do lift your cup, dear."});
        case Voice::eerie: return any(r, {"Liiiar...", "The dead know a lie...", "Lift... your... cups..."});
        case Voice::chirpy: return any(r, {"Liar liar!", "Nope! Show me!", "No way! Liar!"});
        case Voice::slow: return any(r, {"Hmm... no. Liar.", "I don't... believe that. Liar."});
        case Voice::sly: return any(r, {"Ohh, I don't think so.", "Liar. Prove me wrong.", "Pretty bid. Shame it's a lie."});
    }
    return "Liar!";
}

std::string line_caught(const Character& c, Rng& r) {
    switch (c.voice) {
        case Voice::gruff: return any(r, {"Blast and barnacles.", "Hmph. Fair cop.", "Curse my luck."});
        case Voice::posh: return any(r, {"How frightfully embarrassing.", "Well. That was a fib.", "Oh, bother."});
        case Voice::eerie: return any(r, {"The sea takes another...", "Ahh... found out...", "Ooooh..."});
        case Voice::chirpy: return any(r, {"Whoops!", "Aw, nuts!", "Worth a try!"});
        case Voice::slow: return any(r, {"Oh. Oh dear.", "Hmm. Too many, then.", "Ah. Not quite."});
        case Voice::sly: return any(r, {"You got lucky.", "Hm. Well played. This time.", "Tch."});
    }
    return "Drat.";
}

std::string line_wrong_call(const Character& c, Rng& r) {
    switch (c.voice) {
        case Voice::gruff: return any(r, {"What? Count 'em again!", "Bah! Rigged!", "Barnacles!"});
        case Voice::posh: return any(r, {"Oh. I do beg your pardon.", "How very surprising.", "Well, I never."});
        case Voice::eerie: return any(r, {"The truth... how cruel...", "Ohhh... it was true..."});
        case Voice::chirpy: return any(r, {"No way!", "Huh! Really?", "Aw, c'mon!"});
        case Voice::slow: return any(r, {"Oh. It was true.", "Hmm. I see. Yes."});
        case Voice::sly: return any(r, {"So you weren't bluffing. Noted.", "Hm. You'll pay for that."});
    }
    return "Drat.";
}

std::string line_gloat(const Character& c, Rng& r) {
    switch (c.voice) {
        case Voice::gruff: return any(r, {"Ha! Hand it over.", "Knew it.", "Into the jar with it!"});
        case Voice::posh: return any(r, {"Thank you kindly.", "Most gracious of you.", "Delightful."});
        case Voice::eerie: return any(r, {"One more for the deep...", "Hehehe..."});
        case Voice::chirpy: return any(r, {"Yes! Ha ha!", "Called it!", "Woo!"});
        case Voice::slow: return any(r, {"Mm. Thought so.", "There we are."});
        case Voice::sly: return any(r, {"Too easy.", "Read you like a tide chart."});
    }
    return "Ha!";
}

std::string line_out(const Character& c, Rng& r) {
    switch (c.voice) {
        case Voice::gruff: return any(r, {"I'm sunk. Deal me out.", "Bah. I'll be at the bar."});
        case Voice::posh: return any(r, {"I shall retire to the gallery.", "Do carry on without me."});
        case Voice::eerie: return any(r, {"Back... into the dark...", "I fade..."});
        case Voice::chirpy: return any(r, {"Aww, I'm out!", "Next time!"});
        case Voice::slow: return any(r, {"Oh. That's me done, then.", "Time for a nap."});
        case Voice::sly: return any(r, {"I'll be watching.", "Enjoy it while it lasts."});
    }
    return "I'm out.";
}

std::string line_win(const Character& c, Rng& r) {
    switch (c.voice) {
        case Voice::gruff: return any(r, {"Pay up, landlubber.", "The locker thanks you."});
        case Voice::posh: return any(r, {"A splendid game. For me.", "Do give the Keeper my regards."});
        case Voice::eerie: return any(r, {"Stay... stay forever...", "Welcome to the deep..."});
        case Voice::chirpy: return any(r, {"I win! I win!", "Again! Again!"});
        case Voice::slow: return any(r, {"Well. That's that.", "Mm. Good game."});
        case Voice::sly: return any(r, {"Never bet against the house.", "Better luck in a hundred years."});
    }
    return "I win.";
}

std::string line_greet(const Character& c, Rng& r) {
    switch (c.voice) {
        case Voice::gruff: return any(r, {"Sit down. Shake your cup.", "Another one for the table."});
        case Voice::posh: return any(r, {"Charmed, I'm sure.", "Do join us."});
        case Voice::eerie: return any(r, {"Another soul... welcome...", "Sit... sit..."});
        case Voice::chirpy: return any(r, {"Ooh, a new one!", "Hi! Let's play!"});
        case Voice::slow: return any(r, {"Evening.", "Pull up a barrel."});
        case Voice::sly: return any(r, {"Fresh meat.", "Feeling lucky?"});
    }
    return "Hello.";
}

}  // namespace ld
