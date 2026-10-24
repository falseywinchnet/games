#include "eggy_music.hpp"

#include "eggy_compose.hpp"
#include "eggy_cues.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// authoring/eggy/audio_src/eggy_music.py: the eight region loops, each a spec, an
// arrangement and the script's levelling. main() renders every song in one process,
// so synth._rng and the render_note cache run on from song to song: each song here
// starts from the synth._rng state the script had when it began that song, with the
// few cached notes it reuses (that drew from _rng when first rendered) primed from the
// states they were first rendered at, all captured from the script itself.
//
// The levelling: the master is scaled to a 0.5 peak, written as 24-bit PCM and measured
// by ffmpeg's ebur128 filter; the script then applies (-19 - I) dB, I as ffmpeg prints
// it to a tenth, and a soft limit. integrated_loudness() is that filter's measurement.
namespace eggy {
namespace {

using namespace compose;
using voice::Signal;

const std::vector<int> major{0, 2, 4, 5, 7, 9, 11};
const std::vector<int> dorian{0, 2, 3, 5, 7, 9, 10};
const std::vector<int> aeolian{0, 2, 3, 5, 7, 8, 10};
const std::vector<int> penta{0, 2, 4, 7, 9};

// songs.S(name, bars, layers, lead, center, e)
Section sec(std::string name, std::vector<std::string> bars, const std::string& layers, std::string lead = "", int center = 72,
            double e = 1.0) {
    Section s{std::move(name), std::move(bars), {}, std::move(lead), center, e};
    std::string word{};
    for (const char c : layers + " ") {
        if (c == ' ') {
            if (!word.empty()) {
                s.layers.insert(word);
            }
            word.clear();
        } else {
            word += c;
        }
    }
    return s;
}

bool has(const Section& s, const char* layer) {
    return s.layers.count(layer) != 0;
}

const std::vector<Rhythm> march_r{{{0, 1.5}, {1.5, .5}, {2, 1}, {3, 1}, {4, 1.5}, {5.5, .5}, {6, 2}},
                                  {{0, .75}, {.75, .25}, {1, 1}, {2, .75}, {2.75, .25}, {3, 1}, {4, 2}, {6, 1}, {7, 1}},
                                  {{0, 1}, {1, .5}, {1.5, .5}, {2, 1}, {3, 1}, {4, .75}, {4.75, .25}, {5, 1}, {6, 2}}};
const std::vector<Rhythm> march_c{{{0, 1}, {1, 1}, {2, 2}, {4, 4}}, {{0, .75}, {.75, .25}, {1, 1}, {2, 2}, {4, 3.5}}};
const std::vector<Rhythm> west_r{{{0, 3}, {3, .5}, {3.5, .5}, {4, 4}},
                                 {{0, .5}, {.5, .5}, {1, 2.5}, {4, 1}, {5, 3}},
                                 {{0, 2}, {2, 1}, {3, 1}, {4, 4}}};
const std::vector<Rhythm> west_c{{{0, 1}, {1, 1}, {2, 2}, {4, 4}}};
const std::vector<Rhythm> swing_r{{{0, 1}, {1, .5}, {1.5, 1}, {2.5, .5}, {3, 1}, {4, 1.5}, {5.5, .5}, {6, 1.5}},
                                  {{0, .5}, {.5, .5}, {1, 1}, {2, .5}, {2.5, 1.5}, {4, .5}, {4.5, .5}, {5, .5}, {5.5, 1}, {6.5, 1}},
                                  {{0, 1.5}, {1.5, 1.5}, {3, 1}, {4, .5}, {4.5, .5}, {5, .5}, {5.5, 1.5}},
                                  {{.5, .5}, {1, .5}, {1.5, .5}, {2, 1}, {3, .5}, {3.5, 1.5}, {5, 1}, {6, 1.5}}};
const std::vector<Rhythm> swing_c{{{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 3}},
                                  {{0, .5}, {.5, .5}, {1, 1}, {2, 2}, {4, 3.2}},
                                  {{0, 1.5}, {1.5, .5}, {2, 2}, {4, 3}}};
const std::vector<Rhythm> toy_r{{{0, 1}, {1, .5}, {1.5, .5}, {2, 1}, {3, 1}, {4, .5}, {4.5, .5}, {5, .5}, {5.5, .5}, {6, 1.5}},
                                {{0, .5}, {.5, .5}, {1, .5}, {1.5, .5}, {2, 2}, {4, .5}, {4.5, .5}, {5, 1}, {6, 2}},
                                {{.5, .5}, {1, 1}, {2, .5}, {2.5, .5}, {3, 1}, {4, 1}, {5, 1}, {6, 1.5}}};
const std::vector<Rhythm> toy_c{{{0, .5}, {.5, .5}, {1, 1}, {2, 2}, {4, 3.5}}, {{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 3}}};
const std::vector<Rhythm> calm_r{{{0, 2}, {2, 1}, {3, 1}, {4, 3}},
                                 {{0, 1}, {1, 1}, {2, 2}, {4, 1.5}, {5.5, .5}, {6, 2}},
                                 {{1, 1}, {2, 1}, {3, 2}, {5, 3}}};
const std::vector<Rhythm> calm_c{{{0, 2}, {2, 2}, {4, 4}}, {{0, 1}, {1, 1}, {2, 2}, {4, 4}}};

const std::vector<DrumBar> snare_march{{{"snare", 0, .45, .1}, {"ghost", .25, .12, .1}, {"ghost", .5, .15, .1},
                                        {"snare", .75, .3, .1}, {"snare", 1, .4, .1}, {"snare", 2, .45, .1},
                                        {"ghost", 2.5, .15, .1}, {"snare", 2.75, .3, .1}, {"snare", 3, .4, .1},
                                        {"ghost", 3.5, .15, .1}, {"kick", 0, .35, 0}, {"kick", 2, .3, 0}}};

DrumBar snare_roll() {
    DrumBar roll{};
    for (const Hit& h : snare_march[0]) {
        if (h.t < 2) {
            roll.push_back(h);
        }
    }
    for (int k = 0; k < 16; ++k) {
        roll.push_back({"ghost", 2 + k * .125, .1 + .03 * k, .1});
    }
    return roll;
}

// ------------------------------------------------------------------ 1 meadow: Little Boots March
const std::vector<std::string> ma{"G", "D/F#", "Em", "C", "G", "Am7 D7", "G", "D7"};
const std::vector<std::string> mb{"C", "G/B", "Am", "D", "Em", "C", "Am7 D7", "G"};

Song meadow() {
    return Song{.id = "eggy_meadow",
                .bpm = 112,
                .bpb = 4,
                .swing = 0.0,
                .key = 7,
                .scale = major,
                .seed = 3101,
                .bass_inst = "tuba",
                .sections = {sec("intro", {"G", "D", "G", "D7"}, "snare tuba"), sec("A1", ma, "snare tuba pizz lead", "flute", 79, .9),
                             sec("B", mb, "snare tuba pizz lead glock", "brass", 74),
                             sec("C", {"Em", "C", "G", "D", "Em", "C", "Am7", "D7"}, "pizz box lead", "whistle", 79, .75),
                             sec("A2", ma, "snare tuba pizz lead glock", "flute", 81)},
                .bus = {{"lead", -17, .2}, {"glock", -24, .3}, {"pizz", -23, .15}, {"bass", -19, .04}, {"drums", -22, .12},
                        {"box", -24, .3}},
                .wet = .45,
                .ir = {1.6, 1.3, .6}};
}

Song title() {
    Song s = meadow();
    s.id = "eggy_title";
    s.bpm = 96;
    s.seed = 3102;
    s.sections = {sec("A1", ma, "box tuba lead", "whistle", 79, .8), sec("B", mb, "box tuba pizz lead", "flute", 79, .85),
                  sec("A2", ma, "box tuba pizz lead snare", "celesta", 84, .8)};
    return s;
}

void arr_meadow(Ctx& ctx) {
    const DrumBar roll = snare_roll();
    for (const auto& [s0, nb, spec] : ctx.secs) {
        const Section& sc = *spec;
        const double e = sc.e;
        if (has(sc, "lead")) {
            melody(ctx, s0, nb, sc.lead, sc.center, march_r, march_c, .7 * std::pow(e, .5), .9);
        }
        if (has(sc, "glock")) {
            counter(ctx, s0, nb, "celesta", sc.center + 5, .35, .35, "glock", 0);
        }
        if (has(sc, "snare")) {
            pdrums(ctx, s0, nb, snare_march, e * .9, &roll);
            for (int b = 0; b < nb; b += 8) {
                ctx.drum(s0 + b * 4, "triangle", .12, -.4);
            }
        }
        if (has(sc, "tuba")) {
            bass_line(ctx, s0, nb, "two", 31, .62 * e);
        }
        if (has(sc, "pizz")) {
            comp(ctx, s0, nb, "chunk", "pizz", 55, 69, .45 * e, -.4, "pizz");
        }
        if (has(sc, "box")) {
            arp(ctx, s0, nb, "musicbox", 72, 91, .3 * e, {0, 2, 4, 2, 3, 1, 4, 2}, .5, .2, "box", .4);
        }
    }
}

// ------------------------------------------------------------------ 2 forest: Mossy Mystery
const std::vector<std::string> fa{"Dm9", "Em7", "Fmaj7", "Em7", "Dm9", "Gm7", "Bbmaj7", "A7sus4 A7"};
const std::vector<std::string> fb{"Bbmaj7", "C", "Am7", "Dm", "Gm7", "C", "Fmaj7", "A7"};

Song forest() {
    return Song{.id = "eggy_forest",
                .bpm = 90,
                .bpb = 4,
                .swing = 0.3,
                .key = 2,
                .scale = dorian,
                .seed = 3201,
                .sections = {sec("intro", {fa.begin(), fa.begin() + 4}, "arp pad"), sec("A1", fa, "arp pad bass lead", "clarinet", 69, .85),
                             sec("A2", fa, "arp pad bass lead harp sneak", "clarinet", 72),
                             sec("B", fb, "harp pad bass lead sneak", "celesta", 81), sec("C", fa, "arp pad lead", "whistle", 76, .7),
                             sec("A3", fa, "arp pad bass lead harp sneak", "clarinet", 72)},
                .bus = {{"lead", -17, .3, std::pair{.75, .25}, .3}, {"arp", -23, .35}, {"harp", -24, .35}, {"pad", -27, .5},
                        {"bass", -21, .05}, {"drums", -28, .15}},
                .wet = .6,
                .ir = {2.2, 1.8, .8}};
}

void arr_forest(Ctx& ctx) {
    const std::vector<DrumBar> sneak{{{"ghost", 1, .16, .3}, {"ghost", 3, .16, .3}, {"wblo", 2.5, .1, -.3}, {"tap", 0, .12, 0}}};
    for (const auto& [s0, nb, spec] : ctx.secs) {
        const Section& sc = *spec;
        const double e = sc.e;
        if (has(sc, "lead")) {
            melody(ctx, s0, nb, sc.lead, sc.center, swing_r, swing_c, .62 * std::pow(e, .5), .92);
        }
        if (has(sc, "arp")) {
            arp(ctx, s0, nb, "celesta", 69, 86, .3 * e, {0, 2, 1, 3, 2, 4, 3, 1}, .5, .25, "arp", .5);
        }
        if (has(sc, "harp")) {
            arp(ctx, s0, nb, "harp", 50, 74, .35 * e, {0, 2, 4, 3, 1, 2}, 1.0, -.3, "harp");
        }
        if (has(sc, "pad")) {
            comp(ctx, s0, nb, "sustain", "pad", 52, 69, .4 * e, 0, "pad");
        }
        if (has(sc, "bass")) {
            bass_line(ctx, s0, nb, "walk", 31, .48 * e);
        }
        if (has(sc, "sneak")) {  // a tiny soldier tiptoeing: soft snare ghosts and woodblocks
            pdrums(ctx, s0, nb, sneak, e);
        }
    }
}

// ------------------------------------------------------------------ 3 pond: Brook Hoedown
const std::vector<std::string> pa{"G", "G", "C", "G", "G", "G", "D7", "G"};
const std::vector<std::string> pb{"C", "C", "G", "G", "D", "D7", "G", "G"};

Song pond() {
    return Song{.id = "eggy_pond",
                .bpm = 120,
                .bpb = 4,
                .swing = 0.5,
                .key = 7,
                .scale = major,
                .seed = 3301,
                .bass_inst = "tuba",
                .sections = {sec("intro", {"G", "C", "D7", "G"}, "banjo board"), sec("A1", pa, "banjo board jug lead", "reed", 74, .9),
                             sec("B", pb, "banjo board jug lead", "whistle", 79), sec("A2", pa, "banjo board jug lead", "reed", 76),
                             sec("C", {"Em", "C", "G", "D7", "Em", "C", "D7", "G"}, "banjo jug lead", "pluck", 76, .8),
                             sec("A3", pa, "banjo board jug lead", "reed", 76)},
                .bus = {{"lead", -17, .15}, {"banjo", -21, .12}, {"bass", -19, .03}, {"drums", -25, .08}},
                .wet = .4,
                .ir = {1.3, 1.1, .5}};
}

void arr_pond(Ctx& ctx) {
    DrumBar board{};
    for (int k = 0; k < 8; ++k) {
        board.push_back({"washboard", k / 2.0, k % 2 == 0 ? .28 : .16, .35});
    }
    board.push_back({"kick", 0, .22, 0});
    board.push_back({"kick", 2, .18, 0});
    const std::vector<DrumBar> boards{board};
    for (const auto& [s0, nb, spec] : ctx.secs) {
        const Section& sc = *spec;
        const double e = sc.e;
        if (has(sc, "lead")) {
            melody(ctx, s0, nb, sc.lead, sc.center, toy_r, toy_c, .68 * std::pow(e, .5), .85);
        }
        if (has(sc, "banjo")) {  // forward rolls
            arp(ctx, s0, nb, "banjo", 59, 79, .42 * e, {0, 2, 4, 0, 3, 4, 1, 4}, .5, -.35, "banjo", .4);
        }
        if (has(sc, "jug")) {
            bass_line(ctx, s0, nb, "two", 31, .62 * e);
        }
        if (has(sc, "board")) {
            pdrums(ctx, s0, nb, boards, e);
        }
    }
}

// ------------------------------------------------------------------ 4 ravine: Fistful of Pebbles
const std::vector<std::string> ra{"Am", "Am", "G", "Am", "F", "G", "Am", "E7"};
const std::vector<std::string> rb{"Dm", "Am", "G", "C", "F", "E7", "Am", "Am"};

Song ravine() {
    return Song{.id = "eggy_ravine",
                .bpm = 96,
                .bpb = 4,
                .swing = 0.0,
                .key = 9,
                .scale = aeolian,
                .seed = 3401,
                .sections = {sec("intro", {"Am", "Am", "G", "E7"}, "twang drone gallop"),
                             sec("A1", ra, "twang drone gallop lead", "whistle", 76, .9),
                             sec("A2", ra, "twang drone gallop lead anvil", "whistle", 79),
                             sec("B", rb, "twang gallop lead anvil", "brass", 72),
                             sec("C", {"Am", "G", "F", "E7", "Am", "G", "F", "E7"}, "twang drone lead", "twang", 69, .75),
                             sec("A3", ra, "twang drone gallop lead anvil", "whistle", 79)},
                .bus = {{"lead", -16, .4, std::pair{.75, .35}, .45}, {"twang", -22, .5}, {"pad", -28, .5}, {"drums", -24, .25},
                        {"bass", -21, .05}},
                .wet = .7,
                .ir = {2.8, 2.4, 1.0}};
}

void arr_ravine(Ctx& ctx) {
    const std::vector<DrumBar> gallop{{{"logkick", 0, .35, 0},  {"loghi", .5, .2, -.2},  {"loghi", .75, .18, -.2},
                                       {"logkick", 1, .3, 0},   {"loghi", 1.5, .2, -.2}, {"loghi", 1.75, .18, -.2},
                                       {"logkick", 2, .35, 0},  {"loghi", 2.5, .2, -.2}, {"loghi", 2.75, .18, -.2},
                                       {"logkick", 3, .3, 0},   {"loghi", 3.5, .2, -.2}, {"loghi", 3.75, .18, -.2},
                                       {"rim", 1, .12, .3},     {"rim", 3, .12, .3}}};
    for (const auto& [s0, nb, spec] : ctx.secs) {
        const Section& sc = *spec;
        const double e = sc.e;
        if (has(sc, "lead")) {
            melody(ctx, s0, nb, sc.lead, sc.center, west_r, west_c, .7 * std::pow(e, .5), .95);
        }
        if (has(sc, "twang")) {
            arp(ctx, s0, nb, "twang", 45, 64, .45 * e, {0, 4, 2, 4}, 1.0, .35, "twang", 1.2);
        }
        if (has(sc, "drone")) {
            comp(ctx, s0, nb, "sustain", "drone", 45, 57, .5 * e, 0, "pad");
        }
        if (has(sc, "gallop")) {
            pdrums(ctx, s0, nb, gallop, e);
        }
        if (has(sc, "anvil")) {
            for (int b = 0; b < nb; b += 2) {
                ctx.drum(s0 + b * 4, "anvil", .2, .4);
            }
        }
        bass_line(ctx, s0, nb, "two", 33, .5 * e);
    }
}

// ------------------------------------------------------------------ 5 highland: Dwarven Halls
const std::vector<std::string> ha{"Dm", "Dm", "C", "Dm", "Bb", "C", "Dm", "A7"};
const std::vector<std::string> hb{"Gm", "Dm", "Bb", "F", "Gm", "Dm", "Bb", "A7"};

Song highland() {
    return Song{.id = "eggy_highland",
                .bpm = 84,
                .bpb = 4,
                .swing = 0.0,
                .key = 2,
                .scale = dorian,
                .seed = 3501,
                .bass_inst = "tuba",
                .sections = {sec("intro", {ha.begin(), ha.begin() + 4}, "deep drums drone"),
                             sec("A1", ha, "deep drums drone lead", "brass", 62, .9),
                             sec("B", hb, "deep drums forge lead march", "reed", 69), sec("C", ha, "drone lead box", "flute", 74, .75),
                             sec("A2", ha, "deep drums forge drone lead march", "brass", 64)},
                .bus = {{"lead", -17, .35}, {"pad", -24, .45}, {"bass", -18, .06}, {"drums", -21, .3}, {"box", -26, .4}},
                .wet = .65,
                .ir = {3.0, 2.4, 1.0}};
}

void arr_highland(Ctx& ctx) {
    const std::vector<DrumBar> drums{{{"frame", 0, .5, 0}, {"logkick", 1.5, .25, 0}, {"frame", 2, .4, 0}, {"loghi", 3, .2, .2},
                                      {"loghi", 3.5, .2, .2}}};
    const std::vector<DrumBar> forge{{{"anvil", 1, .16, .4}, {"anvil", 3, .16, .4}}};
    const std::vector<OctaveStep> deep{{0, 0, .9}, {1.5, 0, .4}, {2, 7, .9}, {3, 0, .9}};
    for (const auto& [s0, nb, spec] : ctx.secs) {
        const Section& sc = *spec;
        const double e = sc.e;
        if (has(sc, "lead")) {
            melody(ctx, s0, nb, sc.lead, sc.center, march_r, march_c, .68 * std::pow(e, .5), .95);
        }
        if (has(sc, "deep")) {
            bass_oct(ctx, s0, nb, "tuba", 26, .6 * e, deep);
        }
        if (has(sc, "drums")) {
            pdrums(ctx, s0, nb, drums, e);
        }
        if (has(sc, "forge")) {
            pdrums(ctx, s0, nb, forge, e);
        }
        if (has(sc, "march")) {
            pdrums(ctx, s0, nb, snare_march, e * .55);
        }
        if (has(sc, "drone")) {
            comp(ctx, s0, nb, "sustain", "reed", 50, 62, .35 * e, 0, "pad");
        }
        if (has(sc, "box")) {
            arp(ctx, s0, nb, "musicbox", 74, 91, .25, {0, 2, 1, 3}, 1.0, .2, "box", .6);
        }
    }
}

// ------------------------------------------------------------------ 6 snow: Snow Bell Monastery
const std::vector<std::string> sa{"Dsus2", "Dsus2", "C", "Dsus2", "Bbmaj7", "C", "Dsus2", "Asus4"};

Song monastery() {
    return Song{.id = "eggy_monastery",
                .bpm = 66,
                .bpb = 4,
                .swing = 0.0,
                .key = 2,
                .scale = penta,
                .seed = 3601,
                .sections = {sec("A1", sa, "drone bowls heart", "", 74, .8), sec("A2", sa, "drone bowls heart bells lead", "whistle", 74, .85),
                             sec("B", {"Bbmaj7", "C", "Dsus2", "Dsus2", "Bbmaj7", "C", "Asus4", "Asus4"}, "drone bowls bells lead box",
                                 "flute", 79),
                             sec("A3", sa, "drone bowls heart lead", "whistle", 76, .85)},
                .bus = {{"lead", -18, .5, std::pair{1.5, .3}, .35}, {"pad", -22, .55}, {"bowls", -22, .6}, {"bells", -25, .6},
                        {"drums", -25, .4}, {"box", -27, .5}},
                .wet = .8,
                .ir = {3.6, 3.0, 1.2}};
}

void arr_monastery(Ctx& ctx) {
    const std::vector<DrumBar> heart{{{"frame", 0, .35, 0}, {"frame", .5, .2, 0}}, {{"frame", 0, .3, 0}}};
    for (const auto& [s0, nb, spec] : ctx.secs) {
        const Section& sc = *spec;
        const double e = sc.e;
        if (has(sc, "lead")) {
            melody(ctx, s0, nb, sc.lead, sc.center, calm_r, calm_c, .55 * e, 1.0);
        }
        if (has(sc, "drone")) {
            for (int b = 0; b < nb; b += 2) {
                ctx.note(s0 + b * 4, "drone", 38, 8.0, .5 * e, 0, "pad");
                ctx.note(s0 + b * 4, "drone", 45, 8.0, .35 * e, 0, "pad");
            }
        }
        if (has(sc, "bowls")) {
            scatter(ctx, s0, nb, "bowl", 55, 72, .9, .45, "bowls", &penta);
        }
        if (has(sc, "bells")) {
            scatter(ctx, s0, nb, "vibes", 79, 91, 1.2, .3, "bells", &penta);
        }
        if (has(sc, "heart")) {
            pdrums(ctx, s0, nb, heart, e);
        }
        if (has(sc, "box")) {
            arp(ctx, s0, nb, "musicbox", 76, 93, .22, {0, 2, 1, 3, 2, 4}, 1.0, .3, "box", .6);
        }
    }
}

// ------------------------------------------------------------------ 7 summit
const std::vector<std::string> summit_a{"D", "A/C#", "Bm", "G", "D", "Em7 A7", "D", "A7"};

Song summit() {
    return Song{.id = "eggy_summit",
                .bpm = 100,
                .bpb = 4,
                .swing = 0.0,
                .key = 2,
                .scale = major,
                .seed = 3701,
                .bass_inst = "tuba",
                .sections = {sec("A1", summit_a, "snare tuba lead bowls", "brass", 74, .9),
                             sec("B", {"G", "D/F#", "Em", "A", "Bm", "G", "Em7 A7", "D"}, "snare tuba lead glock bowls", "flute", 81),
                             sec("A2", summit_a, "tuba lead glock bowls", "celesta", 84, .85)},
                .bus = {{"lead", -17, .35}, {"glock", -24, .4}, {"bass", -20, .05}, {"drums", -23, .2}, {"bowls", -25, .6}},
                .wet = .6,
                .ir = {2.6, 2.2, 1.0}};
}

void arr_summit(Ctx& ctx) {
    const DrumBar roll = snare_roll();
    for (const auto& [s0, nb, spec] : ctx.secs) {
        const Section& sc = *spec;
        const double e = sc.e;
        if (has(sc, "lead")) {
            melody(ctx, s0, nb, sc.lead, sc.center, march_r, march_c, .68 * std::pow(e, .5), .9);
        }
        if (has(sc, "glock")) {
            counter(ctx, s0, nb, "celesta", sc.center + 5, .35, .35, "glock");
        }
        if (has(sc, "snare")) {
            pdrums(ctx, s0, nb, snare_march, e * .7, &roll);
        }
        if (has(sc, "tuba")) {
            bass_line(ctx, s0, nb, "two", 33, .58 * e);
        }
        if (has(sc, "bowls")) {
            scatter(ctx, s0, nb, "bowl", 62, 74, .5, .35, "bowls");
        }
    }
}

// ------------------------------------------------------------------ the songs and where the script began them
constexpr voice::Word128 synth_increment{0xc9c7353e6e2b1f28ull, 0x7d761f2d4027fae7ull};

constexpr voice::GeneratorState synth_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, synth_increment, false, 0u};
    return state;
}

