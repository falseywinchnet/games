#include "card_remarks.hpp"

#include <algorithm>
#include <cctype>
#include <span>

namespace games {
namespace {

// The grammar. A line is drawn from `line`; every {name} in it is replaced by one of the
// entries of the slot of that name, itself expanded the same way.
constexpr std::string_view kLine[] = {
    "{look} the {stock}.",
    "{look} the {stock}. {verdict}",
    "That's {tint}. {verdict}",
    "The lettering is {lettering}.",
    "{lettering_line}",
    "{watermark}",
    "{owner} card {comparison}.",
    "{rival} {reaction}.",
    "Let's see {rival} {boast}.",
    "{feel} the {weight}.",
    "{thickness}",
    "Rainstar Software. {tagline}",
    "{aside}",
    "{look} the {detail}. {verdict}",
};
constexpr std::string_view kLook[] = {"observe", "behold", "regard", "consider", "note", "look at",
                                      "admire", "study", "appreciate", "take in", "savor"};
constexpr std::string_view kStock[] = {"{tint} stock",        "{quality} {tint} stock", "{quality} {tint} {paper}",
                                       "{tint} {paper}",      "{quality} {tint} coloring", "{tint} {paper}, so {quality}"};
constexpr std::string_view kTint[] = {"eggshell", "bone",        "ivory",       "alabaster",     "oyster",   "pale nimbus",
                                      "ecru",     "chalk",       "porcelain",   "linen white",   "oatmeal",  "cream",
                                      "off-white", "antique white", "vellum",   "dove gray",     "swan white", "birch"};
constexpr std::string_view kQuality[] = {"restrained", "subtle",      "quietly assertive", "understated", "unapologetic",
                                         "tasteful",   "discreet",    "immaculate",        "sober",       "impeccable",
                                         "serene",     "faintly warm", "deliberate",       "unhurried",   "almost arrogant",
                                         "confident",  "judicious",   "self-possessed",    "composed"};
constexpr std::string_view kPaper[] = {"stock", "cardstock", "cotton", "cover", "board", "laid", "wove", "rag"};
constexpr std::string_view kVerdict[] = {"Impressive.",
                                         "Very impressive.",
                                         "Exquisite.",
                                         "Hm.",
                                         "Tasteful.",
                                         "Not bad. Not bad at all.",
                                         "Someone get {rival} some water.",
                                         "I need a moment.",
                                         "{adverb} {adjective}.",
                                         "Unbelievable.",
                                         "Nobody say anything.",
                                         "This changes things.",
                                         "I think it's {tint}."};
constexpr std::string_view kAdverb[] = {"remarkably", "quietly",   "almost unbearably", "genuinely",
                                        "disarmingly", "frankly",  "impossibly",        "deeply"};
constexpr std::string_view kAdjective[] = {"tasteful", "correct",   "elegant",   "confident",
                                           "restrained", "considered", "civilized", "serious"};
constexpr std::string_view kLettering[] = {"{raise} {typeface}", "{typeface}, {raise}", "{raise}, in {typeface}"};
constexpr std::string_view kRaise[] = {"raised",          "embossed",         "thermographed", "engraved",
                                       "letterpressed",   "faintly raised",   "deeply impressed", "hand-set"};
constexpr std::string_view kTypeface[] = {"Silian Rail",       "Romalian type",          "Libre Baskerville",
                                          "spaced capitals",   "a modest roman",         "a quiet old-style serif",
                                          "small capitals",    "a face nobody here can name"};
constexpr std::string_view kLetteringLine[] = {"{raise} lettering. {verdict}", "Run a thumb over the {raise} letters.",
                                               "The {letters} on those letters. {verdict}", "That {letters}. {verdict}"};
constexpr std::string_view kLetters[] = {"kerning", "spacing", "serifs", "tracking", "weight"};
constexpr std::string_view kWatermark[] = {"Is that a watermark?", "There's a watermark. Of course there's a watermark.",
                                           "Hold it to the light. Go on.", "It even has {extra}.",
                                           "Hold it up to the lamp and watch it {glint}."};
constexpr std::string_view kExtra[] = {"a watermark", "a texture you can feel", "a little heft to it",
                                       "a glint when it turns", "corners you could cut yourself on"};
constexpr std::string_view kGlint[] = {"glint", "catch the light", "sparkle", "shimmer"};
constexpr std::string_view kRival[] = {"Van Patten", "Price",    "McDermott",  "Montgomery",          "Paul Allen", "Bryce",
                                       "Carruthers", "Timothy",  "Luis",       "everyone at the table", "the maître d'"};
constexpr std::string_view kOwner[] = {"Van Patten's", "Price's",    "McDermott's", "Montgomery's", "Paul Allen's",
                                       "Bryce's",      "Carruthers's", "Timothy's",  "Luis's"};
constexpr std::string_view kComparison[] = {"has nothing on this",        "looks photocopied next to this",
                                            "suddenly feels like a napkin", "was clearly printed in a hurry",
                                            "can't compete",              "is on {lesser}"};
constexpr std::string_view kLesser[] = {"copier paper", "something gray", "a coaster", "a much thinner stock",
                                        "eggshell, and not even a good eggshell"};
constexpr std::string_view kReaction[] = {"has gone very quiet",         "is sweating",
                                          "needs to sit down",           "has left the table",
                                          "is pretending not to look",   "is staring at the email address",
                                          "put a card away, unseen",     "dropped a fork",
                                          "wants to know who did the printing", "asked about the stock, twice"};
constexpr std::string_view kBoast[] = {"get a reservation at Dorsia", "beat that",          "top that kerning",
                                       "match this stock",            "explain that weight", "find a better {tint}",
                                       "do better on {paper}"};
constexpr std::string_view kFeel[] = {"feel", "notice", "weigh", "sense", "appreciate"};
constexpr std::string_view kWeight[] = {"heft", "weight of it", "{grams} {paper}", "cotton content", "stiffness",
                                        "way it holds its edge"};
constexpr std::string_view kGrams[] = {"sixty-pound", "eighty-pound", "hundred-and-ten-pound", "three-hundred-gram",
                                       "triple-thick", "duplexed", "two-ply"};
constexpr std::string_view kThickness[] = {"The thickness of it. {verdict}", "Turn it edge-on. It almost disappears.",
                                           "Look how thin. How {adjective}.", "It barely bends. {verdict}",
                                           "The edge alone is {adjective}."};
constexpr std::string_view kTagline[] = {"Games, mostly.",
                                         "Pay me not to work.",
                                         "Vice President, {game}.",
                                         "Mergers and amusements.",
                                         "Established this afternoon.",
                                         "We make games. {adverb} {adjective} ones.",
                                         "The {game} division.",
                                         "Head office: somewhere tasteful."};
constexpr std::string_view kGame[] = {"Solitaire", "Spider", "FreeCell",    "Hearts",      "Koi-Koi",   "Gems",
                                      "Sudoku",    "Untangle", "Nature Cube", "Liar's Dice", "Four Pegs", "Switchbox"};
constexpr std::string_view kAside[] = {"I have to return some {returns}.",
                                       "Gmail. A bold choice.",
                                       "The email address alone. {verdict}",
                                       "Square corners. {verdict}",
                                       "No phone number. {adverb} {adjective}.",
                                       "Nobody prints cards like this anymore.",
                                       "Careful. Fingerprints.",
                                       "Don't let {rival} see it.",
                                       "Is it {tint} or {tint}?",
                                       "Let me see that again.",
                                       "Where did you get it printed?"};
constexpr std::string_view kReturns[] = {"videotapes", "library books", "cassettes", "shopping carts",
                                         "Koi-Koi decks", "rented tuxedos"};
constexpr std::string_view kDetail[] = {"corners", "margins", "edge", "spacing", "way the light moves on it",
                                        "weight of the ink", "spacing between the letters", "cut of the edge"};

struct Slot {
    std::string_view name;
    std::span<const std::string_view> choices;
};
constexpr Slot kSlots[] = {
    {"line", kLine},         {"look", kLook},           {"stock", kStock},       {"tint", kTint},
    {"quality", kQuality},   {"paper", kPaper},         {"verdict", kVerdict},   {"adverb", kAdverb},
    {"adjective", kAdjective}, {"lettering", kLettering}, {"raise", kRaise},     {"typeface", kTypeface},
    {"lettering_line", kLetteringLine}, {"letters", kLetters}, {"watermark", kWatermark}, {"extra", kExtra},
    {"glint", kGlint},       {"rival", kRival},         {"owner", kOwner},         {"comparison", kComparison}, {"lesser", kLesser},
    {"reaction", kReaction}, {"boast", kBoast},         {"feel", kFeel},         {"weight", kWeight},
    {"grams", kGrams},       {"thickness", kThickness}, {"tagline", kTagline},   {"game", kGame},
    {"aside", kAside},       {"returns", kReturns},     {"detail", kDetail},
};
constexpr int kDepth = 6;            // slots within slots, at most
constexpr std::size_t kRemembered = 16;  // remarks not to be said again soon

// Capitals at the start and after each sentence's end.
void begin_sentences(std::string& text) {
    bool start = true;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (start && std::isalpha(c)) {
            text[i] = static_cast<char>(std::toupper(c));
            start = false;
        } else if (c == '.' || c == '?' || c == '!') {
            start = i + 1 < text.size() && text[i + 1] == ' ';
        } else if (c != ' ') {
            start = false;
        }
    }
}

}  // namespace

