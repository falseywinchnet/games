#include "script.hpp"

#include <algorithm>
#include <cctype>

namespace pt {

namespace {
const std::vector<Crime>& crimes() {
    static const std::vector<Crime> v = {
        {"The last cracker has vanished", "took the last cracker", "the cracker thief", "plate"},
        {"Somebody knocked the teapot over", "knocked the teapot over", "the teapot toppler", "teapot"},
        {"Someone has pecked a hole in the birthday cake", "pecked the cake", "the cake pecker", "cake"},
        {"The sugar lumps are gone", "pinched the sugar lumps", "the sugar thief", "jar"},
        {"The captain's shiny button is missing", "stole the shiny button", "the button snatcher", "button"},
        {"Someone hid the mirror", "hid the mirror", "the mirror hider", "mirror"},
        {"There are seed husks all over the tablecloth", "spat the seed husks", "the husk spitter", "plate"},
        {"Somebody taught the kettle to whistle rude songs", "taught the kettle the rude song", "the kettle's teacher", "teapot"},
        {"The good napkins have been shredded", "shredded the napkins", "the napkin shredder", "plate"},
        {"Someone ate the decorations off the cake", "ate the cake decorations", "the decoration gobbler", "cake"},
    };
    return v;
}
std::string number_word(int n) {
    static const char* w[] = {"none", "one", "two", "three", "four", "five", "six", "seven"};
    return n >= 0 && n <= 7 ? w[n] : std::to_string(n);
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

void Script::cast(std::vector<std::string> names, std::vector<Manner> manners, int twins_a, int twins_b, int c) {
    names_ = std::move(names);
    manners_ = std::move(manners);
    twins_a_ = twins_a;
    twins_b_ = twins_b;
    crime_ = c;
}

std::string Script::liar_word() { return any({"lying", "a liar", "a fibber", "telling porkies", "a dirty great liar", "not to be trusted"}); }
std::string Script::honest_word() { return any({"honest", "truthful", "telling the truth", "as honest as the day is long", "on the level", "straight as a perch"}); }

std::string Script::pair(int a, int b) {
    if (twins_a_ >= 0 && ((a == twins_a_ && b == twins_b_) || (a == twins_b_ && b == twins_a_)) && pick(3) != 0) return "the twins";
    return name(a) + " and " + name(b);
}

// The claim itself. `speaker` speaks in the first person about themself.
std::string Script::core(const Formula& f, int speaker) {
    const Crime& cr = crime(crime_);
    const std::string A = f.a >= 0 && f.a < static_cast<int>(names_.size()) ? name(f.a) : "?", B = f.b >= 0 && f.b < static_cast<int>(names_.size()) ? name(f.b) : "?";
    const std::string N = number_word(f.n);
    switch (f.kind) {
        case Kind::is_liar:
            return any({A + " is " + liar_word() + ".", "Don't believe a word " + A + " says.", "Everything " + A + " says is false.", A + " never tells the truth.",
                        A + " is a fibber, through and through."});
        case Kind::is_honest:
            return any({A + " is " + honest_word() + ".", A + " tells the truth.", "You can trust every word " + A + " says.", A + " wouldn't fib to save their tail feathers.",
                        "Every word out of " + A + "'s beak is true."});
        case Kind::both_liars:
            return any({upper_first(pair(f.a, f.b)) + " are both lying.", "Neither " + A + " nor " + B + " tells the truth.", upper_first(pair(f.a, f.b)) + "? Liars, the pair of them.",
                        "Both " + A + " and " + B + " are fibbers."});
        case Kind::both_honest:
            return any({upper_first(pair(f.a, f.b)) + " both tell the truth.", A + " and " + B + " are honest, both of them.", "You can trust " + A + ", and you can trust " + B + "."});
        case Kind::exactly_one_liar:
            return any({"Exactly one of " + pair(f.a, f.b) + " is lying.", "Of " + pair(f.a, f.b) + ", one lies and the other doesn't.",
                        "One of " + pair(f.a, f.b) + " is honest and the other is a liar.", "Only one of " + pair(f.a, f.b) + " tells the truth."});
        case Kind::same_kind:
            return any({A + " and " + B + " are birds of a feather: either both honest, or both liars.", "If " + A + " lies, so does " + B + ", and if " + A + " is honest, so is " + B + ".",
                        A + " and " + B + " are the same sort: both truthful or both fibbers."});
        case Kind::did_it:
            return any({A + " " + cr.did + ".", "It was " + A + ". " + A + " " + cr.did + ".", A + " is " + cr.noun + ".", A + " " + cr.did + ", plain and simple."});
        case Kind::didnt_do_it:
            return any({A + " didn't do it.", "It wasn't " + A + ".", A + " is innocent.", A + " is not " + cr.noun + "."});
        case Kind::one_of:
            return any({A + " " + cr.did + ", or " + B + " did.", "It was either " + A + " or " + B + ".", upper_first(cr.noun) + " is " + A + " or " + B + ".",
                        "One of " + A + " and " + B + " " + cr.did + "."});
        case Kind::if_honest_did:
            return any({"If " + A + " is honest, then " + B + " " + cr.did + ".", "If " + A + " tells the truth, it was " + B + ".",
                        B + " " + cr.did + ", if " + A + " is honest."});
        case Kind::culprit_lies:
            return any({"Whoever " + cr.did + " is a liar.", upper_first(cr.noun) + " is a liar, mark my words.", "The one who " + cr.did + " doesn't tell the truth."});
        case Kind::culprit_honest:
            return any({"Whoever " + cr.did + " is honest.", upper_first(cr.noun) + " tells the truth, oddly enough.", "The one who " + cr.did + " is an honest bird."});
        case Kind::count_liars:
            if (f.n == 1) return any({"Exactly one of us is lying.", "There is exactly one liar at this table.", "Just one of us lies. Exactly one."});
            return any({"Exactly " + N + " of us are lying.", "There are exactly " + N + " liars at this table.", "Count them: exactly " + N + " of us lie."});
        case Kind::at_least_liars:
            if (f.n == 1) return any({"At least one of us is lying.", "There's at least one liar at this table."});
            return any({"At least " + N + " of us are lying.", "There are " + N + " liars here, or more."});
        case Kind::neighbour_did:
            if (f.a == speaker) return any({"One of my neighbours " + cr.did + ".", "It was someone sitting right next to me.", "Look either side of me. One of them " + cr.did + "."});
            return any({"One of " + A + "'s neighbours " + cr.did + ".", "It was someone sitting next to " + A + "."});
        case Kind::not_me:
            if (f.a == speaker) return any({"I didn't do it!", "It wasn't me.", "I'm innocent, I tell you.", "Not me. Never. I'd never."});
            return any({A + " didn't do it.", "It wasn't " + A + "."});
    }
    return "...";
}

std::string Script::dress(int who, const std::string& claim, bool lead) {
    // `wrap` forms keep the claim as its own sentence; `lead` forms run into it ("Darling, ada is..."), which only
    // reads well for a plain statement, so exclamations ("Ha! It was me!") get wraps only
    const Manner m = manners_.empty() ? Manner::sunny : manners_[static_cast<size_t>(who) % manners_.size()];
    const std::string l = lower_first(claim, names_);
    std::vector<std::string> wrap, leads;
    switch (m) {
        case Manner::posh: wrap = {claim + " Quite so.", claim}; leads = {"I dare say " + l, "One hates to gossip, but " + l, "Frightfully sorry, but " + l}; break;
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

std::string Script::statement(const Statement& s) { return dress(s.speaker, core(s.f, s.speaker)); }

std::string Script::question(const Question& q) {
    // the player's own words: "Ask Cleo: is it true that Ada is lying?"
    plain_ = true;
    std::string c = strip_stop(core(q.f, -1));
    plain_ = false;
    if (q.f.kind == Kind::not_me || (q.f.kind == Kind::neighbour_did && q.f.a == q.to)) {
        // put about the one being asked
        if (q.f.kind == Kind::not_me) c = "you didn't do it";
        else c = "one of your neighbours did it";
    }
    return name(q.to) + ", is it true that " + lower_first(c, names_) + "?";
}

std::string Script::reply(int who, bool yes) {
    const Manner m = manners_.empty() ? Manner::sunny : manners_[static_cast<size_t>(who) % manners_.size()];
    switch (m) {
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

std::string Script::refuse(int who) {
    const Manner m = manners_.empty() ? Manner::sunny : manners_[static_cast<size_t>(who) % manners_.size()];
    switch (m) {
        case Manner::posh: return any({"I shan't say a thing unless asked properly.", "One does not volunteer information."});
        case Manner::salty: return any({"I ain't sayin' nothin'. Ask me straight.", "Arr. Ask, and maybe I'll tell."});
        case Manner::nervous: return any({"I'd... rather not say. Unless you ask?", "Oh, don't make me volunteer anything."});
        case Manner::gossip: return any({"Oh, I know things. Ask me nicely.", "My beak is sealed. Mostly."});
        case Manner::grump: return any({"...", "Not talking.", "Hmph."});
        case Manner::scholar: return any({"I respond only to well-formed questions.", "Pose a question and I shall answer it."});
        case Manner::drama: return any({"My lips are sealed! Unless asked!", "I cannot speak of it! Ask me!"});
        case Manner::sunny: return any({"Ask me something! Go on!", "I've got a secret! Ask!"});
    }
    return "...";
}

std::string Script::opener(int liars, int parrots) {
    const Crime& cr = crime(crime_);
    const std::string L = number_word(liars);
    const std::string are = liars == 1 ? "is" : "are";
    return any({cr.what + "! " + upper_first(number_word(parrots)) + " parrots at the table, and exactly " + L + " of them " + are + " lying. Who " + cr.did + "?",
                cr.what + ". Exactly " + L + " of these " + number_word(parrots) + " birds " + are + " liars. Find out who " + cr.did + ".",
                "Order, order! " + cr.what + ". Exactly " + L + " " + (liars == 1 ? "liar sits" : "liars sit") + " at this table. Who " + cr.did + "?"});
}

std::string Script::guilty(int who) {
    return dress(who, any({"Fine! It was me! I " + crime(crime_).did + "!", "Caught. I " + crime(crime_).did + ", and I'd do it again.", "Squawk! You got me!"}), false);
}
std::string Script::gloat(int who) {
    return dress(who, any({"Ha! It was me all along! I " + crime(crime_).did + "!", "Wrong bird! It was me, and I'm off!", "Hee hee. I " + crime(crime_).did + ". Bye!"}), false);
}
std::string Script::wrongly(int who) {
    return dress(who, any({"Me? I never!", "How dare you!", "Wrong bird, I'm afraid.", "I've been framed!"}), false);
}
std::string Script::cheer(int who) {
    return dress(who, any({"Justice!", "Well reasoned!", "Hooray!", "I knew it all along."}), false);
}

}  // namespace pt