struct Prime {
    const char* inst;
    double m;
    double dur;
    double vel;
    voice::GeneratorState state;
};

struct Track {
    const char* id;
    Song (*spec)();
    void (*arrange)(Ctx&);
    voice::GeneratorState start;
    std::vector<Prime> primes;
};

const std::vector<Track>& tracks() {
    static const std::vector<Track> list{
        {"eggy_title", title, arr_meadow, synth_at(0x1905e0335aae9634ull, 0x9199b0d09775add5ull), {}},
        {"eggy_meadow", meadow, arr_meadow, synth_at(0xeb606be069db797eull, 0x0a8a9bea4b4c5f6bull), {}},
        {"eggy_forest", forest, arr_forest, synth_at(0x7e22c5e5636cbb9bull, 0x186534dcf975c6cfull), {}},
        {"eggy_pond",
         pond,
         arr_pond,
         synth_at(0xcfe92148c0c4a271ull, 0x3956c7e9ea4ebf11ull),
         // whistle notes the brook reuses from earlier songs' cache
         {{"whistle", 83, 0.425, 0.7513052757842047, synth_at(0x277084d964126ce2ull, 0x601ab9d335bb8667ull)},
          {"whistle", 83, 0.425, 0.6272008572026121, synth_at(0x41d8e323eb4ba12full, 0x28fbbb1a7c7f2b88ull)},
          {"whistle", 86, 0.425, 0.7259216356602514, synth_at(0xfd7450e281c27e24ull, 0xbedec0a143974262ull)}}},
        {"eggy_ravine", ravine, arr_ravine, synth_at(0xd39afe3a0ae97dedull, 0x86ad6f9a0624a432ull), {}},
        {"eggy_highland", highland, arr_highland, synth_at(0xf38a76104046388full, 0x1d640e5919af2b57ull), {}},
        {"eggy_monastery", monastery, arr_monastery, synth_at(0x2f2b7eecef048f6full, 0xee7dcb52fc24a745ull), {}},
        {"eggy_summit", summit, arr_summit, synth_at(0x55d8618c1e2f214aull, 0xa9f80f77f2358db9ull), {}},
    };
    return list;
}

