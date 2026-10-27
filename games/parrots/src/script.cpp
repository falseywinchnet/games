#include "script.hpp"

#include <algorithm>
#include <cctype>

namespace pt {

namespace {
const std::vector<Crime>& crimes() {
    static const std::vector<Crime> v = {
        {"Someone has eaten the keeper's mango", "ate the keeper's mango", "the mango thief"},
        {"The orchids have been nibbled", "nibbled the orchids", "the orchid nibbler"},
        {"Somebody rang the dinner bell at dawn", "rang the dinner bell at dawn", "the dawn bell-ringer"},
        {"The keeper's spectacles are missing", "pinched the keeper's spectacles", "the spectacle snatcher"},
        {"There are seed husks in the fountain", "spat husks into the fountain", "the husk spitter"},
        {"Someone has been pulling the labels off the palms", "pulled the labels off the palms", "the label puller"},
        {"The watering can is full of grapes", "filled the watering can with grapes", "the grape hoarder"},
        {"Somebody taught the gardener's boy a very rude word", "taught the gardener's boy the rude word", "the rude-word teacher"},
        {"The butterflies have been chased into the ferns", "chased the butterflies", "the butterfly chaser"},
        {"Someone ate the bananas, skins and all", "ate the bananas", "the banana bandit"},
        {"The keeper's hat is at the bottom of the bird bath", "dropped the keeper's hat in the bird bath", "the hat dunker"},
        {"Someone has unravelled a whole rope swing", "unravelled the rope swing", "the rope picker"},
    };
    return v;
}
std::string lower_first(std::string s, const std::vector<std::string>& names) {
    if (s.empty()) return s;
    for (const std::string& n : names)
        if (s.rfind(n, 0) == 0) return s;  // starts with a name: keep the capital
    if (s.rfind("I ", 0) == 0 || s.rfind("I'", 0) == 0) return s;
    s[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
    return s;
}
std::string upper_first(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}
std::string strip_stop(std::string s) {
    while (!s.empty() && (s.back() == '.' || s.back() == '!')) s.pop_back();
    return s;
}
std::string number_word(int n) {
    static const char* w[] = {"none", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"};
    return n >= 0 && n <= 10 ? w[n] : std::to_string(n);
}
}  // namespace

int crime_count() { return static_cast<int>(crimes().size()); }
const Crime& crime(int i) { return crimes()[static_cast<size_t>(((i % crime_count()) + crime_count()) % crime_count())]; }

const char* manner_name(Manner m) {
    static const char* n[] = {"posh", "salty", "nervous", "gossip", "grump", "scholar", "drama", "sunny"};
    return n[static_cast<int>(m)];
}

int Script::pick(int n) {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 7;
    rng_ ^= rng_ << 17;
    return n <= 1 ? 0 : static_cast<int>(rng_ % static_cast<std::uint64_t>(n));
}

void Script::cast(std::vector<std::string> names, std::vector<Manner> manners, std::vector<int> species, std::vector<Bird> birds, Scene scene, int c) {
    names_ = std::move(names);
    manners_ = std::move(manners);
    species_ = std::move(species);
    birds_ = std::move(birds);
    scene_ = std::move(scene);
    crime_ = c;
}

std::string Script::liar_word() { return any({"a liar", "lying", "a fibber", "telling porkies", "not to be trusted", "full of feathers and fibs"}); }
std::string Script::truthful_word() { return any({"telling the truth", "truthful", "honest", "on the level", "straight as a perch", "as honest as the day is long"}); }

std::string Script::where(Stuff s) {
    switch (s) {
    case Stuff::wood: return any({"on wood", "on a wooden perch", "on a branch"});
    case Stuff::rope: return any({"on rope", "on a rope perch", "on one of the ropes"});
    case Stuff::brass: return any({"on brass", "on the brass", "on the brass hoop"});
    case Stuff::iron: return any({"on iron", "on the ironwork", "on the old lamp"});
    case Stuff::stone: return any({"on stone", "on the stonework", "on a stone rim"});
    }
    return "somewhere";
}

// The claim itself. `speaker` speaks in the first person about themself.
std::string Script::core(const Formula& f, int speaker) {
    const Crime& cr = crime(crime_);
    const int nb = static_cast<int>(names_.size());
    auto nm = [&](int i) { return i >= 0 && i < nb ? name(i) : std::string("?"); };
    const std::string A = nm(f.a), B = nm(f.b);
    const bool me = f.a == speaker;
    const std::string S = where(static_cast<Stuff>(f.n));
    switch (f.kind) {
    case Kind::is_liar:
        return any({A + " is " + liar_word() + ".", "Don't believe a word " + A + " says.", "Everything " + A + " says is false.", A + " never tells the truth.",
                    A + " lies, through and through."});
    case Kind::is_truthful:
        return any({A + " is " + truthful_word() + ".", A + " tells the truth.", "You can trust " + A + ".", A + " wouldn't fib to save their tail feathers.",
                    "Every word out of " + A + "'s beak is true."});
    case Kind::exactly_one_liar:
        return any({"Exactly one of " + A + " and " + B + " is lying.", "Of " + A + " and " + B + ", one lies and the other doesn't.",
                    "One of " + A + " and " + B + " is honest and the other is a liar."});
    case Kind::same_kind:
        return any({A + " and " + B + " are birds of a feather: both liars, or both honest.", "If " + A + " lies, so does " + B + ", and if " + A + " doesn't, neither does " + B + ".",
                    A + " and " + B + " are the same sort, honest or not."});
    case Kind::did_it:
        return any({A + " " + cr.did + ".", "It was " + A + ". " + A + " " + cr.did + ".", A + " is " + cr.noun + ".", A + " " + cr.did + ", plain and simple."});
    case Kind::didnt_do_it:
        return any({A + " didn't do it.", "It wasn't " + A + ".", A + " is innocent.", A + " is not " + cr.noun + "."});
    case Kind::not_me:
        if (me) return any({"I didn't do it!", "It wasn't me.", "I'm innocent, I tell you.", "Not me. Never. I'd never."});
        return any({A + " didn't do it.", "It wasn't " + A + "."});
    case Kind::one_of:
        return any({A + " " + cr.did + ", or " + B + " did.", "It was either " + A + " or " + B + ".", upper_first(cr.noun) + " is " + A + " or " + B + "."});
    case Kind::if_truthful_did:
        return any({"If " + A + " is honest, then " + B + " " + cr.did + ".", "If " + A + " tells the truth, it was " + B + ".", B + " " + cr.did + ", if " + A + " is honest."});
    case Kind::culprit_lies:
        return any({"Whoever " + cr.did + " is a liar.", upper_first(cr.noun) + " is a liar, mark my words.", "The one who " + cr.did + " doesn't tell the truth."});
    case Kind::culprit_truthful:
        return any({"Whoever " + cr.did + " isn't a liar.", upper_first(cr.noun) + " doesn't lie, oddly enough.", "The one who " + cr.did + " is no fibber."});
    case Kind::culprit_on:
        return any({"Whoever " + cr.did + " is sitting " + S + ".", upper_first(cr.noun) + " is perched " + S + ".", "Look " + S + ". That's where " + cr.noun + " sits."});
    case Kind::culprit_shade:
        return any({"Whoever " + cr.did + " is sitting in the shade.", upper_first(cr.noun) + " is hiding in the shade.", "The one who " + cr.did + " keeps to the shade."});
    case Kind::culprit_sun:
        return any({"Whoever " + cr.did + " is sitting in the sun.", upper_first(cr.noun) + " is out in the sunshine, bold as brass.", "The one who " + cr.did + " sits in the sun."});
    case Kind::culprit_high:
        return any({"Whoever " + cr.did + " is sitting up high.", upper_first(cr.noun) + " is perched high up.", "Look up. " + upper_first(cr.noun) + " sits high."});
    case Kind::culprit_low:
        return any({"Whoever " + cr.did + " is sitting low down.", upper_first(cr.noun) + " sits near the floor.", "Look low. " + upper_first(cr.noun) + " is down there."});
    case Kind::culprit_red:
        return any({"Whoever " + cr.did + " is a red bird.", upper_first(cr.noun) + " has red feathers.", "It was a red one. " + upper_first(cr.noun) + " is red."});
    case Kind::culprit_not_red:
        return any({"Whoever " + cr.did + " isn't a red bird.", upper_first(cr.noun) + " hasn't a red feather on them.", "It wasn't a red one."});
    case Kind::culprit_beside:
        if (me) return any({"Whoever " + cr.did + " is sitting right beside me.", "It was my neighbour. Someone on my perch " + cr.did + ".", upper_first(cr.noun) + " shares my perch."});
        return any({"Whoever " + cr.did + " is sitting beside " + A + ".", upper_first(cr.noun) + " shares a perch with " + A + ".", "It was " + A + "'s neighbour."});
    case Kind::culprit_alone:
        return any({"Whoever " + cr.did + " has a perch all to themself.", upper_first(cr.noun) + " sits alone.", "The one who " + cr.did + " sits by themself, no neighbours."});
    case Kind::culprit_above:
        if (me) return any({"Whoever " + cr.did + " is sitting higher than me.", upper_first(cr.noun) + " is perched above me.", "Look above me. That's where " + cr.noun + " is."});
        return any({"Whoever " + cr.did + " is sitting higher than " + A + ".", upper_first(cr.noun) + " is perched above " + A + "."});
    case Kind::culprit_below:
        if (me) return any({"Whoever " + cr.did + " is sitting lower than me.", upper_first(cr.noun) + " is perched below me.", "Look below me. That's where " + cr.noun + " is."});
        return any({"Whoever " + cr.did + " is sitting lower than " + A + ".", upper_first(cr.noun) + " is perched below " + A + "."});
    case Kind::sits_on:
        if (me) return any({"I'm sitting " + S + ".", "As you can see, I'm " + S + ".", "Me? I'm " + S + "."});
        return any({A + " is sitting " + S + ".", A + "'s perched " + S + "."});
    case Kind::sits_shade:
        if (me) return any({"I'm sitting in the shade.", "I'm in the shade, nice and cool."});
        return any({A + " is sitting in the shade.", A + " is keeping to the shade."});
    case Kind::sits_sun:
        if (me) return any({"I'm sitting in the sun.", "I'm out in the sunshine."});
        return any({A + " is sitting in the sun.", A + " is out in the sunshine."});
    case Kind::sits_above:
        if (f.a == speaker) return any({"I'm sitting higher than " + B + ".", "I'm perched above " + B + "."});
        if (f.b == speaker) return any({A + " is sitting higher than me.", A + " is perched above me."});
        return any({A + " is sitting higher than " + B + ".", A + " is perched above " + B + "."});
    case Kind::sits_beside:
        if (f.a == speaker || f.b == speaker) {
            const std::string other = f.a == speaker ? B : A;
            return any({other + " is sitting right beside me.", "I share my perch with " + other + "."});
        }
        return any({A + " and " + B + " share a perch.", A + " is sitting beside " + B + "."});
    }
    return "...";
}

std::string Script::dress(int who, const std::string& claim, bool lead) {
    // `wrap` forms keep the claim as its own sentence; `lead` forms run into it, which only reads
    // well for a plain statement, so exclamations get wraps only
    const std::string l = lower_first(claim, names_);
    std::vector<std::string> wrap, leads;
    switch (manner(who)) {
    case Manner::posh: wrap = {claim + " Quite so.", claim, claim + " One does notice these things."}; leads = {"I dare say " + l, "One hates to gossip, but " + l, "Frightfully sorry, but " + l}; break;
    case Manner::salty: wrap = {"Arr! " + claim, claim + " Arr.", "Listen here, matey. " + claim, claim + " That's the truth of it, or I'm a seagull."}; leads = {"Shiver me feathers, " + l}; break;
    case Manner::nervous: wrap = {"Um... " + claim, claim + " ...sorry.", "Oh dear. " + claim, "Don't look at me like that! " + claim}; leads = {"W-well, " + l}; break;
    case Manner::gossip: wrap = {claim + " Ooh, scandal!", "Darling! " + claim}; leads = {"Darling, " + l, "You didn't hear it from me, but " + l, "Ooh, between you and me: " + l}; break;
    case Manner::grump: wrap = {"Hmph. " + claim, claim + " Now leave me alone.", "Bah. " + claim, claim + " Obviously."}; break;
    case Manner::scholar: wrap = {claim + " Q.E.D.", "I have considered the matter carefully. " + claim}; leads = {"Logically speaking, " + l, "Observe: " + l}; break;
    case Manner::drama: wrap = {"Oh, the horror! " + claim, claim + " I shall never recover!", "Gasp! " + claim}; leads = {"It pains me to say it, but " + l}; break;
    case Manner::sunny: wrap = {"Ooh! " + claim, claim + " Isn't this fun?", "Hello! " + claim, claim + " Hee hee."}; break;
    }
    if (lead) wrap.insert(wrap.end(), leads.begin(), leads.end());
    return wrap.empty() ? claim : any(wrap);
}

std::string Script::claim(int who, const Formula& f) { return dress(who, core(f, who)); }

std::string Script::question(int to, const Formula& f) {
    plain_ = true;
    std::string c = strip_stop(core(f, -1));
    plain_ = false;
    if (f.kind == Kind::not_me && f.a == to) c = "you didn't do it";
    return name(to) + ", is it true that " + lower_first(c, names_) + "?";
}

std::string Script::reply(int who, bool yes) {
    switch (manner(who)) {
    case Manner::posh: return yes ? any({"Indeed it is.", "Quite true.", "Yes, naturally."}) : any({"Certainly not.", "Good heavens, no.", "No, no, no."});
    case Manner::salty: return yes ? any({"Aye!", "Aye, that it is.", "Arr, yes."}) : any({"Nay!", "Arr, no.", "Not on yer life."});
    case Manner::nervous: return yes ? any({"Y-yes.", "Yes? Yes.", "I... yes."}) : any({"N-no.", "No! I mean, no.", "Um, no."});
    case Manner::gossip: return yes ? any({"Oh, absolutely, darling.", "Yes, and I've more where that came from.", "Mm-hm, yes."}) : any({"Not a chance, darling.", "Ooh, no.", "Nope, not a word of it."});
    case Manner::grump: return yes ? any({"Yes. Obviously.", "Yes. Go away.", "Hmph. Yes."}) : any({"No. Obviously.", "No. Go away.", "Hmph. No."});
    case Manner::scholar: return yes ? any({"Affirmative.", "That is correct.", "Yes, demonstrably."}) : any({"Negative.", "That is incorrect.", "No, demonstrably."});
    case Manner::drama: return yes ? any({"YES! A thousand times yes!", "Yes... yes it is!", "It's true! All of it!"}) : any({"NO! Never!", "How dare you! No!", "No, no, a thousand times no!"});
    case Manner::sunny: return yes ? any({"Yep!", "Yes yes yes!", "Uh-huh!"}) : any({"Nope!", "Nuh-uh!", "No way!"});
    }
    return yes ? "Yes." : "No.";
}

std::string Script::ask_me(int who) {
    switch (manner(who)) {
    case Manner::posh: return any({"I shan't volunteer a thing. But one may ask me a question.", "Ask me properly, and I shall answer. One question."});
    case Manner::salty: return any({"I ain't sayin' nothin'. Ask me straight. Just the one.", "Arr. One question, and make it count."});
    case Manner::nervous: return any({"I'd... rather not say. Unless you ask? Only one, please.", "Oh, don't make me volunteer anything. Ask me something?"});
    case Manner::gossip: return any({"Oh, I know things. Ask me nicely. One question, darling.", "My beak is sealed. Mostly. Ask away, once."});
    case Manner::grump: return any({"Not talking. Ask a question if you must. One.", "Hmph. One question. Then shoo."});
    case Manner::scholar: return any({"I respond only to well-formed questions. You may pose one.", "Pose a question and I shall answer it. Choose carefully."});
    case Manner::drama: return any({"My lips are sealed! Unless asked! Once!", "I cannot speak of it! Ask me! One question!"});
    case Manner::sunny: return any({"Ask me something! Go on! Just one!", "I've got a secret! You can ask me one thing!"});
    }
    return "Ask me.";
}

std::string Script::opener(int liars, int birds) {
    const Crime& cr = crime(crime_);
    const std::string L = number_word(liars);
    const std::string lies = liars == 1 ? "one of them is a liar" : L + " of them are liars";
    return any({cr.what + "! " + upper_first(number_word(birds)) + " parrots in the aviary: " + lies + ", two know the truth, and the rest are no help at all. Who " + cr.did + "?",
                cr.what + ". Of these " + number_word(birds) + " birds, " + lies + " and two tell the truth. The others just chatter. Find out who " + cr.did + ".",
                "Keeper's notice: " + lower_first(cr.what, names_) + ". " + upper_first(lies) + ". Two birds know the truth. Who " + cr.did + "?"});
}

std::string Script::guilty(int who) {
    return dress(who, any({"Fine! It was me! I " + crime(crime_).did + "!", "Caught. I " + crime(crime_).did + ", and I'd do it again.", "Squawk! You got me!"}), false);
}
std::string Script::gloat(int who) {
    return dress(who, any({"Ha! It was me all along! I " + crime(crime_).did + "!", "Wrong bird! It was me, and I'm off!", "Hee hee. I " + crime(crime_).did + ". Bye!"}), false);
}
std::string Script::wrongly(int who) { return dress(who, any({"Me? I never!", "How dare you!", "Wrong bird, I'm afraid.", "I've been framed!"}), false); }
std::string Script::cheer(int who) { return dress(who, any({"Justice!", "Well reasoned!", "Hooray!", "I knew it all along."}), false); }

std::string Script::chatter(int who, int weekday) {
    std::string line;
    switch (pick(24)) {
    case 0: case 1: case 2: line = about_self(who); break;
    case 3: case 4: line = about_kind(who); break;
    case 5: case 6: line = about_weather(); break;
    case 7: line = about_day(weekday); break;
    case 8: case 9: line = about_aviary(); break;
    case 10: case 11: line = about_food(); break;
    case 12: case 13: case 14: line = joke(); break;
    case 15: case 16: line = musing(); break;
    case 17: case 18: line = story(); break;
    case 19: case 20: line = about_others(who); break;
    case 21: line = ask_player(); break;
    default: line = song(); break;
    }
    // a manner's flourish, now and then
    return pick(3) == 0 ? dress(who, line, false) : line;
}

}  // namespace pt
