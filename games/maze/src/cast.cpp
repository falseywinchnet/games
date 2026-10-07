#include "cast.hpp"

#include "platform/raster.hpp"

#include <cmath>
#include <functional>
#include <memory>

namespace mz {

namespace {
Tex32 to_tex(const Canvas& c) {
    Tex32 t;
    t.make(c.w, c.h);
    for (size_t i = 0; i < t.px.size(); ++i) {
        const std::uint8_t* p = &c.px[i * 4];
        const unsigned a = p[3];
        auto un = [&](unsigned v) { return a ? std::min(255u, v * 255 / a) : 0u; };
        t.px[i] = a << 24 | un(p[2]) << 16 | un(p[1]) << 8 | un(p[0]);
    }
    return t;
}
const Col K = hex(0x18121A);
const Col kSkin = hex(0xF4D2B4), kWhite = hex(0xFFFFFF);

void ell(Canvas& c, double x, double y, double rx, double ry, Col col, double ink = 2) {
    c.fill_ellipse(x, y, rx, ry, col);
    if (ink > 0) { c.begin(); c.ellipse(x, y, rx, ry); c.stroke(K, ink); }
}
void poly(Canvas& c, std::initializer_list<std::pair<double, double>> pts, Col col, double ink = 2) {
    c.begin();
    bool first = true;
    for (auto [x, y] : pts) { if (first) c.move(x, y); else c.line(x, y); first = false; }
    c.close();
    c.fill(col);
    if (ink > 0) {
        c.begin();
        first = true;
        for (auto [x, y] : pts) { if (first) c.move(x, y); else c.line(x, y); first = false; }
        c.close();
        c.stroke(K, ink);
    }
}
void eyes(Canvas& c, double x, double y, double sep, double r, double look = 0) {
    for (int s = -1; s <= 1; s += 2) {
        c.fill_ellipse(x + s * sep, y, r, r * 1.2, kWhite);
        c.fill_circle(x + s * sep + look, y + r * .2, r * .55, K);
        c.fill_circle(x + s * sep + look - r * .2, y - r * .1, r * .2, kWhite);
    }
}
void dots(Canvas& c, double x, double y, double sep, double r) {
    c.fill_circle(x - sep, y, r, K);
    c.fill_circle(x + sep, y, r, K);
}
void smile(Canvas& c, double x, double y, double w, double h, double ink = 2) {
    c.begin(); c.move(x - w, y); c.quad(x, y + h, x + w, y); c.stroke(K, ink);
}
void legs(Canvas& c, double x, double top, double sep, double w, Col col, Col shoe) {
    for (int s = -1; s <= 1; s += 2) {
        c.fill_rect(x + s * sep - w / 2, top, w, 122 - top, col);
        ell(c, x + s * sep + s * 2, 123, w * .9, 4, shoe, 1.5);
    }
}

struct Def {
    CastMember m;
    std::function<void(Canvas&)> draw;
};

const std::vector<Def>& defs() {
    static std::vector<Def> v = {
        {{"Alice", "Lewis Carroll, Alice's Adventures in Wonderland (1865), after John Tenniel",
          {"Curiouser and curiouser!", "Which way ought I to go from here? Never mind, I'll find out by walking.", "I can't explain myself, I'm afraid, because I'm not myself, you see.",
           "This is the strangest rabbit hole I've ever fallen down. It has carpet.", "If I find {R} first, I promise I'll share.", "Have you seen a white rabbit? Waistcoat. Watch. Late for something.",
           "I keep growing taller and shorter in here. I think it's the lighting.", "Everything in this maze is a little bit real, and a little bit not."}},
         [](Canvas& c) {
             legs(c, 48, 96, 7, 7, kWhite, K);
             poly(c, {{48, 52}, {74, 100}, {22, 100}}, hex(0xF4D24A));                      // a yellow frock (the Nursery Alice)
             poly(c, {{48, 56}, {64, 98}, {32, 98}}, kWhite);                                // the pinafore
             ell(c, 48, 38, 20, 30, hex(0xF2C850), 0);                                       // long fair hair behind
             ell(c, 48, 32, 14, 16, kSkin);
             c.begin(); c.move(34, 26); c.quad(48, 10, 62, 26); c.stroke(hex(0x3A6AD0), 4);  // the hair band
             eyes(c, 48, 33, 6, 2.6);
             smile(c, 48, 40, 4, 2.5, 1.5);
             c.fill_rect(30, 60, 6, 22, kSkin); c.fill_rect(60, 60, 6, 22, kSkin);
         }},
        {{"The Cheshire Cat", "Lewis Carroll, Alice's Adventures in Wonderland (1865), after John Tenniel",
          {"We're all mad here. I'm mad. You're mad.", "If you don't much care where you get to, then it doesn't matter which way you go.", "I'll be here. Mostly the grin.",
           "{R}? I could tell you where it is. I could also vanish. I prefer vanishing.", "Did you say pig, or fig?", "A corridor is only a long grin with walls on.",
           "You'd be surprised how far a grin can go without its cat."}, .75, .8},
         [](Canvas& c) {
             // half there: a striped body fading out beneath a magnificent grin
             for (int k = 0; k < 7; ++k) c.fill_rect(24, 70 + k * 7, 48, 4, hex(0x8A7A6A, static_cast<float>(.5 - k * .07)));
             ell(c, 48, 52, 30, 24, hex(0xA8987C));
             poly(c, {{24, 40}, {28, 18}, {40, 34}}, hex(0xA8987C));
             poly(c, {{72, 40}, {68, 18}, {56, 34}}, hex(0xA8987C));
             for (int k = 0; k < 4; ++k) c.fill_rect(34 + k * 8, 30, 3, 10, hex(0x6A5A4A));
             c.fill_ellipse(38, 46, 5, 6, hex(0xD8E870)); c.fill_ellipse(58, 46, 5, 6, hex(0xD8E870));
             c.fill_rect(37, 42, 2, 9, K); c.fill_rect(57, 42, 2, 9, K);
             c.begin(); c.move(24, 56); c.quad(48, 86, 72, 56); c.quad(48, 66, 24, 56); c.fill(kWhite);
             c.begin(); c.move(24, 56); c.quad(48, 86, 72, 56); c.quad(48, 66, 24, 56); c.stroke(K, 2);
             for (int k = 1; k < 8; ++k) c.stroke_line(24 + k * 6, 58 + std::sin(k * .45) * 8, 24 + k * 6, 62 + std::sin(k * .45) * 12, K, 1);
         }},
        {{"The Hatter", "Lewis Carroll, Alice's Adventures in Wonderland (1865), after John Tenniel",
          {"Why is a raven like a writing-desk?", "No room! No room!", "It's always six o'clock here, so it's always tea-time.", "Have some tea. There isn't any. Have some anyway.",
           "I've been looking for {R} since half past Tuesday.", "Your hair wants cutting. So does this maze.", "The price is on the hat. Ten and six. In this style."}},
         [](Canvas& c) {
             legs(c, 48, 92, 8, 8, hex(0x6A6A4A), K);
             poly(c, {{30, 50}, {66, 50}, {72, 96}, {24, 96}}, hex(0x3A7A4A));            // a green coat
             poly(c, {{42, 50}, {54, 50}, {48, 62}}, kWhite);
             poly(c, {{40, 52}, {48, 56}, {40, 60}}, hex(0xD03030), 1);                    // the bow tie
             poly(c, {{56, 52}, {48, 56}, {56, 60}}, hex(0xD03030), 1);
             ell(c, 48, 38, 13, 14, kSkin);
             eyes(c, 48, 37, 5, 2.4, 1);
             c.begin(); c.move(40, 44); c.quad(48, 50, 56, 44); c.stroke(K, 1.5);
             c.fill_rect(30, 22, 36, 4, hex(0x3A3A3A));                                    // the great hat
             poly(c, {{34, 23}, {36, 0}, {62, 2}, {62, 23}}, hex(0x5A5A5A));
             c.fill_rect(35, 16, 27, 5, hex(0xC88A3A));
             c.fill_rect(52, 6, 9, 9, hex(0xF4F0E0)); c.stroke_line(54, 9, 59, 9, K, 1); c.stroke_line(54, 12, 58, 12, K, 1);
             ell(c, 72, 72, 6, 4, kWhite, 1.5);                                            // a teacup
         }},
        {{"The White Rabbit", "Lewis Carroll, Alice's Adventures in Wonderland (1865), after John Tenniel",
          {"Oh dear! Oh dear! I shall be too late!", "The Duchess! Oh my dear paws! Oh my fur and whiskers!", "No time! There's never time in a maze.",
           "{R}? It's past the third left, or the fourth right, or not at all. I'm late!", "Have you seen my gloves? White kid gloves. And a fan.", "Tick, tock. That's me. Mostly tock."}, .6, .9},
         [](Canvas& c) {
             legs(c, 48, 98, 7, 6, kWhite, kWhite);
             ell(c, 48, 80, 17, 22, kWhite);
             poly(c, {{34, 64}, {62, 64}, {60, 92}, {36, 92}}, hex(0xC83A3A));            // the waistcoat
             c.fill_circle(48, 72, 1.5, hex(0xF0C030)); c.fill_circle(48, 80, 1.5, hex(0xF0C030));
             ell(c, 66, 76, 6, 6, hex(0xF0C030), 1.5);                                     // the watch
             c.stroke_line(48, 74, 62, 74, hex(0xF0C030), 1);
             ell(c, 48, 46, 13, 13, kWhite);
             ell(c, 40, 18, 5, 16, kWhite); ell(c, 56, 18, 5, 16, kWhite);
             c.fill_ellipse(40, 18, 2, 11, hex(0xF4B0C0)); c.fill_ellipse(56, 18, 2, 11, hex(0xF4B0C0));
             c.fill_circle(43, 45, 2.2, hex(0xC02040)); c.fill_circle(53, 45, 2.2, hex(0xC02040));
             c.fill_circle(48, 51, 1.6, hex(0xF080A0));
         }},
        {{"The Queen of Hearts", "Lewis Carroll, Alice's Adventures in Wonderland (1865), after John Tenniel",
          {"Off with their heads!", "Off with its head! The maze's, I mean. Has a maze got a head?", "All ways here are my ways.", "Who has been painting my walls? Off with the snail's head!",
           "{R} is mine. Everything is mine. Off with your head, for asking.", "Do you play croquet? With the marble, I mean. It does not like it."}},
         [](Canvas& c) {
             poly(c, {{48, 48}, {80, 124}, {16, 124}}, hex(0xD02030));
             for (int k = 0; k < 3; ++k) { c.fill_circle(40 + k * 8, 90 + k * 8, 4, kWhite); c.fill_circle(36 + k * 12, 110, 3, kWhite); }
             poly(c, {{40, 50}, {56, 50}, {60, 70}, {36, 70}}, hex(0xF0E8D0));
             ell(c, 48, 36, 13, 14, kSkin);
             dots(c, 48, 35, 5, 1.8);
             c.stroke_line(42, 30, 46, 32, K, 1.5); c.stroke_line(54, 30, 50, 32, K, 1.5);
             c.begin(); c.move(43, 43); c.quad(48, 40, 53, 43); c.stroke(hex(0xA02020), 2);
             poly(c, {{36, 24}, {36, 12}, {42, 18}, {48, 8}, {54, 18}, {60, 12}, {60, 24}}, hex(0xF0C030), 1.5);
             c.stroke_line(72, 50, 76, 90, hex(0xF0C030), 2.5); c.fill_circle(72, 48, 4, hex(0xD02030));
         }},
        {{"Humpty Dumpty", "Lewis Carroll, Through the Looking-Glass (1871), after John Tenniel",
          {"When I use a word, it means just what I choose it to mean, neither more nor less.", "Don't stand there chattering to yourself like that. Tell me your name and your business.",
           "There's glory for you!", "I'd sit on a wall, but these walls go all the way up.", "{R}? Impenetrability! That's what I say.", "Un-birthday presents are the best kind."}, .7, .85},
         [](Canvas& c) {
             for (int y = 96; y < 126; y += 8) for (int x = 18 + (y / 8 % 2) * 8; x < 80; x += 16) poly(c, {{double(x), double(y)}, {double(x + 15), double(y)}, {double(x + 15), double(y + 7)}, {double(x), double(y + 7)}}, hex(0xB85A3A), 1);
             ell(c, 48, 60, 26, 34, hex(0xF4ECD4));
             poly(c, {{24, 70}, {72, 70}, {70, 84}, {26, 84}}, hex(0x3A5AC0));             // the cravat
             poly(c, {{40, 68}, {48, 76}, {56, 68}, {48, 72}}, hex(0xF0C030), 1);
             eyes(c, 48, 50, 8, 3.5);
             c.begin(); c.move(36, 62); c.quad(48, 70, 60, 62); c.stroke(K, 2);
         }},
        {{"Dorothy", "L. Frank Baum, The Wonderful Wizard of Oz (1900), after W. W. Denslow",
          {"There is no place like home. Though this place has a lot of carpet.", "Toto, keep close. That marble is as big as a house.", "I'm looking for a way back. And {R}, apparently.",
           "My silver shoes are pinching. Do you think they're magic?", "We followed a yellow road once. This one's all bricks, and none of them yellow.", "Have you seen a scarecrow? He goes off thinking."}},
         [](Canvas& c) {
             legs(c, 48, 98, 7, 7, kWhite, hex(0xC8CCD8));
             poly(c, {{48, 52}, {72, 100}, {24, 100}}, hex(0x5A80D0));
             for (int y = 58; y < 100; y += 6) c.fill_rect(30, y, 36, 2, hex(0xF0F0FF, .8f));       // gingham
             for (int x = 30; x < 68; x += 6) c.fill_rect(x, 58, 2, 40, hex(0xF0F0FF, .6f));
             ell(c, 48, 34, 14, 15, kSkin);
             ell(c, 32, 40, 4, 10, hex(0x6A3A1A), 1.5); ell(c, 64, 40, 4, 10, hex(0x6A3A1A), 1.5);   // plaits
             c.begin(); c.move(34, 28); c.quad(48, 14, 62, 28); c.line(62, 24); c.quad(48, 16, 34, 24); c.fill(hex(0x6A3A1A));
             eyes(c, 48, 34, 6, 2.5);
             smile(c, 48, 41, 4, 2.5, 1.5);
             ell(c, 72, 108, 9, 6, hex(0x1A1A1A), 1);                                               // Toto
             c.fill_circle(78, 102, 4, hex(0x1A1A1A)); c.fill_circle(79, 101, 1, kWhite);
         }},
        {{"The Scarecrow", "L. Frank Baum, The Wonderful Wizard of Oz (1900), after W. W. Denslow",
          {"I haven't got any brains, you know. But I've a very good sense of direction.", "I don't mind being stuffed with straw. It means I can't be hurt.", "If I had brains I'd tell you where {R} is.",
           "Crows used to be frightened of me. Then I met a marble.", "I keep thinking. It doesn't seem to help.", "Left, I think. Or the other left."}},
         [](Canvas& c) {
             legs(c, 48, 94, 8, 8, hex(0x3A6AB0), hex(0x6A4A2A));
             poly(c, {{28, 50}, {68, 50}, {72, 96}, {24, 96}}, hex(0x3A6AB0));
             for (int k = 0; k < 5; ++k) c.stroke_line(24 + k * 2, 96, 18 + k * 3, 104, hex(0xE8C860), 1.5);
             c.fill_rect(10, 54, 76, 7, hex(0x3A6AB0)); for (int k = 0; k < 3; ++k) { c.stroke_line(10, 58, 4, 54 + k * 4, hex(0xE8C860), 1.5); c.stroke_line(86, 58, 92, 54 + k * 4, hex(0xE8C860), 1.5); }
             ell(c, 48, 36, 14, 15, hex(0xF0E8C8));
             dots(c, 48, 34, 5, 2.2);
             c.begin(); c.move(40, 42); c.quad(48, 48, 56, 42); c.stroke(hex(0xC03030), 2);
             poly(c, {{30, 24}, {66, 24}, {58, 14}, {38, 14}}, hex(0x3A6AB0));
             poly(c, {{40, 14}, {56, 14}, {52, 2}, {44, 2}}, hex(0x3A6AB0));
         }},
        {{"The Tin Woodman", "L. Frank Baum, The Wonderful Wizard of Oz (1900), after W. W. Denslow",
          {"Have you an oil-can? My joints are a little stiff from all this standing still.", "I have no heart, so I'm very careful not to be unkind.", "I could chop through a wall, but it seems rude to the maze.",
           "{R}? Splendid. I hope it makes you happy. I'd be happy, if I could.", "Squeak. Pardon me. Squeak."}},
         [](Canvas& c) {
             legs(c, 48, 94, 8, 9, hex(0xB8C0C8), hex(0x8A9098));
             poly(c, {{30, 50}, {66, 50}, {66, 96}, {30, 96}}, hex(0xC8D0D8));
             for (int y = 58; y < 96; y += 12) c.stroke_line(30, y, 66, y, hex(0x8A9098), 1);
             ell(c, 48, 66, 5, 5, hex(0xD02040), 1);                                                // the heart he was given
             c.fill_rect(22, 54, 8, 32, hex(0xB8C0C8)); c.fill_rect(66, 54, 8, 32, hex(0xB8C0C8));
             c.stroke_line(78, 40, 78, 100, hex(0x8A5A2A), 3); poly(c, {{70, 40}, {86, 36}, {86, 50}, {70, 48}}, hex(0xC0C8D0), 1.5);
             poly(c, {{36, 22}, {60, 22}, {60, 48}, {36, 48}}, hex(0xC8D0D8));
             eyes(c, 48, 32, 6, 2.6);
             c.stroke_line(42, 42, 54, 42, K, 1.5);
             poly(c, {{38, 22}, {58, 22}, {48, 4}}, hex(0xB8C0C8));                                // the funnel hat
         }},
        {{"The Cowardly Lion", "L. Frank Baum, The Wonderful Wizard of Oz (1900), after W. W. Denslow",
          {"I'm afraid of the marble. I'm afraid of the dark. I'm afraid of carpet, a bit.", "Roar! ...sorry. Was that too loud?", "Be brave, they said. In a maze? Have they seen it?",
           "If you find {R}, could you carry it past the snail for me?", "My heart beats so fast when that bulb pops."}, .85, .9},
         [](Canvas& c) {
             ell(c, 48, 92, 26, 22, hex(0xD8A050));
             legs(c, 48, 104, 14, 9, hex(0xD8A050), hex(0xC89040));
             ell(c, 48, 48, 30, 30, hex(0xA0602A));                                                // the mane
             ell(c, 48, 50, 19, 19, hex(0xE0B060));
             eyes(c, 48, 46, 7, 3, -1);
             c.stroke_line(38, 38, 44, 40, K, 1.5); c.stroke_line(58, 38, 52, 40, K, 1.5);
             c.fill_ellipse(48, 56, 5, 3.5, hex(0x6A3A2A));
             c.begin(); c.move(42, 62); c.quad(48, 58, 54, 62); c.stroke(K, 1.5);
         }},
        {{"Winnie-the-Pooh", "A. A. Milne, Winnie-the-Pooh (1926), after E. H. Shepard",
          {"Bother.", "I am a Bear of Very Little Brain, and long words Bother me.", "Is there honey at the end of this maze? I'd go for honey.",
           "I was going to look for {R}, but I may have had a little something first.", "When you are a Bear of Very Little Brain, mazes are mostly corridors.", "Hallo! Is anybody at home?"}, .7, .85},
         [](Canvas& c) {
             legs(c, 48, 104, 10, 11, hex(0xD8A048), hex(0xD8A048));
             ell(c, 48, 86, 22, 24, hex(0xE0AA50));
             ell(c, 48, 50, 18, 17, hex(0xE0AA50));
             ell(c, 32, 36, 6, 6, hex(0xE0AA50)); ell(c, 64, 36, 6, 6, hex(0xE0AA50));
             dots(c, 48, 48, 6, 2);
             c.fill_ellipse(48, 56, 4, 3, K);
             smile(c, 48, 60, 5, 2.5, 1.5);
             poly(c, {{70, 84}, {86, 84}, {84, 104}, {72, 104}}, hex(0xC8803A));                   // a pot of hunny
             c.fill_rect(70, 84, 16, 4, hex(0xF0B030));
         }},
        {{"Piglet", "A. A. Milne, Winnie-the-Pooh (1926), after E. H. Shepard",
          {"Oh, d-d-dear. It's very dark in here. And very m-m-mazy.", "It is hard to be brave, when you're only a Very Small Animal.", "I'm holding a balloon for a friend. It's for his birthday. Do you know where he lives?",
           "Is {R} very big? I could help, if it's not very big.", "If the marble comes, I shall be Behind You."}, .45, .6},
         [](Canvas& c) {
             c.stroke_line(64, 20, 58, 86, K, 1); ell(c, 66, 14, 10, 12, hex(0xE83040));
             legs(c, 46, 110, 6, 6, hex(0xF0B8C0), hex(0xF0B8C0));
             ell(c, 46, 98, 14, 16, hex(0xF0B8C0));
             ell(c, 46, 72, 13, 13, hex(0xF0B8C0));
             poly(c, {{36, 62}, {32, 50}, {42, 58}}, hex(0xF0B8C0), 1.5); poly(c, {{56, 62}, {60, 50}, {50, 58}}, hex(0xF0B8C0), 1.5);
             dots(c, 46, 70, 4, 1.6);
             ell(c, 46, 77, 3.5, 2.5, hex(0xE890A0), 1);
         }},
        {{"Eeyore", "A. A. Milne, Winnie-the-Pooh (1926), after E. H. Shepard",
          {"Good morning. If it is a good morning. Which I doubt.", "Somebody has taken my tail. Again. You haven't seen a tail in here?", "A maze. Of course. Why not.",
           "{R}? I wouldn't get my hopes up. I never do.", "Nobody ever comes this way. And now you have. Hm.", "Thistles would be nice. There are no thistles."}, .85, .8},
         [](Canvas& c) {
             ell(c, 52, 88, 28, 18, hex(0x8A8A94));
             legs(c, 52, 98, 18, 7, hex(0x8A8A94), hex(0x5A5A64));
             c.stroke_line(80, 84, 90, 100, K, 1.5); ell(c, 91, 102, 3, 4, hex(0xE060A0), 1);       // a tail held on with a nail
             ell(c, 28, 66, 14, 18, hex(0x9A9AA4));
             ell(c, 22, 78, 9, 8, hex(0xB8B8C0));
             poly(c, {{22, 50}, {16, 34}, {28, 46}}, hex(0x8A8A94)); poly(c, {{34, 50}, {38, 32}, {40, 48}}, hex(0x8A8A94));
             c.stroke_line(18, 62, 24, 63, K, 2); c.stroke_line(30, 62, 36, 63, K, 2);           // heavy lids
             c.fill_rect(30, 50, 6, 30, hex(0x3A3A44));                                          // a dark mane
         }},
        {{"Tigger", "A. A. Milne, The House at Pooh Corner (1928), after E. H. Shepard",
          {"Tiggers don't like honey. Or mazes. Tiggers like bouncing.", "Hallo! I'm a Tigger! Is this where you live?", "Tiggers can climb walls. Not these walls, but walls in general.",
           "Shall I bounce you to {R}? Hold on to something.", "What do Tiggers like? Everything except whatever's in front of them."}, .7, .9},
         [](Canvas& c) {
             legs(c, 48, 100, 9, 9, hex(0xF09030), hex(0xF09030));
             ell(c, 48, 82, 18, 22, hex(0xF09030));
             for (int k = 0; k < 4; ++k) c.fill_rect(32, 68 + k * 8, 10, 3, K), c.fill_rect(54, 72 + k * 8, 10, 3, K);
             c.begin(); c.move(66, 92); c.quad(90, 96, 84, 70); c.stroke(hex(0xF09030), 4);
             ell(c, 48, 48, 17, 16, hex(0xF09030));
             ell(c, 34, 34, 5, 5, hex(0xF09030)); ell(c, 62, 34, 5, 5, hex(0xF09030));
             for (int k = 0; k < 3; ++k) c.fill_rect(42 + k * 5, 33, 2, 7, K);
             eyes(c, 48, 46, 6, 2.6);
             ell(c, 48, 56, 7, 5, hex(0xF8E8C0), 1);
             c.fill_ellipse(48, 53, 3, 2, hex(0xE06080));
         }},
        {{"Popeye", "E. C. Segar, Thimble Theatre (1929)",
          {"Blow me down! A maze with no water in it!", "I'm a sailor. I can steer anything, even a hallway.", "That marble's got nothin' on me. Well. Almost nothin'.",
           "{R}? Sounds like grub. Is there spinach in it?", "Arf! Arf! Which way's starboard down here?", "I fights fair. Mazes don't."}},
         [](Canvas& c) {
             legs(c, 48, 96, 9, 9, hex(0x2A2A4A), hex(0x6A3A1A));
             poly(c, {{30, 52}, {66, 52}, {64, 96}, {32, 96}}, hex(0x2A2A4A));
             poly(c, {{36, 52}, {60, 52}, {48, 64}}, hex(0x3A60D0));
             ell(c, 22, 78, 9, 13, kSkin); ell(c, 74, 78, 9, 13, kSkin);                          // the forearms
             ell(c, 48, 38, 14, 14, kSkin);
             c.fill_circle(44, 36, 2.2, K); c.stroke_line(51, 36, 56, 36, K, 2);                 // one eye squinted
             ell(c, 56, 42, 7, 4, kSkin, 1.5);                                                    // the chin
             c.stroke_line(58, 44, 70, 50, hex(0x8A5A2A), 2.5); c.fill_rect(68, 46, 5, 6, hex(0x8A5A2A));   // the pipe
             poly(c, {{34, 26}, {62, 26}, {58, 18}, {38, 18}}, kWhite, 1.5);
         }},
        {{"Olive Oyl", "E. C. Segar, Thimble Theatre (1919)",
          {"Oh, my goodness! A maze!", "I'm looking for a sailor. Have you seen a sailor? Squints a bit.", "These corridors go on longer than my legs.",
           "{R}? Well I never. Is it for me?", "Don't you dare roll at me, you great marble!"}, .55, 1.0},
         [](Canvas& c) {
             legs(c, 48, 90, 4, 4, kSkin, hex(0x6A2A1A));
             poly(c, {{40, 48}, {56, 48}, {62, 92}, {34, 92}}, hex(0x2A2A2A));
             poly(c, {{40, 40}, {56, 40}, {56, 62}, {40, 62}}, hex(0xD03040));
             c.fill_rect(34, 42, 4, 32, kSkin); c.fill_rect(58, 42, 4, 32, kSkin);
             ell(c, 48, 26, 9, 13, kSkin);
             ell(c, 56, 14, 6, 6, hex(0x1A1A1A), 1);                                              // the bun
             c.fill_rect(39, 14, 18, 4, hex(0x1A1A1A));
             dots(c, 48, 25, 4, 1.6);
             c.begin(); c.move(44, 32); c.quad(48, 34, 52, 32); c.stroke(K, 1.2);
         }},
        {{"Krazy Kat", "George Herriman, Krazy Kat (1913)",
          {"L'il dollink! Is that a brick, or a valentine?", "A maze is just a garden what forgot to grow.", "I'm looking for a mouse. He says he loves me. With bricks.",
           "{R}? Heppy, heppy day!", "Ain't the moon bright tonight? Oh. It's a ceiling."}, .6, .9},
         [](Canvas& c) {
             legs(c, 48, 98, 6, 5, K, K);
             ell(c, 48, 80, 13, 20, K, 0);
             c.begin(); c.move(58, 92); c.quad(84, 100, 76, 70); c.stroke(K, 3);
             ell(c, 48, 46, 15, 14, K, 0);
             poly(c, {{36, 38}, {34, 22}, {44, 34}}, K, 0); poly(c, {{60, 38}, {62, 22}, {52, 34}}, K, 0);
             ell(c, 48, 50, 10, 8, kWhite, 0);
             dots(c, 48, 46, 4, 1.6);
             c.fill_circle(48, 51, 1.8, K);
             poly(c, {{40, 62}, {56, 62}, {48, 70}}, hex(0xF0C030), 1);                           // a little bow
         }},
        {{"Ignatz Mouse", "George Herriman, Krazy Kat (1913)",
          {"Has a cat been through here? Black, daft, sentimental?", "This brick isn't for you. Probably.", "A maze is a terrible place to throw a brick. Too many walls.",
           "{R}? Ha. I'll trade you a brick for it.", "Don't tell the constable you saw me."}, .45, .7},
         [](Canvas& c) {
             legs(c, 48, 104, 5, 4, hex(0x6A6A6A), K);
             ell(c, 48, 90, 10, 16, hex(0xC8C8C8));
             ell(c, 48, 62, 11, 11, hex(0xC8C8C8));
             ell(c, 36, 50, 7, 7, hex(0xC8C8C8)); ell(c, 60, 50, 7, 7, hex(0xC8C8C8));
             dots(c, 48, 60, 4, 1.6);
             c.fill_circle(48, 68, 2, K);
             poly(c, {{58, 78}, {76, 72}, {78, 80}, {60, 86}}, hex(0xB84A2A), 1.5);                // the brick
         }},
        {{"Gertie the Dinosaur", "Winsor McCay, Gertie the Dinosaur (1914)",
          {"Rrrrumble? That's hello, in dinosaur.", "I'm ever so careful not to step on anyone.", "Is the marble an egg? It's a very large egg.",
           "I'd carry {R} for you, but I'd knock the walls down.", "Bow, Gertie! Like this!"}, 1.0, .95},
         [](Canvas& c) {
             ell(c, 52, 92, 30, 20, hex(0x6AA058));
             legs(c, 52, 100, 18, 10, hex(0x6AA058), hex(0x5A8A48));
             c.begin(); c.move(80, 92); c.quad(96, 98, 94, 116); c.stroke(hex(0x6AA058), 6);
             c.begin(); c.move(30, 84); c.quad(14, 60, 22, 30); c.stroke(hex(0x6AA058), 10);
             ell(c, 26, 26, 11, 9, hex(0x6AA058));
             c.fill_circle(29, 23, 2.2, K); c.fill_circle(29.5, 22.5, .8, kWhite);
             smile(c, 22, 30, 5, 2.5, 1.5);
         }},
        {{"Count Orlok", "F. W. Murnau, Nosferatu (1922)",
          {"Your maze has a beautiful neck. Corridor. I meant corridor.", "I keep away from the light bulbs. For reasons.", "So late, and you are still wandering? How... delightful.",
           "{R}? I care nothing for {R}. I care for the dark.", "The marble and I have an understanding. It stays in the corridors. I stay in the shadows."}},
         [](Canvas& c) {
             legs(c, 48, 100, 7, 6, hex(0x1A1A20), K);
             poly(c, {{34, 40}, {62, 40}, {70, 104}, {26, 104}}, hex(0x1A1A22));
             for (int s = -1; s <= 1; s += 2) for (int k = 0; k < 4; ++k) c.stroke_line(48 + s * 18, 74, 48 + s * (24 + k * 2), 86 + k, hex(0xE8E4D8), 1.2);   // long fingers
             ell(c, 48, 28, 11, 16, hex(0xE8E4D8));
             poly(c, {{38, 22}, {32, 14}, {40, 18}}, hex(0xE8E4D8), 1.5); poly(c, {{58, 22}, {64, 14}, {56, 18}}, hex(0xE8E4D8), 1.5);
             c.fill_circle(44, 26, 2.2, K); c.fill_circle(52, 26, 2.2, K);
             c.stroke_line(40, 22, 46, 24, K, 1.5); c.stroke_line(56, 22, 50, 24, K, 1.5);
             c.fill_rect(46, 38, 2, 4, kWhite); c.fill_rect(49, 38, 2, 4, kWhite);
         }},
        {{"Sherlock Holmes", "Arthur Conan Doyle, the Holmes stories, after Sidney Paget",
          {"You see, but you do not observe.", "When you have eliminated the impossible, whatever remains, however improbable, must be the truth.", "The marble rolls clockwise. Elementary.",
           "Someone has been repainting these walls. A small, slow, gastropod someone.", "{R}, I deduce, lies beyond a door you have not yet opened.", "The game is afoot! Somewhere to the left."}},
         [](Canvas& c) {
             legs(c, 48, 98, 7, 7, hex(0x5A5040), K);
             poly(c, {{30, 46}, {66, 46}, {74, 100}, {22, 100}}, hex(0x8A7A5A));
             poly(c, {{26, 48}, {70, 48}, {76, 74}, {20, 74}}, hex(0x9A8A6A));                    // the cape
             ell(c, 48, 34, 12, 14, kSkin);
             dots(c, 48, 33, 5, 1.6);
             c.stroke_line(48, 33, 50, 40, K, 1.5);
             poly(c, {{34, 26}, {62, 26}, {58, 14}, {38, 14}}, hex(0x8A7A5A));                   // the deerstalker
             poly(c, {{34, 26}, {28, 30}, {36, 30}}, hex(0x8A7A5A), 1);
             c.stroke_line(54, 42, 62, 46, hex(0x6A4A2A), 2); c.fill_rect(60, 44, 5, 5, hex(0x6A4A2A));
             ell(c, 78, 70, 6, 6, hex(0xC8E8F8, .6f), 2); c.stroke_line(73, 75, 66, 84, hex(0x6A4A2A), 2.5);
         }},
        {{"Pinocchio", "Carlo Collodi, The Adventures of Pinocchio (1883), after Enrico Mazzanti",
          {"I know exactly where {R} is.", "I'm a real boy. Well. A real puppet. Well. Wood.", "I am not lost. (Did my nose just grow?)",
           "The cricket told me to stay home. I never listen.", "I'll go and look for {R} right after school. Which I am not skipping."}, .55, .9},
         [](Canvas& c) {
             legs(c, 48, 96, 6, 5, hex(0xC89A6A), hex(0x6A3A1A));
             poly(c, {{36, 52}, {60, 52}, {62, 96}, {34, 96}}, hex(0xF0EEE0));
             c.fill_rect(30, 56, 6, 26, hex(0xC89A6A)); c.fill_rect(60, 56, 6, 26, hex(0xC89A6A));
             ell(c, 48, 40, 12, 12, hex(0xD8AA78));
             c.stroke_line(58, 42, 82, 40, hex(0xC89A6A), 3);                                     // the nose
             dots(c, 48, 38, 4, 1.8);
             poly(c, {{36, 32}, {60, 32}, {50, 4}}, hex(0xF0EEE0));                               // the pointed cap
         }},
        {{"Peter Rabbit", "Beatrix Potter, The Tale of Peter Rabbit (1902)",
          {"I've lost my jacket twice. Not in here. In a garden.", "Is there a gardener in this maze? With a rake?", "I squeezed under a gate once. These walls haven't got gates.",
           "{R}? Is it near any lettuces?", "My mother said not to go into the maze. So I did."}, .5, .8},
         [](Canvas& c) {
             legs(c, 48, 104, 6, 5, hex(0xA88A6A), hex(0xA88A6A));
             ell(c, 48, 88, 13, 18, hex(0xB89A78));
             poly(c, {{34, 72}, {62, 72}, {62, 96}, {34, 96}}, hex(0x4A6AC0));                    // the blue jacket
             c.fill_circle(48, 80, 1.6, hex(0xF0C030)); c.fill_circle(48, 88, 1.6, hex(0xF0C030));
             ell(c, 48, 56, 12, 12, hex(0xB89A78));
             ell(c, 42, 32, 4, 14, hex(0xB89A78)); ell(c, 54, 32, 4, 14, hex(0xB89A78));
             dots(c, 48, 54, 4, 1.8);
             c.fill_circle(48, 60, 1.4, hex(0xD08090));
         }},
        {{"Mr. Toad", "Kenneth Grahame, The Wind in the Willows (1908)",
          {"Poop-poop!", "The only way to see a maze is at speed! Poop-poop!", "I say, have you a motor-car? I could find {R} in a jiffy.",
           "I am the cleverest toad that ever lived, and I am quite lost.", "Ratty says I'm reckless. Ratty is not here."}, .75, .8},
         [](Canvas& c) {
             legs(c, 48, 104, 10, 8, hex(0x6A9A4A), hex(0x5A7A3A));
             ell(c, 48, 86, 22, 20, hex(0x6A9A4A));
             poly(c, {{28, 74}, {68, 74}, {72, 100}, {24, 100}}, hex(0x8A6A3A));                  // a tweed coat
             ell(c, 48, 56, 20, 14, hex(0x7AAA5A));
             for (int s = -1; s <= 1; s += 2) { ell(c, 48 + s * 9, 50, 7, 6, hex(0xC8E8F0), 2); c.fill_circle(48 + s * 9, 51, 2.5, K); }   // motoring goggles
             c.stroke_line(30, 50, 66, 50, hex(0x6A4A2A), 2);
             c.begin(); c.move(36, 62); c.quad(48, 68, 60, 62); c.stroke(K, 2);
         }},
        {{"The Gingerbread Man", "the folk tale, as printed in St. Nicholas Magazine (1875)",
          {"Run, run, as fast as you can! You can't catch me, I'm the Gingerbread Man!", "I've run away from an old woman, an old man, a cow and a horse. And now a maze.",
           "Is there a fox in here? I'm not swimming anywhere.", "{R}? Pfft. Catch me first!"}, .55, .75},
         [](Canvas& c) {
             poly(c, {{40, 60}, {56, 60}, {66, 120}, {54, 120}, {48, 96}, {42, 120}, {30, 120}}, hex(0xC07A3A));
             poly(c, {{40, 62}, {56, 62}, {76, 72}, {74, 80}, {56, 74}, {40, 74}, {22, 80}, {20, 72}}, hex(0xC07A3A));
             ell(c, 48, 50, 14, 14, hex(0xC07A3A));
             dots(c, 48, 48, 5, 2);
             smile(c, 48, 54, 5, 3, 1.5);
             for (int k = 0; k < 3; ++k) c.fill_circle(48, 70 + k * 8, 2.2, hex(0xF03040));
             c.stroke_line(24, 75, 30, 75, kWhite, 1.5); c.stroke_line(66, 75, 72, 75, kWhite, 1.5);
         }},
        {{"Tux", "Larry Ewing's Linux penguin, drawn with credit and thanks",
          {"Free as in freedom! Free as in maze!", "Have you tried turning the maze off and on again?", "I'd compile a map, but the snail keeps patching the walls.",
           "{R}? There's probably a package for that.", "Waddle, waddle. I am very fast on ice. This is carpet."}, .6, .75},
         [](Canvas& c) {
             ell(c, 48, 82, 22, 30, hex(0x14141A));
             ell(c, 48, 88, 15, 22, hex(0xF8F8F0), 0);
             ell(c, 48, 46, 16, 15, hex(0x14141A));
             ell(c, 42, 44, 4, 5, kWhite, 0); ell(c, 54, 44, 4, 5, kWhite, 0);
             c.fill_circle(43, 45, 2, K); c.fill_circle(53, 45, 2, K);
             c.begin(); c.move(40, 52); c.quad(48, 48, 56, 52); c.quad(48, 58, 40, 52); c.fill(hex(0xF0B020));
             ell(c, 38, 112, 9, 4, hex(0xF0B020), 1.5); ell(c, 58, 112, 9, 4, hex(0xF0B020), 1.5);
         }},
        {{"Sun Wukong", "Wu Cheng'en, Journey to the West (16th century)",
          {"I am the Great Sage, Equal of Heaven! And I am in a maze.", "I can leap a hundred thousand li in one somersault. Not with a ceiling.", "My staff can grow as long as I like. Shall I poke the marble?",
           "{R}? I'll fetch it in a blink. Seventy-two transformations should do it.", "The Buddha's palm was a maze too, in its way."}},
         [](Canvas& c) {
             legs(c, 48, 98, 8, 7, hex(0xC86A2A), hex(0x2A2A2A));
             poly(c, {{32, 52}, {64, 52}, {66, 98}, {30, 98}}, hex(0xC82A2A));
             c.fill_rect(26, 70, 44, 4, hex(0xF0C030));
             c.stroke_line(80, 20, 80, 120, hex(0xC8A040), 4); c.fill_rect(77, 18, 6, 6, hex(0xC82A2A)); c.fill_rect(77, 114, 6, 6, hex(0xC82A2A));
             ell(c, 48, 38, 15, 15, hex(0xA8783A));
             ell(c, 48, 42, 10, 9, hex(0xE8C8A0), 0);
             c.begin(); c.move(32, 30); c.quad(48, 22, 64, 30); c.stroke(hex(0xF0C030), 3.5);     // the golden headband
             eyes(c, 48, 38, 5, 2.6);
             smile(c, 48, 46, 5, 3, 1.5);
         }},
        {{"Robin Hood", "the English ballads (15th century)",
          {"Rob from the rich, give to the lost. That's you.", "Sherwood had more trees. And fewer marbles.", "I could split an arrow at a hundred paces. Not much use here.",
           "{R}? Belongs to the people, I'd say. Go and get it.", "Have you seen a large friar? Or a little John? He's large too."}},
         [](Canvas& c) {
             legs(c, 48, 96, 7, 7, hex(0x3A5A2A), hex(0x6A4A2A));
             poly(c, {{32, 50}, {64, 50}, {68, 96}, {28, 96}}, hex(0x4A8A3A));
             c.fill_rect(28, 74, 40, 3, hex(0x6A4A2A));
             c.begin(); c.move(72, 40); c.quad(88, 70, 72, 100); c.stroke(hex(0x8A5A2A), 2.5); c.stroke_line(72, 40, 72, 100, K, .8);
             ell(c, 48, 38, 12, 13, kSkin);
             dots(c, 48, 37, 4, 1.6);
             smile(c, 48, 44, 4, 2, 1.2);
             poly(c, {{34, 30}, {62, 30}, {50, 16}}, hex(0x4A8A3A));
             c.stroke_line(56, 22, 68, 10, hex(0xD03030), 2);                                     // a feather
         }},
        {{"The Frog Prince", "the Brothers Grimm (1812)",
          {"Kiss me? No? Fair enough. Mazes aren't romantic.", "I fetched a golden ball from a well once. This one's stone, and it bites back.", "I am a prince, you know. Underneath.",
           "{R} would look splendid in my pond.", "Ribbit. Excuse me. Royal ribbit."}, .6, .7},
         [](Canvas& c) {
             ell(c, 48, 98, 22, 18, hex(0x4AA040));
             ell(c, 30, 112, 9, 5, hex(0x4AA040)); ell(c, 66, 112, 9, 5, hex(0x4AA040));
             ell(c, 48, 76, 20, 13, hex(0x5AB050));
             ell(c, 38, 64, 7, 7, hex(0x5AB050)); ell(c, 58, 64, 7, 7, hex(0x5AB050));
             c.fill_circle(38, 64, 3, K); c.fill_circle(58, 64, 3, K);
             c.begin(); c.move(34, 80); c.quad(48, 88, 62, 80); c.stroke(K, 2);
             poly(c, {{38, 58}, {38, 46}, {43, 52}, {48, 42}, {53, 52}, {58, 46}, {58, 58}}, hex(0xF0C030), 1.5);
         }},
        {{"A Knight Errant", "a stock figure of the old romances",
          {"Halt! Who goes there? Oh. You. Carry on.", "I have quested for the Grail for forty years. Have you seen a grail?", "This castle has rather a lot of corridors and no throne room.",
           "I shall guard {R} with my life. Once I find it.", "My armour squeaks. I am told it is fashionable."}},
         [](Canvas& c) {
             legs(c, 48, 96, 8, 9, hex(0xA8B0B8), hex(0x8A9098));
             poly(c, {{30, 48}, {66, 48}, {66, 96}, {30, 96}}, hex(0xB8C0C8));
             poly(c, {{40, 54}, {56, 54}, {56, 90}, {40, 90}}, hex(0xD02030), 1.5);
             c.fill_rect(46, 54, 4, 36, kWhite); c.fill_rect(40, 64, 16, 4, kWhite);
             ell(c, 48, 32, 14, 16, hex(0xB8C0C8));
             c.fill_rect(38, 30, 20, 3, K);
             c.stroke_line(76, 30, 76, 100, hex(0xC8D0D8), 3); c.fill_rect(70, 74, 12, 3, hex(0x8A6A3A));
             c.begin(); c.move(48, 16); c.quad(54, 4, 64, 8); c.stroke(hex(0xD02030), 3);
         }},
    };
    return v;
}
}  // namespace

int cast_count() { return static_cast<int>(defs().size()); }
const CastMember& cast_member(int i) { return defs()[static_cast<size_t>(((i % cast_count()) + cast_count()) % cast_count())].m; }
const Tex32& cast_tex(int i) {
    static std::vector<std::unique_ptr<Tex32>> cache(static_cast<size_t>(cast_count()));
    const size_t k = static_cast<size_t>(((i % cast_count()) + cast_count()) % cast_count());
    if (!cache[k]) {
        Canvas c;
        c.resize(128, 128);  // power-of-two for the renderer; the 96-wide drawing sits in the middle
        c.clear({0, 0, 0, 0});
        c.translate(16, 0);
        defs()[k].draw(c);
        cache[k] = std::make_unique<Tex32>(to_tex(c));
    }
    return *cache[k];
}

}  // namespace mz
