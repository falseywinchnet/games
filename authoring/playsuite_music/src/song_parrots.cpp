// The Paradoxiary: "Beak Expedition", a jungle adventure with a wink, and its two stingers.
// Original: written in the idiom of a 1990s comedy-adventure film score (a flute through
// the mist, broad horns over tribal drums, the hero's swaggering brass fanfare with a
// cheeky muted trumpet and a slide whistle); it quotes and paraphrases nothing.
#include "songs.hpp"

namespace ps {

namespace {
std::vector<ChordAt> join(std::vector<std::vector<ChordAt>> parts) {
    std::vector<ChordAt> out;
    for (auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}
Patch P(Inst i, double bright = 0.5) {
    Patch p;
    p.inst = i;
    p.bright = bright;
    return p;
}

// The expedition: horns over the drums, D minor, long notes
const char* kExpedition =
    "D4:q A4:h G4:e F4:e  A4:hd D5:q  E5:h D5:q C5:q  G4:w  "
    "F4:q Bb4:h A4:e G4:e  D5:hd F5:q  E5:q D5:q C5:q E5:q  C#5:w";
// the hero turns up: a swaggering fanfare in D major
const char* kHero =
    "A4:e D5:e F#5:q A5:h  G5:q E5:e C5:e G5:h  D5:q B4:e D5:e G5:q B5:q  A5:hd r:q  "
    "F5:q D5:e F5:e Bb5:h  C6:q G5:e C6:e E6:h  D6:w  r:h F#5:e A5:e D6:q";
// the flute's call from the mist
const char* kCallOfTheMist = "A5:h D6:q C6:e A5:e  G5:h F5:q E5:q  D5:w  r:w";
}  // namespace

Song songParrots() {
    Song s;
    s.id = "music_parrots_loop";
    s.title = "Beak Expedition (The Paradoxiary)";
    s.spb = spbFor(104);
    s.bars = 48;
    s.sections = {{"mist", 8}, {"expedition", 8}, {"build", 8}, {"hero", 8}, {"expedition2", 8}, {"into_the_mist", 8}};
    s.rev_size = 0.85;
    s.rev_rt60 = 2.4;
    s.rev_damp = 0.45;
    s.dly_beats = 1.5;
    s.dly_fb = 0.28;
    s.dly_lp = 4000;
    s.comp_ratio = 2.0;
    s.description = "A jungle adventure with a wink: a flute calling through the mist over a string drone and "
                    "bird calls, broad horns over tribal drums, a build, and the hero's swaggering brass "
                    "fanfare with a choir, a cheeky muted-trumpet echo and one slide-whistle wink.";
    const double M = 0, E = 32, B = 64, H = 96, E2 = 128, O = 160;
    const std::vector<ChordAt> hM = prog(M, "Dm:8 C:4 Dm:4 Bb:4 C:4 Dm:8");
    const std::vector<ChordAt> hE = prog(E, "Dm Dm C C Bb Bb C A");
    const std::vector<ChordAt> hB = prog(B, "Bb C Dm C Bb C A A7");
    const std::vector<ChordAt> hH = prog(H, "D C G D Bb C D:8");
    const std::vector<ChordAt> hE2 = prog(E2, "Dm Dm C C Bb Bb C A");
    const std::vector<ChordAt> hO = prog(O, "Dm C Bb A Dm C Bb A7");
    s.chords = join({hM, hE, hB, hH, hE2, hO});

    // the flute (a pan pipe in the mist): the call, and the expedition the second time
    Patch flP = P(Inst::Flute, 0.45);
    flP.vib = 1.4;
    Track& fl = s.add("flute", flP);
    fl.gain_db = -2;
    fl.pan = -0.2;
    fl.rev = 0.4;
    fl.dly = 0.25;
    fl.human_ms = 6;
    expectBeat(fl.mel(M, kCallOfTheMist, 0, 0.7, 0.95), M + 16, "parrots call 1");
    fl.mel(M + 16, "F5:h G5:q F5:q  G5:h E5:q C5:q  D5:w  r:w", 0, 0.65, 0.95);
    expectBeat(fl.mel(E2, kExpedition, 12, 0.75, 0.95), O, "parrots expedition 2");
    fl.mel(O, "A5:h D6:q C6:e A5:e  G5:h E5:q C5:q  D5:h F5:q Bb5:q  A5:w", 0, 0.6, 0.95);

    // the horns: the expedition, and long notes through the build
    Patch hnP = P(Inst::Brass, 0.38);
    hnP.attack = 0.08;
    hnP.width = 0.7;
    Track& hn = s.add("horns", hnP);
    hn.gain_db = -1;
    hn.rev = 0.35;
    hn.human_ms = 5;
    expectBeat(hn.mel(E, kExpedition, 0, 0.8, 0.97), B, "parrots expedition");
    padChords(hn, hB, 53, 67, 3, 0.65, 0.95);
    padChords(hn, hE2, 50, 62, 2, 0.45, 0.95);

    // the hero: the fanfare in the trumpets, a choir behind, a muted trumpet answering with a wink
    Patch trP = P(Inst::Brass, 0.72);
    trP.width = 0.6;
    Track& tr = s.add("fanfare", trP);
    tr.gain_db = 0;
    tr.rev = 0.3;
    tr.human_ms = 3;
    expectBeat(blockHarmony(tr, hH, H, kHero, 3, 0, 0.9, 0.92, 55), E2, "parrots hero");
    Patch mtP = P(Inst::MutedTrumpet, 0.6);
    Track& mt = s.add("muted_trumpet", mtP);
    mt.gain_db = -4;
    mt.pan = 0.35;
    mt.rev = 0.3;
    mt.mel(H + 12, "r:h D6.:e D6.:e r:q", 0, 0.75);   // a cheeky echo after the first phrase
    mt.mel(H + 20, "r:h A5.:e F5.:e r:q", 0, 0.7);
    Patch whP = P(Inst::Whistle, 0.6);
    whP.glide = 0.14;
    Track& wh = s.add("slide_whistle", whP);
    wh.gain_db = -5;
    wh.pan = 0.3;
    wh.rev = 0.3;
    wh.mel(H + 28, "r:h A5:e ~D6:e ~A6:q", 0, 0.7, 1.0);   // the one wink
    Patch chP = P(Inst::Choir, 0.5);
    chP.param = 0.8;
    chP.attack = 0.8;
    chP.release = 1.6;
    Track& ch = s.add("choir", chP);
    ch.gain_db = -8;
    ch.rev = 0.5;
    ch.hp = 160;
    padChords(ch, hH, 57, 74, 3, 0.6);
    padChords(ch, prog(B + 24, "A:8"), 57, 73, 3, 0.45);

    // strings: a low drone in the mist, rising through the build, under everything after
    Patch stP = P(Inst::Strings, 0.42);
    stP.attack = 0.9;
    stP.release = 1.6;
    stP.width = 0.9;
    Track& st = s.add("strings", stP);
    st.gain_db = -8;
    st.rev = 0.45;
    st.hp = 70;
    padChords(st, hM, 45, 62, 3, 0.45);
    padChords(st, hE, 48, 64, 3, 0.5);
    padChords(st, hB, 52, 72, 4, 0.6);
    padChords(st, hH, 50, 69, 4, 0.55);
    padChords(st, join({hE2, hO}), 45, 62, 3, 0.45);

    // marimba: a slow, open ostinato under the expedition
    Track& ma = s.add("marimba", P(Inst::Marimba, 0.45));
    ma.gain_db = -9;
    ma.pan = -0.35;
    ma.rev = 0.3;
    ma.dly = 0.15;
    ma.human_ms = 4;
    arpChords(ma, hE, 50, 69, "0.2.1.2.", 0.5, 3, 0.55, 0.8);
    arpChords(ma, join({hE2, hO}), 50, 69, "0.2.1.2.", 0.5, 3, 0.5, 0.8);

    // the bass: long, low, pulsing roots
    Track& bass = s.add("bass", P(Inst::UprightBass, 0.35));
    bass.gain_db = -2;
    bass.rev = 0.1;
    bassLine(bass, hM, 38, "R-------", 0.5, 0.6, 1.0);
    bassLine(bass, join({hE, hB, hE2, hO}), 38, "R..R....", 0.5, 0.75, 0.9);
    bassLine(bass, hH, 38, "R..R.5..", 0.5, 0.85, 0.8);

    // timpani: the build's roll and the hero's arrival
    Track& ti = s.add("timpani", P(Inst::Timpani, 0.5));
    ti.gain_db = -4;
    ti.rev = 0.35;
    for (int k = 0; k < 16; ++k) ti.n(B + 24 + k * .5, .5, 45, 0.45 + k * .03);
    ti.mel(H, "D3!:q r:hd", 0, 0.95);
    ti.mel(H + 16, "Bb2!:q r:hd", 0, 0.85);

    // the jungle: big low drums (heartbeat in the mist, a tribal march after), congas, a
    // shaker only where the hero walks, the forest's own birds
    Track& drum = s.add("big_drum", P(Inst::Tom, 0.25));
    drum.gain_db = -3;
    drum.rev = 0.3;
    Track& tom = s.add("toms", P(Inst::Tom, 0.5));
    tom.gain_db = -7;
    tom.pan = 0.2;
    tom.rev = 0.25;
    Track& conga = s.add("congas", P(Inst::Tom, 0.75));
    conga.gain_db = -11;
    conga.pan = -0.4;
    conga.rev = 0.2;
    Track& shaker = s.add("shaker", P(Inst::Shaker, 0.45));
    shaker.gain_db = -14;
    shaker.pan = 0.25;
    Track& crash = s.add("crash", P(Inst::Crash, 0.45));
    crash.gain_db = -9;
    crash.rev = 0.35;
    Track& birds = s.add("birds", P(Inst::Bird, 0.5));
    birds.gain_db = -8;
    birds.rev = 0.6;
    birds.dly = 0.3;
    for (int bar = 0; bar < s.bars; ++bar) {
        const double b = bar * 4.0;
        const bool mist = b < E || b >= O + 16, hero = b >= H && b < E2, build = b >= B && b < H;
        if (mist) drum.pat(b, 1, "x.....x.........", 36, 0.25, 0.7);
        else drum.pat(b, 1, hero ? "x.....x.x...x..." : "x.....x...x.....", 36, 0.25, 0.85);
        if (!mist) tom.pat(b, 1, hero ? "....x..x....x.x." : build && bar % 2 ? "....x...x...x.xx" : "........x.......", 48, 0.25, 0.6);
        if (!mist && !build) conga.pat(b, 1, "..x...x...x..x..", 58, 0.25, 0.5);
        if (hero) shaker.pat(b, 1, "x.x.x.x.x.x.x.x.", 60, 0.25, 0.45);
        if (b == E || b == H) crash.n(b, 3, 60, 0.75);
        // a bird or two calling, now here, now there
        if (bar % 3 != 1) {
            const double at = b + ((bar * 7) % 5) * .5 + .25;
            birds.n(at, 0.6, 86 + (bar * 5) % 10, 0.5, ((bar * 13) % 7 - 3) / 4.0);
            if (bar % 4 == 0) birds.n(at + 1.5, 0.4, 90 + (bar * 3) % 7, 0.4, ((bar * 11) % 7 - 3) / 4.0);
        }
    }
    return s;
}

// The case closed: a brassy "ta-da" with the whistle whooping up and a cowbell.
Song stingerParrotsWin() {
    Song s;
    s.id = "stinger_win_parrots";
    s.title = "Paradoxiary win";
    s.spb = spbFor(138);
    s.bars = 2;
    s.loop = false;
    s.tail = 3.0;
    s.lufs = -18.0;
    s.comp_ratio = 1.5;
    s.max_seconds = 4.9;
    s.rev_rt60 = 1.5;
    Track& br = s.add("brass", P(Inst::Brass, 0.7));
    br.rev = 0.25;
    br.mel(0, "[F4,Bb4,D5]!:e [F4,Bb4,D5]!:e r:e [F4,C5,Eb5]!:e r:e [G4,C5,E5]:e [A4,C5,F5]!:e r:e  [Bb4,D5,F5,Bb5]!:w", 0, 0.9, 0.9);
    Track& tp = s.add("trumpet", P(Inst::MutedTrumpet, 0.6));
    tp.rev = 0.2;
    tp.pan = 0.15;
    tp.mel(4, "D6:q F6:q Bb6!:h", 0, 0.85);
    Patch whP = P(Inst::Whistle, 0.6);
    whP.glide = 0.15;
    Track& wh = s.add("whistle", whP);
    wh.gain_db = -3;
    wh.pan = 0.3;
    wh.mel(2, "Bb5:e ~F6:e ~Bb6:q", 0, 0.8, 1.0);
    Track& ti = s.add("timpani", P(Inst::Timpani, 0.5));
    ti.gain_db = -3;
    ti.mel(4, "Bb2!:q", 0, 0.9);
    Track& bell = s.add("cowbell", P(Inst::Cowbell, 0.6));
    bell.gain_db = -6;
    bell.pat(0, 1, "x.x.x.x.x.x.x.x.", 60, 0.25, 0.6);
    Track& crash = s.add("crash", P(Inst::Crash, 0.5));
    crash.gain_db = -6;
    crash.n(4, 2, 60, 0.8);
    Track& bass = s.add("bass", P(Inst::FunkBass, 0.35));
    bass.mel(4, "Bb1!:h", 0, 0.9);
    return s;
}

// The wrong bird: the sad muted trumpet, wah-wah-wah-waaah, and the whistle sliding off.
Song stingerParrotsLose() {
    Song s;
    s.id = "stinger_lose_parrots";
    s.title = "Paradoxiary wrong bird";
    s.spb = spbFor(100);
    s.bars = 2;
    s.loop = false;
    s.tail = 3.0;
    s.lufs = -18.0;
    s.comp_ratio = 1.5;
    s.max_seconds = 4.9;
    s.rev_rt60 = 1.5;
    Patch tpP = P(Inst::MutedTrumpet, 0.55);
    tpP.vib = 2.5;
    tpP.glide = 0.08;
    Track& tp = s.add("trumpet", tpP);
    tp.rev = 0.2;
    tp.mel(0, "D5:q C#5:q C5:q B4:q ~A#4:h", 0, 0.85, 0.95);
    Patch whP = P(Inst::Whistle, 0.6);
    whP.glide = 0.3;
    Track& wh = s.add("whistle", whP);
    wh.gain_db = -5;
    wh.pan = 0.3;
    wh.mel(4, "F6:e ~C6:e ~F5:q", 0, 0.7, 1.0);
    Track& bass = s.add("bass", P(Inst::FunkBass, 0.3));
    bass.gain_db = -2;
    bass.mel(4, "A#1:h", 0, 0.8);
    Track& wb = s.add("woodblock", P(Inst::WoodBlock, 0.6));
    wb.gain_db = -6;
    wb.n(5, 0.25, 72, 0.6);
    return s;
}

}  // namespace ps