CardRemarks::CardRemarks(std::uint32_t seed) : state_(seed == 0 ? 0x9e3779b9U : seed) {}

std::uint32_t CardRemarks::random() {
    // xorshift32
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_;
}

std::string CardRemarks::expand(std::string_view text, int depth) {
    std::string out;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t open = text.find('{', at);
        if (open == std::string_view::npos) {
            out.append(text.substr(at));
            break;
        }
        out.append(text.substr(at, open - at));
        const std::size_t close = text.find('}', open);
        if (close == std::string_view::npos) {
            out.append(text.substr(open));
            break;
        }
        const std::string_view name = text.substr(open + 1, close - open - 1);
        at = close + 1;
        if (depth >= kDepth)
            continue;
        for (const Slot& slot : kSlots) {
            if (slot.name != name || slot.choices.empty())
                continue;
            const std::size_t pick = random() % slot.choices.size();
            out.append(expand(slot.choices[pick], depth + 1));
            break;
        }
    }
    return out;
}

std::string CardRemarks::next() {
    std::string remark;
    for (int attempt = 0; attempt < 12; ++attempt) {
        remark = expand("{line}", 0);
        begin_sentences(remark);
        if (std::find(recent_.begin(), recent_.end(), remark) == recent_.end())
            break;
    }
    recent_.push_back(remark);
    if (recent_.size() > kRemembered)
        recent_.pop_front();
    return remark;
}

}  // namespace games