const Track& track(std::string_view id) {
    for (const Track& t : tracks()) {
        if (t.id == id) {
            return t;
        }
    }
    return tracks()[0];
}

double stereo_peak(const voice::Stereo& x) {
    return std::max(voice::peak(x.left), voice::peak(x.right));
}

// soft_limit(x, ceil): a gentle tanh knee above 70 % of the ceiling
void soft_limit(voice::Stereo& x, double ceil) {
    const double k = 0.7 * ceil;
    for (Signal* c : {&x.left, &x.right}) {
        for (double& value : *c) {
            const double a = std::fabs(value);
            if (a > k) {
                const double s = value > 0 ? 1.0 : (value < 0 ? -1.0 : 0.0);
                value = s * (k + (ceil - k) * std::tanh((a - k) / (ceil - k)));
            }
        }
    }
}

template <std::size_t I>
voice::Stereo song() {
    static constexpr std::array<const char*, 8> ids{"eggy_title",  "eggy_meadow",   "eggy_forest",    "eggy_pond",
                                                    "eggy_ravine", "eggy_highland", "eggy_monastery", "eggy_summit"};
    return render_track(ids[I]);
}

constexpr voice::Cue cues[] = {
    {.name = "eggy_title", .stereo = song<0>},     {.name = "eggy_meadow", .stereo = song<1>},
    {.name = "eggy_forest", .stereo = song<2>},    {.name = "eggy_pond", .stereo = song<3>},
    {.name = "eggy_ravine", .stereo = song<4>},    {.name = "eggy_highland", .stereo = song<5>},
    {.name = "eggy_monastery", .stereo = song<6>}, {.name = "eggy_summit", .stereo = song<7>},
};

}  // namespace

