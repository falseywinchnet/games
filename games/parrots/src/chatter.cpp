// The chatter: what a bird says when it has nothing to say about the case. A library of
// lines by topic, and generators that put parts together (a cloud's shape, a fruit and a
// verdict on it, a small adventure), so the same line rarely comes round twice.
#include "script.hpp"

#include <algorithm>

namespace pt {

namespace {
using Lines = std::vector<std::string>;

// What a bird says of a liking, in the first person, as the reason it sits where it does.
const Lines& liking_lines(Liking l) {
    static const Lines high = {"I like to sit up high. You can see the whole dome from here.", "Up high is best. That's why I'm up here.",
                               "I always pick a high perch. The view! The air!", "Higher is better. I'd sit on the moon if they'd let me.",
                               "I like a high perch. Nobody steps on your tail up here."};
    static const Lines low = {"I keep near the floor. Heights make my knees wobble.", "Low and steady, that's me. That's why I'm down here.",
                              "I like a low perch. Closer to the snacks that fall.", "Down here is cosy. I've no head for heights.",
                              "I sit low. If I fall off, I don't fall far."};
    static const Lines wood = {"I only sit on wood. Proper branches, like the old days.", "Wood under my feet, that's how I like it. Hence this branch.",
                               "Give me wood every time. Rope wobbles and brass is cold.", "I'm a wood bird. I like a perch I can nibble."};
    static const Lines rope = {"I love a rope perch. The sway is ever so soothing.", "Rope for me. That's why I'm on this one.",
                               "Rope gives a little under the feet. Lovely.", "I like the ropes. Wood is so... stiff."};
    static const Lines brass = {"I like the brass. It shines, and so do I.", "Brass for me. Polished, cool, elegant. Like myself.",
                                "I sit on brass. One has standards.", "I'm on the brass because the brass suits me."};
    static const Lines iron = {"I like the ironwork. Solid, Victorian, dependable.", "Iron for me. Nothing wobbles on iron.",
                               "I sit on iron. Good old cast iron.", "The old ironwork is my favourite perch. Very proper."};
    static const Lines stone = {"I like stone under my feet. Cool on a warm day.", "Stone for me. It doesn't sway and it doesn't splinter.",
                                "I sit on stone. Very grounding.", "I like the stonework. It feels ancient."};
    static const Lines shade = {"I want the shade. Sun makes my feathers fade.", "I sit in the shade. That's why I'm under the leaves here.",
                                "Shade, please. I burn so easily.", "I prefer the shade. Cool and quiet and green."};
    static const Lines sun = {"I want the sun. That's why I'm out here in it.", "Sunshine on my feathers! I sit in the sun every day.",
                              "I like a sunny perch. Warms the wings.", "Sun for me. The shade is for mushrooms."};
    static const Lines company = {"I like company. I'd never sit alone.", "I always sit with someone. It's friendlier.",
                                  "I need a neighbour on my perch. Somebody to talk to.", "Company! That's why I'm sharing this perch."};
    static const Lines one = {"I like exactly one neighbour. Two's a crowd.", "One neighbour, that's my rule. Not none, not two.",
                              "I want one bird beside me. Just the one.", "Exactly one neighbour. I'm very particular."};
    static const Lines alone = {"I like a perch to myself. That's why I'm alone here.", "I sit alone. I prefer my own company.",
                                "No neighbours for me, thank you. Elbow room.", "A perch all to myself. Bliss."};
    static const Lines nored = {"I won't sit beside a red bird. They're so loud.", "No red birds next to me, thank you. Look, none.",
                                "I keep away from the red ones. Clashing feathers.", "Red birds? Not beside me. Never."};
    switch (l) {
    case Liking::high: return high;
    case Liking::low: return low;
    case Liking::wood: return wood;
    case Liking::rope: return rope;
    case Liking::brass: return brass;
    case Liking::iron: return iron;
    case Liking::stone: return stone;
    case Liking::shade: return shade;
    case Liking::sun: return sun;
    case Liking::company: return company;
    case Liking::one_neighbour: return one;
    case Liking::alone: return alone;
    case Liking::no_red_beside: return nored;
    }
    return high;
}

const Lines& kind_lines(int species) {
    static const Lines k[10] = {
        {"Scarlet macaws can live sixty years. I intend to live seventy.", "I'm a scarlet macaw. The red is natural, thank you.",
         "Macaws eat clay, you know. Settles the stomach.", "My tail is longer than my body. I'm very proud of it.",
         "In the wild we fly in pairs. I'm still looking.", "Red, yellow and blue. I'm basically a flag."},
        {"Blue-and-gold macaws are the most sensible macaws.", "Blue on top, gold underneath. Like a sunrise, turned over.",
         "I can crack a Brazil nut with one bite. Want to see?", "My bare face blushes when I'm excited. Don't look.",
         "We blue-and-golds mate for life. Very romantic.", "I'm the colour of the sea and the sand at once."},
        {"I'm a hyacinth macaw. The biggest parrot there is. You may bow.", "Hyacinth macaws are gentle giants. Mostly gentle.",
         "I'm all blue, top to tail. Except the yellow bits.", "My beak could open a coconut. I've done it twice.",
         "People call us blue angels. I'm told I'm more of a blue rascal.", "There aren't many of us left. So be nice to me."},
        {"Amazons are the best talkers. Listen to this: hello. Hello! HELLO!", "I'm an Amazon parrot. I'm named after a river. A big one.",
         "We Amazons love to sing. Opera, mostly.", "Green all over and proud of it.", "Amazons can live eighty years. I'm only forty. A youngster.",
         "When I'm excited my eyes go small. Watch. ...No, you missed it."},
        {"I'm a cockatoo. When I'm cross, the crest goes up. It's up now.", "Cockatoos can dance, you know. I've got moves.",
         "My crest is a mood ring. Yellow means I'm feeling fabulous.", "We cockatoos are very cuddly. And very loud.",
         "I can open any latch in this aviary. Don't tell the keeper.", "White as snow. Mostly. I rolled in something earlier."},
        {"African greys are the clever ones. I can count to six.", "I'm an African grey. Grey on the outside, brilliant on the inside.",
         "Greys can learn a thousand words. I know nine hundred and ninety-nine.", "My tail is red. It's my little secret.",
         "I can do the telephone, the kettle and the keeper's sneeze.", "Some say greys are worriers. I worry that's true."},
        {"Budgies come from Australia. It's very far and very hot.", "I'm a budgerigar. Small, but I contain multitudes.",
         "We budgies fly in flocks of thousands at home.", "I can see colours you can't. You're missing out.",
         "My cere's blue, so I'm a boy. Or is it the other way round?", "I'm small enough to sit in a teacup. I've tried."},
        {"I'm a lovebird. I'm all about love. And biting.", "Lovebirds pine without a partner. I pine a bit anyway.",
         "Lovebirds carry nesting stuff tucked in their feathers.", "Small, colourful and passionate. That's lovebirds.",
         "Peach face, green coat. Very fashionable.", "If you see two of us cuddling, that's normal. Don't stare."},
        {"I'm an eclectus. The boys are green and the girls are red. Guess.", "Eclectus parrots have feathers like fur. Feel. No, don't.",
         "People thought our boys and girls were different kinds of bird for a hundred years.", "I eat a lot of fruit. A lot.",
         "My beak is candy-corn coloured. Very smart.", "We eclectus are calm birds. Usually. Today, not so much."},
        {"I'm a galah. In Australia, calling someone a galah means they're silly. Rude.", "Pink and grey, the height of fashion.",
         "Galahs hang upside down in the rain. It's lovely.", "We galahs are terrible show-offs. Look at me!",
         "My crest is pale pink. Like a sunrise, or a slice of ham.", "Galahs mate for life and never stop chattering. Ask my wife."},
    };
    return k[((species % 10) + 10) % 10];
}

const Lines weather = {
    "Lovely afternoon out there, isn't it? Not that we can go out in it.", "The sun's been on the glass all day. It's like a bath in here.",
    "Feels like rain later. My knees always know.", "Bit muggy under the leaves today.", "The clouds are racing along up there. Windy, I expect.",
    "It's warm. It's always warm. I'd like a cold day, just once.", "When it rains, the glass sings. You should hear it.",
    "The keeper says there's frost outside. Not in here. Never in here.", "Hot today. Even the butterflies are sitting down.",
    "I can see a blue patch of sky right through the dome. Very nice.", "The light's lovely this afternoon. Golden.",
    "Do you get weather where you come from?", "It was foggy this morning. The whole dome went white.", "I like it when the sun comes through the leaves in stripes.",
    "Steamy in here. My feathers have gone curly.", "The wind's in the west. I can tell by the clouds.", "Rain on the glass is my favourite sound. After my own voice.",
    "The sun's moving round. I'll have to shuffle along soon.", "A bit close today. Thunder later, mark my words.", "It's a fine day for sitting still and doing nothing."};
const Lines cloud_things = {"a teapot", "a sheep", "a boot", "the keeper's hat", "a whale", "a banana", "a castle", "a sleeping cat", "a crocodile",
                            "a cauliflower", "an elephant", "a rabbit", "a ship", "a feather", "a pear", "a duck", "a dragon", "a grand piano",
                            "a dumpling", "a lighthouse", "my aunt", "a bowl of porridge", "a slipper", "a crown", "a snail", "a top hat",
                            "a parrot. Wait, no, a pigeon", "a sandwich", "a walrus", "a trombone"};
const Lines weekday_lines[7] = {
    {"Sunday. The keeper wears his good waistcoat on Sundays.", "It's Sunday. Visitors! Look sharp, everyone.", "Sunday again. Bells from the church across the park.",
     "Sundays are for preening. And napping. Mostly napping."},
    {"Monday. Nobody likes Mondays. Not even parrots.", "It's Monday. The keeper's always grumpy on a Monday.", "Monday: clean water day. Lovely.",
     "Monday already? Where did the weekend go?"},
    {"Tuesday. Nothing ever happens on a Tuesday.", "It's Tuesday. Grape day, if we're lucky.", "Tuesday, the dullest day. Let's make it interesting.",
     "On Tuesdays the gardener's boy comes to rake the paths."},
    {"Wednesday. Half way through the week. Hooray.", "It's Wednesday. The keeper polishes the brass on Wednesdays.",
     "Wednesday. Mango day. If nobody's eaten them.", "Wednesday! Or is it Thursday? Wednesday."},
    {"Thursday. Nearly Friday. Hold on.", "It's Thursday. The schoolchildren come on Thursdays. I do my best tricks.",
     "Thursday: the gardener waters the ferns. I hide.", "Thursday. A good day for a bath."},
    {"Friday! The best day. Don't ask me why.", "It's Friday. Fish for the keeper, fruit for us.", "Friday. The keeper hums on Fridays. Badly.",
     "Friday already. The week flew. Like me."},
    {"Saturday. Crowds! Everyone wants to see the parrots.", "It's Saturday. Band in the park. I sing along.", "Saturday: no lessons, no keeper, just us.",
     "Saturday. I'm on my best behaviour. Mostly."},
};
const Lines aviary = {
    "Have you seen the fountain? I bathe in it when nobody's looking.", "The orchids came out this week. Smell them. Go on.",
    "The butterflies think they own this place.", "Mind the watering can. The gardener leaves it everywhere.", "Those vines are new. They grow a foot a week.",
    "The bird bath is the best gossip spot in the whole dome.", "Someone's been dropping feathers on the path. Not mine. Probably not mine.",
    "Look at the glass up there. Every pane polished by hand.", "The old lamp only lights up at Christmas.", "I've counted the panes in the dome. Twice. Different answers.",
    "The palms are older than the keeper. Older than his father, even.", "You can hear the fountain from anywhere in here. It never stops.",
    "There's a gallery up round the wall. Nobody goes up there now.", "The bamboo grows so fast you can hear it creak at night.",
    "The tree ferns came on a ship from New Zealand. So I'm told.", "That swing's a bit wobbly. Don't tell the keeper, he'll take it away.",
    "The feeding table's best first thing in the morning.", "I like the path. It crunches nicely when the keeper walks on it.",
    "There's a frog living under the fountain. Nobody believes me.", "The hoop up there is the best seat in the house, if you can reach it.",
    "When the sun shines through, the whole dome goes green and gold.", "The keeper planted those bananas himself. He talks to them.",
    "Somebody's put a snail in the bird bath. Not me.", "This place was built in 1851, or 1861, or some year with a one in it.",
    "The leaves drip all afternoon. It's like rain that's indoors.", "There's a moth the size of a saucer that lives in the ferns.",
    "The lianas are perfect for swinging on. Not that we're allowed."};
const Lines fruits = {"mango", "papaya", "grapes", "the oranges", "pomegranate", "the bananas", "passion fruit", "the figs", "apple", "the melon",
                      "guava", "the cherries", "pineapple", "kiwi fruit", "the dates", "the plums"};
const Lines fruit_verdicts = {"is divine today", "is a bit sour", "is my absolute favourite", "is overrated, frankly", "is gone already",
                              "is better than yesterday's", "tastes of sunshine", "is far too sweet", "is the best thing in this aviary",
                              "was hidden under a leaf, and now it's mine", "should be served more often", "is fine. Just fine",
                              "is for special occasions only", "is the keeper's favourite too", "gets stuck in my beak", "has gone a bit brown",
                              "makes my feathers shine", "goes to my head", "is worth fighting over", "is a matter of taste"};
const Lines jokes = {
    "Why do parrots carry umbrellas? So they can be polly-unsaturated. Hee hee.", "What do you call a parrot that won't talk? Polly-tickled pink. ...No? Fair.",
    "What do you call a parrot in a raincoat? Polly-ethylene.", "Why did the parrot wear a raincoat? It wanted to be polly-proof.",
    "Knock knock. Who's there? Polly. Polly who? Polly want a cracker, obviously.", "What's a parrot's favourite game? Hide and speak.",
    "What do you get if you cross a parrot with a centipede? A walkie-talkie.", "Why did the parrot sit on the clock? To be on time.",
    "What's orange and sounds like a parrot? A carrot. Ha!", "Why don't parrots play poker? Too many cheetahs. Wait, that's a different joke.",
    "What do you call a bird that's afraid to fly? A chicken.", "How do birds stay fit? Plenty of worm-ups.",
    "What's a parrot's favourite subject? Polly-tics.", "Why do birds fly south? Too far to walk.",
    "What did the parrot say to the duck? Put it on my bill.", "Which bird is always out of breath? A puffin.",
    "Why did the owl invite friends over? It didn't want to be owl by itself.", "What kind of bird works on a building site? A crane!",
    "What do you call a very rude bird? A mockingbird.", "Why did the pelican get kicked out of the restaurant? Big bill.",
    "Which bird is the best boxer? None of them. Birds can't make fists.", "Knock knock. Who's there? Owl. Owl who? Owl tell you later.",
    "Where do parrots go to learn? Polly-technic.", "What's a parrot's favourite cake? Polly-filla. No, wait.",
    "Why are parrots so good at gossip? We repeat everything.", "What do you call a parrot that's lost its voice? A silent partner.",
    "I told a joke about a seagull once. Nobody laughed. Too cheap.", "How does a parrot apologise? It says sorry. Then says it again. Then again.",
    "What do you call a sleeping parrot? A polly-nap.", "What does a parrot do at a party? Polly-gets everyone dancing.",
    "Why did the toucan get the job? Two can do it.", "What's a bird's favourite film? Beak to the Future.",
    "Why was the bird nervous before the test? It hadn't been eggs-amined in years.", "What do you call a bird in winter? Brrrd.",
    "What do you give a sick bird? Tweetment.", "Why do hummingbirds hum? They forgot the words.",
    "What's a parrot's favourite instrument? The squawk-ordion.", "Why don't birds use phones? They prefer tweeting.",
    "I've got a joke about the fountain, but it's a bit drippy.", "Want to hear a joke about the dome? It'll go over your head.",
    "How do you know the keeper's been here? The bananas are in alphabetical order.", "I'd tell you a joke about palm trees, but you'd find it a bit frondly."};
const Lines musings = {
    "Do you ever wonder what the fish in the fountain think about?", "I've been thinking. About seeds, mostly.", "Sometimes I just sit and look at the leaves.",
    "If I had hands I'd write poems. Short ones.", "I had a dream I could swim. Then I woke up wet. Someone splashed me.",
    "Do you think the moon is a big seed? I do.", "I'm practising my whistle. Fweeeet. ...I'm still practising.",
    "I wonder if the keeper dreams about us.", "Feathers are the best thing. Imagine being bald.", "Sometimes I say a word over and over until it sounds strange. Spoon. Spoon. Spoon.",
    "I've decided to be more mysterious. Starting... now.", "What's the point of a Tuesday?", "I counted my feathers this morning. All present.",
    "I once said 'hello' to a mirror for an hour. Lovely chap.", "If you could be any bird, which would you be? I'd be me, but taller.",
    "I'm saving my best squawk for a special occasion.", "There's a spot on the glass that looks like a face. It's watching.",
    "Do butterflies remember being caterpillars? I'd be embarrassed.", "I'm not lazy. I'm conserving energy.", "Some days you're the bird, some days you're the birdseed.",
    "I like you. You've got a nice face. For someone without a beak.", "You ask a lot of questions. I like that in a person.",
    "Is it rude to sing with your mouth full? Asking for a friend.", "I'd like a little hat. Nothing fancy. A little hat.",
    "Nobody ever asks how the parrot's doing. I'm fine, thanks.", "I'm learning a new word every day. Today's is 'flabbergasted'.",
    "When I grow up I'd like to be a lighthouse.", "I can't remember if I've had breakfast. I'll have another to be sure.",
    "La la la. Tra la. La. That's all of it so far.", "Hello. Hello. Hello. Just checking it still works."};
const Lines story_who = {"I", "My cousin", "My grandmother", "The keeper's cat", "A pigeon I know", "My old friend from the zoo", "The gardener's boy", "My uncle Percy"};
const Lines story_did = {"once flew right round the dome without stopping", "once ate a whole pineapple in an afternoon", "once fell into the fountain",
                         "once learned to say 'good morning' in French", "once got stuck in a watering can", "once escaped for a whole day",
                         "once sang for a duchess", "once stole a teaspoon and never gave it back", "once hid in a hat for a week",
                         "once out-talked a whole schoolroom", "once slept through a thunderstorm", "once found a sixpence in the ferns",
                         "once chased a butterfly for an hour and lost", "once tried to sit on an egg that was a pebble", "once rang the bell for a joke",
                         "once got tangled in a liana and stayed there till tea"};
const Lines story_end = {"True story.", "Don't ask how.", "Nobody believes me.", "I'm still proud of it.", "Never again.", "Ask anyone.",
                         "It was a Tuesday, I think.", "Happiest day of my life.", "The keeper was furious.", "Best day ever.", "I'd do it again.",
                         "We don't talk about it."};
const Lines others_lines = {"{N} snores, you know. Like a kettle.", "{N} has the loveliest voice in the dome. Don't tell {N} I said so.",
                            "{N} owes me a grape.", "Have you met {N}? Charming. Loud, but charming.", "{N} and I go back years.",
                            "{N} thinks I don't know about the hidden walnut. I know.", "{N} taught me to whistle. Badly.",
                            "{N} preens for an hour every morning. An hour!", "I'm not speaking to {N} today. Long story.",
                            "{N} does a wonderful impression of the keeper.", "{N}'s feathers are looking very glossy today.",
                            "{N} once sat on my tail for a whole afternoon.", "{N} can't sing, but {N} won't stop.", "Don't let {N} talk you into a game of cards."};
const Lines asks = {"Do you like {T}?", "Have you ever seen {T}?", "What do you think of {T}?", "Is {T} nice where you live?", "Have you brought {T}? No? Pity."};
const Lines ask_things = {"mangoes", "the fountain", "thunderstorms", "the seaside", "brass bands", "snow", "the keeper's hat", "cherries", "dancing",
                          "the moon", "boats", "big bells", "orchids", "porridge", "kites", "the opera", "rainbows", "puddles", "trains", "fireworks",
                          "a good bath", "picnics", "windmills", "hot cross buns", "violins"};
const Lines songs = {"♪ Oh, the bananas are ripe in the dome, tra la ♪", "♪ Dum de dum, a perch to call my own ♪", "♪ Polly put the kettle on, we'll all have seeds ♪",
                     "♪ Daisy, Daisy, give me your answer, do ♪", "♪ Row, row, row your boat... ♪ That's all I know.", "♪ Tweet tweet, tweedle-ee-dee ♪",
                     "♪ Rain on the glass, rain on the glass, la la ♪", "♪ Oh, I do like to be beside the fountain ♪", "♪ Twinkle twinkle little star ♪ ...the rest escapes me.",
                     "♪ Hi-diddle-dee-dee, a parrot's life for me ♪", "♪ Feathers in the morning, feathers in the night ♪", "♪ Who's a pretty bird? I'm a pretty bird ♪"};
const Lines more_jokes = {
    "What do you call a parrot with no feathers? Cold.", "Why do seagulls fly over the sea? If they flew over the bay they'd be bagels.",
    "How does a bird with a broken wing land safely? With a sparrow-chute.", "What's a parrot's favourite drink? Polly-nesian punch.",
    "What happens when ducks fly upside down? They quack up.", "Why did the chicken join a band? It had the drumsticks.",
    "What do you call two birds in love? Tweethearts.", "Why don't owls fall in love in the rain? Too wet to woo.",
    "What do you get when you cross a parrot with a shark? A bird that talks your ear off.", "What do you call a parrot who's had too much tea? A polly-tannin. Sorry.",
    "Why is the sky so high? So birds don't bump their heads.", "What's black and white and red all over? A penguin that ate a mango.",
    "What bird can lift the most? A crane. I told that one already. Still good.", "Why did the bird go to the doctor? It needed tweetment. I've told that one too.",
    "How do you catch a unique bird? Unique up on it.", "How do you catch a tame bird? The tame way.", "What do you call a parrot in a suit? Polly-tician.",
    "Why do birds sing in the morning? They haven't read the news yet.", "What's a bird's favourite crisp? Cheep and onion.",
    "Why did the parrot cross the road? To prove it wasn't chicken."};
const Lines more_musings = {
    "If a bird sings in an empty dome, does it make a sound? Yes. I've checked.", "I've decided my favourite number is seven. Ask me why. Don't.",
    "When I close one eye, the fountain moves. Try it.", "I wish I had a pocket. Just a small one. For grapes.", "The leaves are greener on the other side of the dome.",
    "I'm going to have a little sit. I'm already sitting. I'm going to sit harder.", "Every morning I say good morning to the sun. It never answers.",
    "I'd like to see the sea. The keeper says it's a big bath.", "Do you think the palm trees talk to each other? Slowly?",
    "I've been practising looking dignified. How am I doing?", "I keep a list of everyone who's been nice to me. You're on it. Probably.",
    "I'm told I'm a terrible gossip. I can't imagine who said so.", "If I ever write a book it'll be called 'Feathers: My Story'.",
    "I like the way the light goes wobbly through the fountain.", "Sometimes I hum without noticing. Was I humming?",
    "I'm very good at waiting. I'm waiting now, look.", "Do you know any good secrets? I collect them.",
    "I miss the old keeper. He brought biscuits.", "My left foot is my favourite foot.", "I think I'd make a good duke. I've the nose for it."};
}  // namespace

std::string Script::about_self(int who) {
    const size_t w = static_cast<size_t>(who);
    const bool liar = w < liars_.size() && liars_[w];
    if (w >= birds_.size() || w >= scene_.seats.size()) return musing();
    const Seat& me = scene_.seats[w];
    // whether a liking holds where this bird sits
    auto holds = [&](Liking l) {
        int nb = 0;
        bool red = false;
        for (int j = 0; j < scene_.birds(); ++j)
            if (scene_.beside(who, j)) { ++nb; red = red || scene_.seats[static_cast<size_t>(j)].red; }
        switch (l) {
        case Liking::high: return me.high;
        case Liking::low: return me.low;
        case Liking::wood: return me.stuff == Stuff::wood;
        case Liking::rope: return me.stuff == Stuff::rope;
        case Liking::brass: return me.stuff == Stuff::brass;
        case Liking::iron: return me.stuff == Stuff::iron;
        case Liking::stone: return me.stuff == Stuff::stone;
        case Liking::shade: return me.shade;
        case Liking::sun: return !me.shade;
        case Liking::company: return nb >= 1;
        case Liking::one_neighbour: return nb == 1;
        case Liking::alone: return nb == 0;
        case Liking::no_red_beside: return !red;
        }
        return true;
    };
    std::vector<Liking> say;
    if (liar) {
        // a liar's likings, as told: ones its seat plainly fails
        for (int l = 0; l <= static_cast<int>(Liking::no_red_beside); ++l)
            if (!holds(static_cast<Liking>(l))) say.push_back(static_cast<Liking>(l));
    } else {
        for (Liking l : birds_[w].likes)
            if (holds(l)) say.push_back(l);
    }
    if (say.empty()) return about_kind(who);
    return any(liking_lines(say[static_cast<size_t>(pick(static_cast<int>(say.size())))]));
}

std::string Script::about_kind(int who) {
    const size_t w = static_cast<size_t>(who);
    return any(kind_lines(w < species_.size() ? species_[w] : 0));
}

std::string Script::about_weather() {
    if (pick(3) == 0) {
        const std::string thing = any(cloud_things);
        return any({"That cloud up there looks like " + thing + ".", "Look, through the glass! A cloud like " + thing + ".",
                    "I saw a cloud this morning shaped exactly like " + thing + ".", "If you squint, that cloud is " + thing + "."});
    }
    return any(weather);
}

std::string Script::about_day(int weekday) {
    if (weekday < 0 || weekday > 6) return musing();
    return any(weekday_lines[weekday]);
}

std::string Script::about_aviary() { return any(aviary); }

std::string Script::about_food() {
    std::string f = any(fruits);
    const bool plural = f.back() == 's';
    std::string verdict = any(fruit_verdicts);
    if (plural) {
        // "the oranges is" won't do
        if (verdict.rfind("is ", 0) == 0) verdict = "are " + verdict.substr(3);
        else if (verdict.rfind("was ", 0) == 0) verdict = "were " + verdict.substr(4);
        else if (verdict.rfind("has ", 0) == 0) verdict = "have " + verdict.substr(4);
        else if (verdict.rfind("gets ", 0) == 0) verdict = "get " + verdict.substr(5);
        else if (verdict.rfind("makes ", 0) == 0) verdict = "make " + verdict.substr(6);
        else if (verdict.rfind("goes ", 0) == 0) verdict = "go " + verdict.substr(5);
        else if (verdict.rfind("tastes ", 0) == 0) verdict = "taste " + verdict.substr(7);
        else if (verdict.rfind("should ", 0) == 0) verdict = "should " + verdict.substr(7);
    }
    std::string s = f + " " + verdict + ".";
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return any({s, "If you ask me, " + f + " " + verdict + ".", "Between you and me, " + f + " " + verdict + "."});
}

std::string Script::joke() { return pick(3) == 0 ? any(more_jokes) : any(jokes); }
std::string Script::musing() { return pick(3) == 0 ? any(more_musings) : any(musings); }

std::string Script::about_others(int who) {
    const int n = static_cast<int>(names_.size());
    if (n < 2) return musing();
    int other = pick(n - 1);
    if (other >= who) ++other;
    std::string s = any(others_lines);
    for (size_t at = s.find("{N}"); at != std::string::npos; at = s.find("{N}")) s.replace(at, 3, names_[static_cast<size_t>(other)]);
    return s;
}

std::string Script::ask_player() {
    std::string s = any(asks);
    const size_t at = s.find("{T}");
    if (at != std::string::npos) s.replace(at, 3, any(ask_things));
    return s;
}

std::string Script::song() { return any(songs); }
std::string Script::story() { return any(story_who) + " " + any(story_did) + ". " + any(story_end); }

}  // namespace pt
