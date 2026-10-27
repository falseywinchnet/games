#include "lines.hpp"

#include <algorithm>

namespace ct {

namespace {
const char* const kTaunt[] = {
    "Too slow!", "Nice hat, bear!", "Finders keepers!", "Can't catch me!", "Yoink!", "Nyah nyah!", "Over here!", "Is that all?",
    "Missed me!", "Lovely carrots!", "Smell ya later!", "Ooh, a pumpkin. Scary.", "Mmm, crunchy!", "We're only borrowing them!",
    "Carrots taste better stolen.", "Peekaboo!", "Yoo-hoo, bear!", "Psst! Over here! No, here!", "This garden is delicious.",
    "Who wants a radish? Me!", "Bandits never sleep! Well, sometimes.", "One for me, one for me...", "Ta-da!",
    "Got your nose! Oh, it's a turnip.", "Five stars for this garden.", "We'll leave you the weeds.", "Hello, neighbour!",
    "Nice pumpkins. Heavy, aren't they?", "Catch us if you can!", "Snack o'clock!", "Your peas are in fine form.",
    "Compliments to the gardener!", "Is it lunch yet? It's always lunch.", "We dug these burrows ourselves.", "Burrow, sweet burrow.",
    "La la la, not stealing anything.", "That carrot winked at me.", "Wheee!", "Hop, hop, hooray!", "A carrot a day!",
    "Shh! I'm being sneaky.", "Fancy a turnip? No? More for me.", "What's the hurry, bear?", "Bears can't dance like this!",
    "Watch this move!", "Is that pumpkin for me? How kind!", "We left a thank-you note. Somewhere.", "Ooh, this one's a big carrot.",
    "Top of the morning!", "Ahoy, bear!", "Peekaboo, I see you!", "Crunch, crunch, crunch!", "Today's special: your lettuce.",
    "Best garden in the valley!", "I call the biggest carrot!", "Can't stop, munching.", "Bandit business. Nothing to see.",
    "A turnip for a pumpkin? Deal?", "Hello again!", "Mind your back with those pumpkins!", "We're very polite thieves.",
    "Wipe your paws before you push!", "Bear coming! Run! Walk! Stay!", "Nobody tell the bear about the beans.",
    "I've been practising my wave. Hello!", "Sorry, the carrots said yes.", "You grow 'em, we show 'em!", "Bow to the carrot king!",
    "Ta-ra!", "Nibble, nibble.", "Look, no paws! Well, some paws.", "Guess where the beans went.", "Pass the salt!",
    "A turnip for your thoughts?", "Fancy a dance, bear?", "Ha-HA!", "Every carrot counts!", "We see food, we eat it.",
    "I'm not here. This is a dream.", "Bandits, assemble!", "Down here! Up here! Everywhere!", "What a lovely day for mischief.",
    "Did someone say radishes?", "Is this your carrot? It's mine now.", "Achoo! Pardon me.", "Stripes are in this season.",
    "Don't look in the next burrow.", "Plot twist: it's our garden now.", "Mmm, cabbage. Best in show.", "Tag! You're it!",
};
const char* const kSpring[] = {
    "Spring! Everything's so crunchy!", "Fresh peas, fresh breeze!", "Achoo! Blossom up my nose.", "The radishes are up! So am I!",
    "Petals in my fur again.", "Spring cleaning! Starting with your carrots.", "Baby lettuces! Adorable. Delicious.",
    "April showers bring May snackers.", "Smell that blossom!", "New season, new snacks!", "The worms say good morning!",
    "Puddles! Splish splosh!",
};
const char* const kSummer[] = {
    "Summer! Strawberry season!", "Phew, hot day for pumpkins!", "Who brought the lemonade?", "Sunshine and stolen beans!",
    "Butterflies! Shh, not the cabbages.", "Too hot to run. Too tasty to stop.", "Sun hat? Pumpkin hat!",
    "Corn on the cob! Corn on the rob!", "Long days, long lunches!", "Picnic time! Your picnic.", "Lovely day for a nap. Not now.",
    "The tomatoes are blushing!",
};
const char* const kAutumn[] = {
    "Pumpkin season? Uh oh.", "Crunchy leaves, crunchy carrots!", "Harvest time! Our favourite!", "Ooh, conkers!",
    "The leaves are falling. So are your apples.", "Autumn: the season of snacks.", "Leaf pile! Wheee!", "Scarves on, bandits!",
    "Squash, marrows, more squash!", "Every leaf's a hiding place.", "Blackberries! Mine!", "Is that a gourd or a hat?",
};
const char* const kWinter[] = {
    "Brr! Got any soup?", "Snow in my burrow again.", "Frosty carrots are the crunchiest.", "Mittens on, bandits!",
    "Is that a snow-bear?", "Cold paws, warm heart!", "My tail's a scarf now.", "Winter parsnips! Sweet as anything.",
    "Look, I can see my breath!", "Cocoa in the burrow, anyone?", "Snowball fight! Later.", "Icicles! Crunchy!",
};
const char* const kNight[] = {
    "Who's there? Oh, it's you.", "Midnight snack time!", "Shh! The moon's watching.", "Fireflies! Lend us a light.",
    "We see better in the dark, you know.", "Bandit hours are the best hours.", "Lovely lanterns, bear.", "Ooh, spooky. Not really.",
    "Bedtime? Not for bandits!", "Twinkle, twinkle, little carrot.", "Hoot hoot! That was me.", "Starry night, snacky night!",
};
const char* const kGreet[] = {
    "New garden, new carrots!", "Ooh, a new garden!", "Welcome to our garden! Er, yours.", "Hello, bear!",
    "What's on the menu today?", "Look, pumpkins!", "Fresh soil! Lovely.", "Here comes the bear!", "Places, bandits!",
    "Bandits, to your burrows!", "Nice place you've got here.", "This garden looks tasty.", "Room for one more carrot?",
    "Everybody, look busy!",
};
const char* const kTrapped[] = {
    "Mmmph!", "Hey! It's dark!", "Let me ouuut!", "Who turned off the sun?", "This pumpkin smells.", "Mmf mmf!", "Rude!",
    "Mmmf! Hrmph!", "It's very orange in here.", "Is it night already?", "Hello? Anyone?", "I'm fine! Totally fine!",
    "Mmm... pumpkin. Not bad, actually.", "Knock knock! Let me out!", "Echo! Echo!", "Cosy. Too cosy.",
    "Pass a carrot down, would you?", "My tail's squashed!", "I demand a carrot!", "Who put a roof on my house?",
    "Mmph mmph mmmph!", "Still here! Still grumpy!", "Is that you, bear? Hmmph.", "Pumpkin pie? Pumpkin prison!",
    "I'll be good! Probably!", "It smells like soup in here.", "Wiggle, wiggle... nope.", "Hrrrnngh! Heavy!",
    "Fine. I'll just have a nap.", "Can somebody open a window?",
};
const char* const kFreed[] = {
    "Freedom!", "Ha! Thanks, bear!", "Fresh air!", "Woo hoo!", "Back in business!", "I'm out! I'm out!",
    "Sunshine! I missed you!", "Hello again, sky!", "That was the longest nap ever.", "Yippee!", "Free as a bird!",
    "Ooh, my tail!", "Out I pop!", "I knew you'd come round, bear.", "Stretch! Ahh.", "Never doubted it!", "The bandit returns!",
    "Air! Lovely air!", "Ta, bear!", "Ready for round two!",
};
const char* const kDuck[] = {
    "Eep!", "Yikes!", "Nope!", "Duck!", "Bye!", "Gotta go!", "Run away!", "Hide!", "Not today!", "Whoops!", "Too close!",
    "Back in a bit!", "Bear alert!", "Zoom!", "Excuse me!", "Down I go!",
};
const char* const kClose[] = {
    "Close! Too close!", "Ooh, that was near!", "Not my burrow, please!", "Wait, wait, wait!", "Stay back, pumpkin!",
    "That pumpkin's looking at me.", "Good pumpkin. Stay.", "Hey! Personal space!", "Uh oh...", "Gulp.",
    "I don't like where this is going.", "Shoo, pumpkin!",
};
const char* const kLastOne[] = {
    "Uh oh. It's just me now.", "Guys? Guys?!", "Er... where did everyone go?", "Just me and the carrots.",
    "I'm not nervous. You're nervous.", "Last bandit standing!", "Don't you dare, bear!", "Lonely out here...",
    "I'll guard the carrots myself!", "All by myself...", "Hold on, let's talk!", "You wouldn't. Would you?",
    "Every bandit for themselves!", "I'm the brave one. Gulp.",
};
const char* const kLaugh[] = {
    "Ha ha ha!", "Stuck! Stuck!", "Now you've done it!", "That's not moving.", "Hee hee hee!", "Oh no, bear!",
    "That pumpkin's having a nap!", "Stuck like jam!", "Hooray for us!", "Bonk! Stuck!", "That one's going nowhere.",
    "Pumpkin parked!", "Oopsie-daisy!", "Snug as a bug!", "Good spot for a pumpkin. Forever!", "Ho ho! Oh dear!",
};
const char* const kLaughAgain[] = {
    "Again? Encore!", "Pumpkin party!", "Ha! Do it again!", "We're having a lovely time!", "Stuck again! Hooray!",
    "Best day ever!", "Bravo! Bravo!", "The stuck club meets here!", "Party in the garden!", "Again! Again!",
    "We love this bit!", "Same again, please!",
};
const char* const kLaughSoft[] = {
    "Psst. Z takes it back.", "Even we feel bad now.", "There's a Hint in the bar, you know.", "Try Undo, bear. We'll wait.",
    "Take your time. Have a carrot.", "Don't worry. Pumpkins are tricky.", "Shh, everyone. He's thinking.", "Deep breaths, bear.",
    "It happens to the best bears.", "There, there. Undo's your friend.", "We won't tell anyone.", "Backspace works too, you know.",
};
const char* const kUnstuck[] = {
    "Aww! He fixed it!", "Hey, that was stuck!", "No fair! It was stuck!", "Unstuck? How?", "Rats, he undid it.",
    "Phew. I mean, curses!", "Back from the brink!", "Spoilsport!", "The pumpkin lives!", "Well, I never!",
};
const char* const kUndo[] = {
    "Hey! No rewinding!", "Ooh, time travel!", "Did that just happen? Or not?", "Backwards bear!", "Rewind! Wheee!",
    "Changed your mind?", "Undo, undo, undo!", "Sneaky!", "Was that a moonwalk?", "Second thoughts, bear?", "Ooh, magic.",
    "That was there a minute ago!", "Oi! Put it back! Er, forward!", "Clever bear!",
};
const char* const kRestart[] = {
    "Starting over? We'll wait.", "From the top!", "Round two!", "Fresh start! Fresh carrots!", "Back to the beginning!",
    "Take two!", "Again from scratch!", "Everybody back to your burrows!", "Places, everyone!", "Here we go again!",
    "Ooh, a do-over!", "Reset the carrots!", "Same garden, new plan?", "And... action!", "Back where we started!", "Encore performance!",
};
const char* const kHint[] = {
    "No peeking at the plan!", "Ooh, secrets.", "Hey, no hints!", "Who's he whispering to?", "Psst, what did it say?",
    "Not fair, the bear has a map!", "Don't listen to it, bear!", "Uh oh, he's got a plan.", "A plan! We hate plans.",
    "Ooh, a clever bear.", "Who told him?", "Shh! Don't tell him!", "Hmm, a hint. Very sneaky.", "Somebody's been reading the plans!",
    "Oh no, he's been tipped off.", "Hey! That's our secret!",
};
const char* const kIdle[] = {
    "Is he asleep?", "Take your time, bear. We've got snacks.", "Hello? Still there?", "Zzz... oh, are we still playing?",
    "Shall we put the kettle on?", "I could get used to this.", "Thinking hard, bear?", "Nice day for standing about.",
    "Psst. Your move.", "Anyone for I-spy?", "I spy something orange.", "Staring contest? I'm winning.", "Hmm-hmm-hmm, la la la...",
    "Tick tock, goes the clock.", "I'll just have a little snack.", "Bear's gone all quiet.", "Is it lunch yet?",
    "Maybe he's a statue now.", "We'll wait. We're very patient.", "Wake me when it's carrot time.",
};
const char* const kGiveUp[] = {
    "We give up!", "Curses!", "Fair cop.", "You win, bear.", "Can we keep one carrot?", "Not the pumpkins!", "All right, all right!",
    "We surrender!", "White flags all round!", "Well played, bear.", "Foiled again!", "Outsmarted by a bear!",
    "Tell the carrots we said goodbye.", "We'll be good! Till tomorrow.", "Next garden, you'll see!", "Fine! Keep your turnips!",
    "Pumpkins win this time.", "Truce! Truce!", "Hats off to the bear!", "Oh, crumbs.", "That was very well pushed.",
    "We'll go quietly. Mostly.", "Good game, bear!", "Rats! Er, raccoons!",
};
const char* const kGiveUpPerfect[] = {
    "Not one wasted push! Wow.", "Perfect! Even we're impressed.", "The fewest pushes? How?!", "Gold pumpkin! Fair and square.",
    "A perfect garden! Bravo, bear.", "That was a masterclass!", "No push wasted! Incredible!",
    "We'll tell our grandchildren about this.", "Flawless! Curses!", "Perfect pushing, bear!",
};
const char* const kBearCatch[] = {
    "Gotcha!", "In you go!", "Got one!", "Stay put!", "Ha!", "That's one!", "Caught you!", "Sorry, little one!",
    "Pumpkin hat for you!", "Snug as a bug.", "Mind your head!", "Lid on!", "Nice and cosy.", "Hup!", "There we are.", "Nighty-night!",
    "One down!", "Pop goes the pumpkin!", "Sit tight!", "Hello down there!", "And stay!", "Neat fit!", "Just right!", "Off you go to bed!",
};
const char* const kBearStuck[] = {
    "Oh dear.", "Oops...", "Hmm. That's stuck.", "Oh, bother.", "Well, that won't move.", "Oh. Oh no.", "Whoops-a-daisy.",
    "Hmm, wrong way.",
};
const char* const kBearStuckAgain[] = {
    "Not again...", "Oh, bother and bother.", "Hmm. Again.", "Dear oh dear.", "That's two.", "Wrong way again.", "Oh, my paws.",
    "Hmmm.",
};
const char* const kBearStuckCalm[] = {
    "Right. Deep breath.", "Undo, I think.", "Let's take that back.", "Steady now.", "A step back, then.", "Back we go.",
    "Hmm. Think, bear.", "Easy does it.",
};
const char* const kBearBump[] = {
    "Oof.", "Hedge.", "Not that way.", "Hmm, no.", "Won't budge.", "Ow.", "Blocked.", "Nope.", "Prickly!", "Mind the hedge, bear.",
    "Excuse me, hedge.", "Bonk.", "That's a hedge, that is.", "Other way, then.", "Hmph.", "Not through there.",
};
const char* const kBearIdle[] = {
    "Hmm...", "Let me see now.", "Which one first...", "Thinking, thinking.", "Hmm. Tricky.", "Now then.", "Where was I?",
    "One step at a time.", "Let's have a look.", "Steady, bear.",
};
const char* const kBearWin[] = {
    "All caught!", "Hooray!", "That's my garden!", "Tidy!", "Every burrow covered!", "Lovely job!", "Wa-hey!", "Safe and sound!",
    "Woo-hoo!", "Pumpkins one, bandits nil!", "Garden saved!", "Ta-da! All done.",
};
const char* const kBearPerfect[] = {"Not a push wasted!", "Perfect!", "Neat as a pin!", "Just so!", "Gold pumpkin!", "Spot on!"};

struct Kind { const char* const* lines; int n; };
template <size_t N> constexpr Kind kind(const char* const (&lines)[N]) { return {lines, static_cast<int>(N)}; }
const Kind kKinds[] = {
    kind(kTaunt), kind(kSpring), kind(kSummer), kind(kAutumn), kind(kWinter), kind(kNight), kind(kGreet), kind(kTrapped),
    kind(kFreed), kind(kDuck), kind(kClose), kind(kLastOne), kind(kLaugh), kind(kLaughAgain), kind(kLaughSoft), kind(kUnstuck),
    kind(kUndo), kind(kRestart), kind(kHint), kind(kIdle), kind(kGiveUp), kind(kGiveUpPerfect), kind(kBearCatch), kind(kBearStuck),
    kind(kBearStuckAgain), kind(kBearStuckCalm), kind(kBearBump), kind(kBearIdle), kind(kBearWin), kind(kBearPerfect),
};
static_assert(sizeof kKinds / sizeof kKinds[0] == static_cast<size_t>(Line::kinds), "a bag of lines for every kind");
}  // namespace

Lines::Lines(std::uint64_t seed)
    : rng_(seed ? seed : 91), bags_(static_cast<size_t>(Line::kinds)), last_(static_cast<size_t>(Line::kinds), -1) {}

int Lines::rand_int(int n) {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 7;
    rng_ ^= rng_ << 17;
    return static_cast<int>((rng_ >> 11) % static_cast<std::uint64_t>(n));
}

int Lines::size(Line kind) {
    const int k = static_cast<int>(kind);
    return k >= 0 && k < static_cast<int>(Line::kinds) ? kKinds[k].n : 0;
}

const char* Lines::line(Line kind, int index) {
    const int k = static_cast<int>(kind);
    if (k < 0 || k >= static_cast<int>(Line::kinds) || index < 0 || index >= kKinds[k].n) return "";
    return kKinds[k].lines[index];
}

const char* Lines::pick(Line kind) {
    const int k = static_cast<int>(kind);
    if (k < 0 || k >= static_cast<int>(Line::kinds)) return "";
    std::vector<int>& bag = bags_[static_cast<size_t>(k)];
    const int n = kKinds[k].n;
    if (bag.empty()) {
        // a fresh shuffle, never starting with the line that ended the last one
        for (int i = 0; i < n; ++i) bag.push_back(i);
        for (int i = n - 1; i > 0; --i) std::swap(bag[static_cast<size_t>(i)], bag[static_cast<size_t>(rand_int(i + 1))]);
        if (n > 1 && bag.back() == last_[static_cast<size_t>(k)]) std::swap(bag.front(), bag.back());
    }
    const int chosen = bag.back();
    bag.pop_back();
    last_[static_cast<size_t>(k)] = chosen;
    return kKinds[k].lines[chosen];
}

}  // namespace ct