voice::Stereo render_master(std::string_view id) {
    const Track& t = track(id);
    const Song spec = t.spec();
    Ctx ctx(spec);
    t.arrange(ctx);
    EggySynth synth(t.start);
    for (const Prime& p : t.primes) {
        synth.prime_note(p.inst, p.m, p.dur, p.vel, p.state);
    }
    voice::Stereo out = mixdown(spec, ctx, synth);
    const double top = stereo_peak(out);
    for (Signal* c : {&out.left, &out.right}) {
        for (double& value : *c) {
            value = value / top * .5;
        }
    }
    return out;
}

double integrated_loudness(const voice::Stereo& x) {
    // ffmpeg's af_ebur128 on the 24-bit WAV write_wav made: each sample is
    // round(clip(x) * 8388607), decoded to 32 bits and converted to double.
    constexpr double pi = 3.14159265358979323846;  // M_PI
    const double rate = voice::sample_rate;
    double f0 = 1681.974450955533;
    const double gain_db = 3.999843853973347;
    double q = 0.7071752369554196;
    double k = std::tan(pi * f0 / rate);
    const double vh = std::pow(10.0, gain_db / 20.0);
    const double vb = std::pow(vh, 0.4996667741545416);
    const double a0 = 1.0 + k / q + k * k;
    const double pre_b0 = (vh + vb * k / q + k * k) / a0;
    const double pre_b1 = 2.0 * (k * k - vh) / a0;
    const double pre_b2 = (vh - vb * k / q + k * k) / a0;
    const double pre_a1 = 2.0 * (k * k - 1.0) / a0;
    const double pre_a2 = (1.0 - k / q + k * k) / a0;
    f0 = 38.13547087602444;
    q = 0.5003270373238773;
    k = std::tan(pi * f0 / rate);
    const double rlb_b0 = 1.0;
    const double rlb_b1 = -2.0;
    const double rlb_b2 = 1.0;
    const double rlb_a1 = 2.0 * (k * k - 1.0) / (1.0 + k / q + k * k);
    const double rlb_a2 = (1.0 - k / q + k * k) / (1.0 + k / q + k * k);

    constexpr int abs_thres = -70;
    constexpr int hist_grain = 100;
    constexpr int hist_size = (10 - abs_thres) * hist_grain + 1;
    const auto loudness = [](double energy) { return -0.691 + 10 * std::log10(energy); };
    const auto hist_pos = [](double l) { return static_cast<int>((l - abs_thres) * hist_grain); };
    std::vector<unsigned> counts(hist_size, 0);
    std::vector<double> energies(hist_size);
    for (int i = 0; i < hist_size; ++i) {
        const double l = i / static_cast<double>(hist_grain) + abs_thres;
        energies[static_cast<std::size_t>(i)] = std::pow(10.0, (l + 0.691) / 10.);
    }
    const std::size_t cache_size = static_cast<std::size_t>(rate) * 4 / 10;
    std::array<std::vector<double>, 2> cache{std::vector<double>(cache_size, 0.0), std::vector<double>(cache_size, 0.0)};
    std::array<double, 2> sums{0, 0};
    std::size_t cache_pos = 0;
    bool filled = false;
    std::array<std::array<double, 3>, 2> xs{};
    std::array<std::array<double, 3>, 2> ys{};
    std::array<std::array<double, 3>, 2> zs{};
    double sum_kept_powers = 0;
    long long nb_kept_powers = 0;
    double integrated = 0;
    int sample_count = 0;
    const std::size_t n = x.left.size();
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t bin_id = cache_pos;
        if (++cache_pos == cache_size) {
            filled = true;
            cache_pos = 0;
        }
        for (std::size_t ch = 0; ch < 2; ++ch) {
            const double v = std::clamp((ch == 0 ? x.left : x.right)[i], -1.0, 1.0);
            const double sample = std::nearbyint(v * 8388607) * 256 * (1.0 / 2147483648.0);
            auto& xc = xs[ch];
            auto& yc = ys[ch];
            auto& zc = zs[ch];
            xc[0] = sample;
            yc[2] = yc[1];
            yc[1] = yc[0];
            yc[0] = xc[0] * pre_b0 + xc[1] * pre_b1 + xc[2] * pre_b2 - yc[1] * pre_a1 - yc[2] * pre_a2;
            xc[2] = xc[1];
            xc[1] = xc[0];
            zc[2] = zc[1];
            zc[1] = zc[0];
            zc[0] = yc[0] * rlb_b0 + yc[1] * rlb_b1 + yc[2] * rlb_b2 - zc[1] * rlb_a1 - zc[2] * rlb_a2;
            const double bin = zc[0] * zc[0];
            sums[ch] = sums[ch] + bin - cache[ch][bin_id];
            cache[ch][bin_id] = bin;
        }
        if (++sample_count == static_cast<int>(rate) / 10) {
            sample_count = 0;
            double power = 1e-12;
            if (filled) {
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    power += 1.0 * sums[ch];
                }
                power /= static_cast<double>(cache_size);
            }
            const double l400 = loudness(power);
            if (l400 >= abs_thres) {
                counts[static_cast<std::size_t>(std::clamp(hist_pos(l400), 0, hist_size - 1))]++;
                sum_kept_powers += power;
                nb_kept_powers++;
                double relative = sum_kept_powers / static_cast<double>(nb_kept_powers);
                if (relative == 0) {
                    relative = 1e-12;
                }
                const double rel_threshold = loudness(relative) + -10;
                const int gate = std::clamp(hist_pos(rel_threshold), 0, hist_size - 1);
                double integrated_sum = 0.0;
                unsigned long long nb_integrated = 0;
                for (int h = gate; h < hist_size; ++h) {
                    const unsigned nb_v = counts[static_cast<std::size_t>(h)];
                    nb_integrated += nb_v;
                    integrated_sum += nb_v * energies[static_cast<std::size_t>(h)];
                }
                if (nb_integrated != 0) {
                    integrated = loudness(integrated_sum / static_cast<double>(nb_integrated));
                }
            }
        }
    }
    return integrated;
}

double printed_loudness(double integrated) {
    char text[32];
    std::snprintf(text, sizeof text, "%5.1f", integrated);
    return std::strtod(text, nullptr);
}

voice::Stereo render_track(std::string_view id) {
    voice::Stereo out = render_master(id);
    const double i = printed_loudness(integrated_loudness(out));
    const double gain = std::pow(10.0, (-19.0 - i) / 20);
    for (Signal* c : {&out.left, &out.right}) {
        for (double& value : *c) {
            value = value * gain;
        }
    }
    if (stereo_peak(out) > .5) {
        soft_limit(out, std::pow(10.0, -3.0 / 20));
    }
    return out;
}

std::span<const voice::Cue> music_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

void register_sound_cues() {
    voice::Bank::shared().add(effect_cues());
    voice::Bank::shared().add(music_cues());
}

}  // namespace eggy
