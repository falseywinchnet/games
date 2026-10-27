// Puzzle-game music: Sudoku day/night, Gems, Nature Cube, Untangle, Puzzle Solve.
#include "dsp.hpp"
#include "songs.hpp"
#include <algorithm>
#include <cmath>

namespace ps {

namespace {
std::vector<ChordAt> cat(std::vector<std::vector<ChordAt>> parts) {
    std::vector<ChordAt> r;
    for (const std::vector<ChordAt>& p : parts)
        r.insert(r.end(), p.begin(), p.end());
    return r;
}

// Shared Sudoku harmonic plan (written in D; night transposes it).
struct SudokuPlan {
    std::vector<ChordAt> a1, b, a2, c;
    std::string ma, mb, mc, ma2;
};
SudokuPlan sudokuPlan(int transpose) {
    SudokuPlan p;
    p.a1 = prog(0, "Dmaj7 Bm7 Gmaj7 Asus4:2 A:2 Dmaj7/F# Em7 Gmaj7 Asus4:2 A:2", transpose);
    p.b = prog(32, "Bm7 F#m7 Gmaj7 Dmaj7/F# Em7 Bm7 Gmaj7 Asus4", transpose);
    p.a2 = prog(64, "Dmaj7 Bm7 Gmaj7 Asus4:2 A:2 Dmaj7/F# Em7 Gmaj7 Asus4:2 A:2", transpose);
    p.c = prog(96, "Gmaj7 A/G F#m7 Bm7 Em9 A13 Dmaj9 Asus4:2 A:2", transpose);
    p.ma = "r:q A4:q D5:q E5:q  F#5:hd D5:q  E5:qd D5:e B4:h  D5:h C#5:h  "
           "r:q F#5:q A5:q B5:q  G5:hd F#5:e E5:e  D5:qd B4:e A4:h  E5:h C#5:h";
    p.mb = "D5:qd C#5:e B4:h  C#5:q E5:q A5:h  B5:qd A5:e F#5:q D5:q  E5:hd r:q  "
           "G5:qd F#5:e E5:q B4:q  D5:hd F#5:q  A5:qd G5:e F#5:q D5:q  E5:w";
    p.mc = "B5:qd A5:e F#5:h  E5:qd F#5:e A5:h  C#6:qd B5:e A5:q E5:q  F#5:hd r:q  "
           "G5:q F#5:q E5:q B5:q  A5:qd G5:e F#5:h  E5:hd C#5:q  D5:h C#5:h";
    // the return varies the first phrase (octave lift, answering turns)
    p.ma2 = "r:q A5:q D6:q E6:q  F#6:h E6:q D6:q  E6:qd D6:e B5:h  D6:h C#6:h  "
            "r:q F#5:q A5:q B5:q  G5:h A5:q B5:q  D6:qd B5:e A5:h  E5:h C#5:h";
    return p;
}
} // namespace

// ---------------------------------------------------------------------------------------------
Song songSudokuDay() {
    Song s;
    s.id = "music_sudoku_day_loop";
    s.title = "Morning Grid (Sudoku Day)";
    s.spb = spbFor(76);
    s.bars = 32;
    s.sections = {{"A1", 8}, {"B", 8}, {"A2", 8}, {"C", 8}};
    s.rev_size = 0.85;
    s.rev_rt60 = 2.4;
    s.rev_damp = 0.45;
    s.rev_predelay = 0.03;
    s.dly_beats = 1.5;
    s.dly_fb = 0.25;
    s.dly_lp = 4000;
    s.comp_ratio = 1.4;
    s.description =
        "Calm, unobtrusive morning focus: soft felt piano, celesta answers, warm pad; no drums.";
    SudokuPlan p = sudokuPlan(0);
    std::vector<ChordAt> all = cat({p.a1, p.b, p.a2, p.c});
    s.chords = all;

    Patch pnP;
    pnP.inst = Inst::Piano;
    pnP.bright = 0.28;
    Track& mel = s.add("piano_melody", pnP);
    mel.gain_db = 0;
    mel.rev = 0.28;
    mel.dly = 0.05;
    mel.human_ms = 9;
    mel.human_vel = 0.06;
    double e = mel.mel(0, p.ma, 0, 0.6, 1.0);
    expectBeat(e, 32, "sudoku day A1");
    e = mel.mel(32, p.mb, 0, 0.62, 1.0);
    expectBeat(e, 64, "sudoku day B");
    e = mel.mel(64, p.ma2, 0, 0.6, 1.0);
    expectBeat(e, 96, "sudoku day A2");
    e = mel.mel(96, p.mc, 0, 0.62, 1.0);
    expectBeat(e, 128, "sudoku day C");

    Track& arp = s.add("piano_broken_chords", pnP);
    arp.gain_db = -5;
    arp.pan = -0.12;
    arp.rev = 0.28;
    arp.human_ms = 9;
    arp.human_vel = 0.08;
    arpChords(arp, all, 50, 66, "0.12.3.2", 0.5, 4, 0.42, 2.0);
    Track& lh = s.add("piano_bass", pnP);
    lh.gain_db = -5;
    lh.rev = 0.25;
    lh.human_ms = 8;
    bassLine(lh, all, 38, "R-------", 0.5, 0.45, 1.0);

    Patch ceP;
    ceP.inst = Inst::Celesta;
    ceP.bright = 0.4;
    Track& ce = s.add("celesta", ceP);
    ce.gain_db = 0;
    ce.pan = 0.35;
    ce.rev = 0.35;
    ce.dly = 0.15;
    ce.human_ms = 6;
    // celesta answers at phrase ends (B and C), and a sparkle line under the A2 lift
    ce.mel(32 + 6, "A5:q C#6:q", 0, 0.5);
    ce.mel(32 + 14, "F#6:e E6:e D6:e A5:e", 0, 0.5);
    ce.mel(32 + 22, "B5:q F#6:q", 0, 0.5);
    ce.mel(32 + 30, "A5:e B5:e C#6:e E6:e", 0, 0.5);
    ce.mel(96 + 6, "C#6:q A5:q", 0, 0.5);
    ce.mel(96 + 14, "B5:e C#6:e D6:e F#6:e", 0, 0.5);
    ce.mel(96 + 22, "C#6:q E6:q", 0, 0.5);
    ce.mel(96 + 30, "E6:e D6:e C#6:e A5:e", 0, 0.5);
    ce.mel(64, "r:w r:h r:q A6:q  r:w r:h r:q E6:q  r:w", 0, 0.38);

    Patch padP;
    padP.inst = Inst::Pad;
    padP.bright = 0.3;
    padP.attack = 1.0;
    padP.release = 1.5;
    Track& pad = s.add("pad", padP);
    pad.gain_db = -17;
    pad.rev = 0.4;
    pad.hp = 200;
    pad.lp = 6000;
    padChords(pad, all, 57, 74, 4, 0.5);
    Patch subP;
    subP.inst = Inst::SubBass;
    subP.attack = 0.08;
    subP.release = 0.4;
    Track& sub = s.add("sub", subP);
    sub.gain_db = -9;
    sub.human_ms = 0;
    bassLine(sub, cat({p.b, p.a2, p.c}), 38, "R-------", 0.5, 0.5, 1.0);
    return s;
}

// Night: the same plan, a whole step lower and slower, on electric piano and music box.
Song songSudokuNight() {
    Song s;
    s.id = "music_sudoku_night_loop";
    s.title = "Midnight Grid (Sudoku Night)";
    s.spb = spbFor(68);
    s.bars = 32;
    s.sections = {{"A1", 8}, {"B", 8}, {"A2", 8}, {"C", 8}};
    s.rev_size = 0.95;
    s.rev_rt60 = 2.8;
    s.rev_damp = 0.6;
    s.rev_predelay = 0.035;
    s.dly_beats = 1.5;
    s.dly_fb = 0.3;
    s.dly_lp = 3000;
    s.comp_ratio = 1.4;
    s.description = "Nocturnal twin of Morning Grid: tremolo electric piano, music-box glints, "
                    "dark pad, soft brush swells.";
    SudokuPlan p = sudokuPlan(-2);
    std::vector<ChordAt> all = cat({p.a1, p.b, p.a2, p.c});
    s.chords = all;

    Patch epP;
    epP.inst = Inst::EP;
    epP.bright = 0.3;
    epP.param = 0.35;
    Track& mel = s.add("ep_melody", epP);
    mel.gain_db = 0;
    mel.rev = 0.3;
    mel.dly = 0.08;
    mel.human_ms = 10;
    mel.human_vel = 0.06;
    double e = mel.mel(0, p.ma, -2, 0.62, 1.0);
    expectBeat(e, 32, "sudoku night A1");
    e = mel.mel(32, p.mb, -2, 0.64, 1.0);
    expectBeat(e, 64, "sudoku night B");
    e = mel.mel(96, p.mc, -2, 0.64, 1.0);
    expectBeat(e, 128, "sudoku night C");

    Patch mbP;
    mbP.inst = Inst::MusicBox;
    mbP.bright = 0.4;
    Track& mb = s.add("music_box", mbP);
    mb.gain_db = 3;
    mb.pan = 0.25;
    mb.rev = 0.4;
    mb.dly = 0.18;
    mb.human_ms = 6;
    e = mb.mel(64, p.ma, -2, 0.5, 1.0);
    expectBeat(e, 96, "sudoku night A2");
    mb.mel(32 + 6, "G5:q B5:q", 0, 0.42);
    mb.mel(32 + 22, "A5:q E6:q", 0, 0.42);
    mb.mel(96 + 14, "A5:e B5:e C6:e E6:e", 0, 0.4);
    mb.mel(96 + 30, "D6:e C6:e B5:e G5:e", 0, 0.4);

    Track& comp = s.add("ep_chords", epP);
    comp.gain_db = -6;
    comp.pan = -0.2;
    comp.rev = 0.3;
    comp.human_ms = 10;
    comp.human_vel = 0.08;
    compChords(comp, all, 50, 64, "x.......x..x....", 4, 0.42, 1.6, 0.25, true);

    Patch padP;
    padP.inst = Inst::Pad;
    padP.bright = 0.18;
    padP.attack = 1.4;
    padP.release = 2.0;
    padP.detune = 0.8;
    Track& pad = s.add("pad", padP);
    pad.gain_db = -15;
    pad.rev = 0.45;
    pad.hp = 160;
    pad.lp = 3500;
    padChords(pad, all, 55, 72, 4, 0.5);
    Patch subP;
    subP.inst = Inst::SubBass;
    subP.attack = 0.1;
    subP.release = 0.5;
    Track& sub = s.add("sub", subP);
    sub.gain_db = -7;
    sub.human_ms = 0;
    bassLine(sub, all, 36, "R-------", 0.5, 0.5, 1.0);
    Patch swP;
    swP.inst = Inst::BrushSweep;
    Track& sweep = s.add("brush_swell", swP);
    sweep.gain_db = -6;
    sweep.rev = 0.3;
    sweep.human_ms = 0;
    for (int bar = 8; bar < 32; bar += 2)
        sweep.n(bar * 4.0, 4.0, 60, 0.45);
    return s;
}

// ---------------------------------------------------------------------------------------------
// Gems: "Prism Pop" - bright match-3 arcade pop in A, 124 BPM. Mids are kept open for effects:
// harmony lives in high plucks/bells and the low bass; the pad is high-passed.
Song songGems() {
    Song s;
    s.id = "music_gems_loop";
    s.title = "Prism Pop (Gems)";
    s.spb = spbFor(124);
    s.bars = 48;
    s.sections = {{"intro", 4}, {"A1", 8},        {"B_build", 8}, {"C_chorus", 8},
                  {"A2", 8},    {"C2_chorus", 8}, {"outro", 4}};
    s.rev_size = 0.7;
    s.rev_rt60 = 1.6;
    s.rev_damp = 0.35;
    s.dly_beats = 0.75;
    s.dly_fb = 0.33;
    s.dly_lp = 7000;
    s.comp_ratio = 2.0;
    s.description = "Sparkling arpeggiated pop: glassy bell hook in the chorus, glock calls, "
                    "pumping bass; low-mids left clear.";
    const double I = 0, A1 = 16, B = 48, C = 80, A2 = 112, C2 = 144, O = 176;
    std::vector<ChordAt> hI = prog(I, "A E/G# F#m D");
    std::vector<ChordAt> h1 = prog(A1, "A E/G# F#m D A E/G# D Esus4:2 E:2");
    std::vector<ChordAt> hB = prog(B, "Bm7 C#m7 D E Bm7 C#m7 Dmaj7 Esus4:2 E:2");
    std::vector<ChordAt> hC = prog(C, "D E C#m7 F#m Bm7 E Dmaj7 Esus4:2 E:2");
    std::vector<ChordAt> h2 = prog(A2, "A E/G# F#m D A E/G# D Esus4:2 E:2");
    std::vector<ChordAt> hC2 = prog(C2, "D E C#m7 F#m Bm7 E Dmaj7 Esus4:2 E:2");
    std::vector<ChordAt> hO = prog(O, "D E F#m E");
    std::vector<ChordAt> all = cat({hI, h1, hB, hC, h2, hC2, hO});
    s.chords = all;

    const std::string hook =
        "A5:qd F#6:qd E6:q  D6:e C#6:e B5:e C#6:e+h  G#5:qd E6:qd C#6:q  B5:e A5:e G#5:e A5:e+h  "
        "F#5:e A5:e D6:e F#6:e E6:e D6:e B5:e D6:e  E6:qd D6:e C#6:q B5:q  C#6:e B5:e A5:e F#5:e "
        "A5:q C#6:q  B5:qd A5:e G#5:h";
    const std::string verse =
        "E6:e C#6:e A5:q r:h  B5:e G#5:e E5:q r:h  A5:e C#6:e F#6:q E6:q C#6:q  D6:hd r:q  "
        "E6:e C#6:e A5:q r:h  B5:e G#5:e E5:q G#5:e B5:e E6:q  F#6:qd E6:e D6:q A5:q  B5:h G#5:h";

    Patch bellP;
    bellP.inst = Inst::BellLead;
    bellP.bright = 0.6;
    Track& bell = s.add("bell_lead", bellP);
    bell.gain_db = 0;
    bell.rev = 0.2;
    bell.dly = 0.18;
    bell.human_ms = 2;
    bell.hp = 300;
    double e = bell.mel(C, hook, 0, 0.85, 0.9);
    expectBeat(e, A2, "gems C");
    e = bell.mel(C2, hook, 0, 0.9, 0.9);
    expectBeat(e, O, "gems C2");
    e = bell.mel(B, "F#5:w G#5:w A5:w B5:w D6:w E6:w F#6:w E6:h G#6:h", 0, 0.6, 1.0);
    expectBeat(e, C, "gems B");

    Patch glP;
    glP.inst = Inst::Glock;
    glP.bright = 0.6;
    Track& gl = s.add("glock", glP);
    gl.gain_db = -3;
    gl.pan = 0.3;
    gl.rev = 0.22;
    gl.dly = 0.2;
    gl.human_ms = 2;
    e = gl.mel(A1, verse, 0, 0.75, 0.9);
    expectBeat(e, B, "gems A1");
    e = gl.mel(A2, verse, 0, 0.8, 0.9);
    expectBeat(e, C2, "gems A2");
    gl.mel(I, "E6:e C#6:e A5:q r:h  B5:e G#5:e E5:q r:h  A5:e C#6:e F#6:q E6:q C#6:q  D6:hd r:q", 0,
           0.55, 0.9);
    gl.mel(C2, hook, 12, 0.35, 0.6);
    gl.mel(O, "A5:qd F#6:qd E6:q  D6:e C#6:e B5:e C#6:e+h  F#6:qd E6:qd C#6:q  B5:hd r:q", 0, 0.6,
           0.9);

    Patch plP;
    plP.inst = Inst::PluckSynth;
    plP.bright = 0.55;
    plP.width = 0.7;
    plP.decay = 0.9;
    Track& arp = s.add("pluck_arp", plP);
    arp.gain_db = -5;
    arp.pan = -0.2;
    arp.rev = 0.15;
    arp.dly = 0.22;
    arp.hp = 350;
    arp.human_ms = 1;
    arpChords(arp, cat({hI, h1}), 64, 79, "0123 1230 2301 3210", 0.25, 4, 0.55, 0.8);
    arpChords(arp, hB, 64, 79, "0123 4321 0123 4321", 0.25, 4, 0.6, 0.8);
    arpChords(arp, hC, 64, 79, "0.12 .3.2 0.12 .3.4", 0.25, 4, 0.6, 0.8);
    arpChords(arp, h2, 64, 79, "0123 1230 2301 3210", 0.25, 4, 0.6, 0.8);
    arpChords(arp, cat({hC2, hO}), 64, 79, "0.12 .3.2 0.12 .3.4", 0.25, 4, 0.62, 0.8);

    Patch padP;
    padP.inst = Inst::Pad;
    padP.bright = 0.55;
    padP.attack = 0.4;
    padP.release = 0.6;
    padP.width = 0.9;
    Track& pad = s.add("super_pad", padP);
    pad.gain_db = -10;
    pad.rev = 0.3;
    pad.hp = 450;
    pad.duck = 0.55;
    padChords(pad, cat({hB, hC}), 64, 81, 4, 0.55);
    padChords(pad, hC2, 64, 81, 4, 0.6);

    Patch bsP;
    bsP.inst = Inst::SynthBass;
    bsP.bright = 0.5;
    Track& bass = s.add("bass", bsP);
    bass.gain_db = -1;
    bass.rev = 0.0;
    bass.duck = 0.35;
    bass.human_ms = 1;
    bassLine(bass, hI, 33, "R-------", 0.5, 0.8, 0.9);
    bassLine(bass, h1, 33, "R.R.R.RO", 0.5, 0.8, 0.6);
    bassLine(bass, hB, 33, "RRRRRRRR", 0.5, 0.75, 0.55);
    bassLine(bass, hC, 33, "R.RO.RO.", 0.5, 0.85, 0.6);
    bassLine(bass, h2, 33, "R.R.R.RO", 0.5, 0.82, 0.6);
    bassLine(bass, hC2, 33, "R.RO.RO.", 0.5, 0.88, 0.6);
    bassLine(bass, hO, 33, "R---R-O-", 0.5, 0.8, 0.8);

    Patch kP;
    kP.inst = Inst::Kick;
    kP.bright = 0.55;
    kP.decay = 0.8;
    Track& kick = s.add("kick", kP);
    kick.gain_db = -9;
    kick.duck_source = true;
    kick.human_ms = 0;
    Patch cP;
    cP.inst = Inst::Clap;
    Track& clap = s.add("clap", cP);
    clap.gain_db = -1;
    clap.rev = 0.2;
    clap.human_ms = 1;
    Patch hcP;
    hcP.inst = Inst::HatC;
    hcP.bright = 0.7;
    Track& hat = s.add("hat", hcP);
    hat.gain_db = 1;
    hat.pan = 0.25;
    hat.human_ms = 1;
    Patch hoP;
    hoP.inst = Inst::HatO;
    hoP.decay = 0.6;
    hoP.bright = 0.7;
    Track& hato = s.add("hat_open", hoP);
    hato.gain_db = -3;
    hato.pan = 0.25;
    Patch shP;
    shP.inst = Inst::Shaker;
    Track& sh = s.add("shaker", shP);
    sh.gain_db = -5;
    sh.pan = -0.35;
    sh.human_ms = 2;
    Patch snP;
    snP.inst = Inst::Snare;
    snP.bright = 0.7;
    Track& sn = s.add("snare_build", snP);
    sn.gain_db = -6;
    sn.rev = 0.2;
    sn.human_ms = 0;
    Patch crP;
    crP.inst = Inst::Crash;
    Track& crash = s.add("crash", crP);
    crash.gain_db = -6;
    crash.rev = 0.2;
    Patch swP;
    swP.inst = Inst::Swell;
    swP.bright = 0.8;
    Track& swell = s.add("riser", swP);
    swell.gain_db = -6;
    swell.rev = 0.25;
    for (int bar = 0; bar < s.bars; ++bar) {
        double b = bar * 4.0;
        bool chorus = (b >= C && b < A2) || (b >= C2 && b < O);
        bool build = b >= B && b < C;
        if (b >= A1 && !(build && b < B + 16))
            kick.pat(b, 1, "x...x...x...x...", 60, 0.25, 1.0);
        if (build && b < B + 16)
            kick.pat(b, 1, "x.......x.......", 60, 0.25, 0.9);
        if (b >= A1)
            clap.pat(b, 1, "....x.......x...", 60, 0.25, chorus ? 0.95 : 0.8);
        hat.pat(b, 1, b < A1 ? "..x...x...x...x." : "..x...x...x...xg", 60, 0.25, 0.7);
        if (chorus)
            hato.pat(b, 1, "..x...x...x...x.", 60, 0.25, 0.6);
        if (b >= A1)
            sh.pat(b, 1, "gxgxgxgxgxgxgxgx", 60, 0.25, 0.5);
    }
    // snare build: 8ths then 16ths across the last two bars of B
    for (int k = 0; k < 8; ++k)
        sn.n(B + 24 + k * 0.5, 0.5, 60, 0.35 + 0.04 * k);
    for (int k = 0; k < 16; ++k)
        sn.n(B + 28 + k * 0.25, 0.25, 60, 0.55 + 0.028 * k);
    swell.n(C - 8, 8, 60, 0.8);
    swell.n(C2 - 4, 4, 60, 0.6);
    for (double c : {I, A1, C, A2, C2, O})
        crash.n(c, 1, 60, 0.8);
    return s;
}

// ---------------------------------------------------------------------------------------------
// Nature Cube: "Still Water, Turning Glass" - Eb lydian, 72 BPM; pads, harp, flute, lake texture.
Song songNatureCube() {
    Song s;
    s.id = "music_nature_cube_loop";
    s.title = "Still Water, Turning Glass (Nature Cube)";
    s.spb = spbFor(72);
    s.bars = 32;
    s.sections = {{"A1_harp_droplets", 8}, {"B_kalimba_choir", 8}, {"A2_flute", 8}, {"C_flute", 8}};
    s.rev_size = 1.0;
    s.rev_rt60 = 3.0;
    s.rev_damp = 0.4;
    s.rev_predelay = 0.04;
    s.dly_beats = 1.5;
    s.dly_fb = 0.35;
    s.dly_lp = 3500;
    s.comp_ratio = 1.3;
    s.tail = 10.0;
    s.description = "Airy lake scene: lydian pads and harp ripples, a breathy flute, water and "
                    "wind beds, distant birds.";
    const double A1 = 0, B = 32, A2 = 64, C = 96;
    std::vector<ChordAt> h1 = prog(A1, "Ebmaj7 F/Eb Ebmaj7 F/Eb Cm7 Abmaj7 Bbsus4 Bb");
    std::vector<ChordAt> hB = prog(B, "Abmaj7 Bb/Ab Gm7 Cm7 Fm7 Bb7sus4 Ebmaj7/G Abmaj7");
    std::vector<ChordAt> h2 = prog(A2, "Ebmaj7 F/Eb Ebmaj7 F/Eb Cm7 Abmaj7 Bbsus4 Bb");
    std::vector<ChordAt> hC = prog(C, "Cm9 Abmaj7 Ebmaj7/Bb Fadd9/A Abmaj7 Gm7 Fm7 Bbsus4:2 Bb:2");
    std::vector<ChordAt> all = cat({h1, hB, h2, hC});
    s.chords = all;

    Patch harpP;
    harpP.inst = Inst::Harp;
    harpP.bright = 0.8;
    Track& harp = s.add("harp", harpP);
    harp.gain_db = 0;
    harp.eq_high_db = 4;
    harp.pan = -0.25;
    harp.rev = 0.35;
    harp.dly = 0.08;
    harp.human_ms = 7;
    harp.human_vel = 0.08;
    arpChords(harp, h1, 51, 79, "01234321", 0.5, 5, 0.55, 2.0);
    arpChords(harp, hB, 51, 79, "0.2.4.3.", 0.5, 5, 0.5, 2.0);
    arpChords(harp, h2, 51, 79, "01234321", 0.5, 5, 0.5, 2.0);
    arpChords(harp, hC, 51, 79, "0124 3421", 0.5, 5, 0.55, 2.0);

    Patch padP;
    padP.inst = Inst::Pad;
    padP.bright = 0.85;
    padP.attack = 1.6;
    padP.release = 2.2;
    padP.width = 0.9;
    Track& pad = s.add("pad", padP);
    pad.gain_db = -11;
    pad.rev = 0.5;
    pad.hp = 150;
    pad.eq_high_db = 6;
    padChords(pad, all, 55, 74, 4, 0.5);
    Patch chP;
    chP.inst = Inst::Choir;
    chP.param = 0.8;
    chP.attack = 1.2;
    chP.release = 1.8;
    Track& choir = s.add("choir", chP);
    choir.gain_db = -11;
    choir.eq_high_db = 4;
    choir.rev = 0.55;
    choir.hp = 180;
    padChords(choir, cat({hB, hC}), 58, 74, 3, 0.5);

    Patch flP;
    flP.inst = Inst::Flute;
    flP.bright = 0.5;
    flP.glide = 0.03;
    Track& fl = s.add("flute", flP);
    fl.gain_db = -2;
    fl.pan = 0.15;
    fl.rev = 0.4;
    fl.dly = 0.12;
    fl.human_ms = 8;
    double e = fl.mel(A2,
                      "G5:hd Bb5:q  A5:w  Bb5:qd G5:e F5:h  A5:hd F5:q  G5:qd F5:e Eb5:h  C6:qd "
                      "Bb5:e G5:h  F5:hd Eb5:q  D5:w",
                      0, 0.7, 1.0);
    expectBeat(e, C, "nature A2");
    e = fl.mel(C,
               "D5:q Eb5:q G5:q Bb5:q  C6:hd Bb5:q  G5:hd F5:q  A5:hd G5:q  Eb6:qd C6:e Bb5:h  "
               "D6:qd Bb5:e F5:h  Ab5:qd G5:e F5:q C5:q  Eb5:h D5:h",
               0, 0.72, 1.0);
    expectBeat(e, 128, "nature C");

    Patch kaP;
    kaP.inst = Inst::Kalimba;
    kaP.bright = 0.4;
    Track& ka = s.add("kalimba", kaP);
    ka.gain_db = 1;
    ka.pan = 0.3;
    ka.rev = 0.35;
    ka.dly = 0.2;
    ka.human_ms = 6;
    e = ka.mel(B,
               "Eb5:q G5:q C6:h  D6:qd C6:e Bb5:h  Bb5:q D6:q F6:h  Eb6:hd r:q  C6:q Ab5:q F5:q "
               "Eb5:q  F5:hd Eb5:q  G5:q Bb5:q Eb6:h  C6:w",
               0, 0.65, 1.0);
    expectBeat(e, A2, "nature B");

    // water droplets: sparse high celesta notes from the Eb lydian pentatonic set (deterministic)
    Patch ceP;
    ceP.inst = Inst::Celesta;
    ceP.bright = 0.35;
    Track& drops = s.add("droplets", ceP);
    drops.gain_db = -6;
    drops.pan = 0.4;
    drops.rev = 0.5;
    drops.dly = 0.25;
    drops.human_ms = 20;
    {
        Rng r(4242);
        const int pool[7] = {82, 84, 87, 89, 91, 94, 96};
        for (double b = 2; b < 128; b += 2.5 + std::floor(r.uni() * 4) * 0.5) {
            if (b >= A2 && b < C)
                continue; // let the flute speak
            drops.n(b, 0.5, pool[int(r.uni() * 7)], 0.3 + 0.25 * r.uni(), r.bip() * 0.6);
        }
    }
    Patch subP;
    subP.inst = Inst::SubBass;
    subP.attack = 0.4;
    subP.release = 1.0;
    Track& sub = s.add("sub", subP);
    sub.gain_db = -11;
    sub.human_ms = 0;
    bassLine(sub, all, 39, "R-------", 0.5, 0.5, 1.0);

    Patch waP;
    waP.inst = Inst::Water;
    Track& water = s.add("water_bed", waP);
    water.gain_db = -4;
    water.rev = 0.15;
    water.bed = true;
    water.hp = 120;
    water.n(0, 128 + 24, 60, 0.7);
    Patch aiP;
    aiP.inst = Inst::Air;
    Track& air = s.add("air_bed", aiP);
    air.gain_db = -9;
    air.rev = 0.3;
    air.bed = true;
    air.hp = 300;
    air.eq_high_db = 6;
    air.n(0, 128 + 24, 60, 0.6);
    Patch biP;
    biP.inst = Inst::Bird;
    Track& birds = s.add("birds", biP);
    birds.gain_db = -8;
    birds.rev = 0.5;
    birds.dly = 0.15;
    birds.human_ms = 0;
    {
        Rng r(777);
        for (double b = 5; b < 124; b += 9 + std::floor(r.uni() * 10))
            birds.n(b + r.uni(), 1, 98 + std::floor(r.uni() * 6), 0.4 + 0.3 * r.uni(),
                    r.bip() * 0.8);
    }
    return s;
}

// ---------------------------------------------------------------------------------------------
// Untangle: "Rubber Band Shuffle" - elastic, bouncy F major, 104 BPM.
Song songUntangle() {
    Song s;
    s.id = "music_untangle_loop";
    s.title = "Rubber Band Shuffle (Untangle)";
    s.spb = spbFor(104);
    s.bars = 44;
    s.swing16 = 0.18;
    s.sections = {{"intro", 4},     {"A1_pizz", 8},     {"A2_marimba", 8},
                  {"B_whistle", 8}, {"C_breakdown", 8}, {"A3_tutti", 8}};
    s.rev_size = 0.55;
    s.rev_rt60 = 1.2;
    s.rev_damp = 0.5;
    s.dly_beats = 0.75;
    s.dly_fb = 0.25;
    s.description = "Quirky elastic puzzle groove: pizzicato and marimba hooks, slide-whistle "
                    "stretches, a boinging glide bass.";
    const double I = 0, A1 = 16, A2 = 48, B = 80, C = 112, A3 = 144;
    std::vector<ChordAt> hI = prog(I, "F D7 Gm7 C7");
    std::vector<ChordAt> h1 = prog(A1, "F F/A Bb Bdim7 F/C D7 Gm7:2 C7:2 F:2 C7:2");
    std::vector<ChordAt> h2 = prog(A2, "F F/A Bb Bdim7 F/C D7 Gm7:2 C7:2 F:2 C7:2");
    std::vector<ChordAt> hB = prog(B, "Bb Bbm6 F/A D7 Gm7 C7 Am7:2 D7:2 Gm7:2 C7:2");
    std::vector<ChordAt> hC = prog(C, "Dm Dm/C Bb A7 Dm Dm/C Gm7 C7");
    std::vector<ChordAt> h3 = prog(A3, "F F/A Bb Bdim7 F/C D7 Gm7:2 C7:2 F:2 C7:2");
    std::vector<ChordAt> all = cat({hI, h1, h2, hB, hC, h3});
    s.chords = all;

    const std::string ta =
        "C5.:e F5.:e A5.:e C6.:e r:e A5.:e C6:q  Bb5.:e A5.:e G5.:e F5.:e G5:q r:q  "
        "D5.:e F5.:e Bb5.:e D6.:e r:e Bb5.:e D6:q  F6:q D6.:e B5.:e Ab5:q r:q  "
        "A5.:e C6.:e F6:q E6.:e D6.:e C6:q  F#5.:e A5.:e D6:q C6.:e A5.:e F#5:q  "
        "G5.:e A5.:e Bb5:q C6.:e Bb5.:e G5:q  F5:q r:q r:h";
    Patch pzP;
    pzP.inst = Inst::Pizz;
    pzP.bright = 0.6;
    Track& pz = s.add("pizz_melody", pzP);
    pz.gain_db = 1;
    pz.pan = -0.2;
    pz.rev = 0.18;
    pz.human_ms = 4;
    double e = pz.mel(A1, ta, 0, 0.85, 0.9);
    expectBeat(e, A2, "untangle A1");
    e = pz.mel(A3, ta, 0, 0.88, 0.9);
    expectBeat(e, 176, "untangle A3");
    e = pz.mel(C,
               "D5.:e F5.:e A5.:e r:e r:h  r:w  Bb4.:e D5.:e F5.:e r:e r:h  r:w  F5.:e A5.:e D6.:e "
               "r:e r:h  r:w  "
               "Bb5.:e A5.:e G5.:e F5.:e E5.:e F5.:e G5:q  Bb5:q G5:q E5:q C5:q",
               0, 0.85, 0.9);
    expectBeat(e, A3, "untangle C");

    Patch maP;
    maP.inst = Inst::Marimba;
    maP.bright = 0.55;
    Track& mm = s.add("marimba_melody", maP);
    mm.gain_db = 0;
    mm.pan = 0.2;
    mm.rev = 0.18;
    mm.human_ms = 4;
    e = mm.mel(A2, ta, 0, 0.85, 0.9);
    expectBeat(e, B, "untangle A2");
    mm.mel(A3, ta, 12, 0.4, 0.9);
    Track& ost = s.add("marimba_ostinato", maP);
    ost.gain_db = -5;
    ost.pan = -0.35;
    ost.rev = 0.15;
    ost.human_ms = 3;
    ost.human_vel = 0.08;
    arpChords(ost, all, 53, 69, "0212 0312", 0.5, 3, 0.6, 0.9);

    // slide whistle: elastic stretches answering the melody, legato bridge tune
    Patch whP;
    whP.inst = Inst::Whistle;
    whP.glide = 0.06;
    whP.bright = 0.5;
    Track& wh = s.add("slide_whistle", whP);
    wh.gain_db = -3;
    wh.pan = 0.25;
    wh.rev = 0.25;
    wh.dly = 0.1;
    wh.human_ms = 3;
    for (double base : {A1, A2, A3}) {
        wh.mel(base + 6, "G5:e ~D6:qd", 0, 0.7, 0.95);
        wh.mel(base + 14, "F6:e ~B5:qd", 0, 0.7, 0.95);
        wh.mel(base + 29, "C5:e ~C6:e ~E6:h", 0, 0.75, 0.95);
    }
    e = wh.mel(B,
               "D5:q F5:q ~Bb5:h  G5:qd F5:e ~Db5:h  C5:q F5:q ~A5:h  F#5:qd A5:e ~C6:h  "
               "Bb5:qd A5:e G5:q F5:q  E5:q G5:q ~C6:h  C6:q A5:q F#5:q D5:q  G5:h ~E5:h",
               0, 0.75, 1.0);
    expectBeat(e, C, "untangle B");
    for (double t : {C + 6.0, C + 14.0, C + 22.0})
        wh.mel(t, "A5:e ~D6:qd", t == C + 14.0 ? -1 : 0, 0.7, 0.95);
    wh.mel(I + 12, "C5:e ~C6:e ~G6:qd ~C6:q", 0, 0.6, 0.95);

    // bouncy glide bass: root, octave "boing", fifth, with slides
    Patch gbP;
    gbP.inst = Inst::GlideBass;
    gbP.bright = 0.5;
    gbP.glide = 0.035;
    Track& gb = s.add("glide_bass", gbP);
    gb.gain_db = -2;
    gb.rev = 0.03;
    gb.human_ms = 2;
    for (const ChordAt& c : all) {
        for (double bb = c.beat; bb < c.beat + c.dur - 1e-9; bb += 2.0) {
            int r = bassNote(c.c, 36);
            gb.n(bb, 0.7, r, 0.85);
            Note o;
            o.beat = bb + 0.75;
            o.dur = 0.5;
            o.pitch = r + 12;
            o.vel = 0.7;
            o.glide = true;
            gb.notes.push_back(o);
            Note f;
            f.beat = bb + 1.5;
            f.dur = 0.4;
            f.pitch = r + 7;
            f.vel = 0.75;
            gb.notes.push_back(f);
        }
    }

    Patch kP;
    kP.inst = Inst::Kick;
    kP.bright = 0.4;
    kP.decay = 0.7;
    Track& kick = s.add("kick", kP);
    kick.gain_db = -7;
    kick.human_ms = 2;
    Patch rimP;
    rimP.inst = Inst::Rim;
    Track& rim = s.add("rim", rimP);
    rim.gain_db = -2;
    rim.pan = 0.15;
    rim.rev = 0.12;
    rim.human_ms = 3;
    Patch cP;
    cP.inst = Inst::Clap;
    Track& clap = s.add("clap", cP);
    clap.gain_db = -3;
    clap.rev = 0.2;
    clap.human_ms = 2;
    Patch shP;
    shP.inst = Inst::Shaker;
    Track& sh = s.add("shaker", shP);
    sh.gain_db = -9;
    sh.pan = -0.35;
    sh.human_ms = 3;
    Patch wbP;
    wbP.inst = Inst::WoodBlock;
    Track& wb = s.add("woodblock", wbP);
    wb.gain_db = -8;
    wb.pan = 0.45;
    wb.rev = 0.12;
    wb.human_ms = 3;
    Patch cbP;
    cbP.inst = Inst::Cowbell;
    Track& cow = s.add("cowbell", cbP);
    cow.gain_db = -9;
    cow.pan = -0.3;
    cow.rev = 0.1;
    for (int bar = 0; bar < s.bars; ++bar) {
        double b = bar * 4.0;
        bool brk = b >= C && b < A3;
        if (b >= A1 && !brk)
            kick.pat(b, 1, "x.....x.x.......", 60, 0.25, 0.9);
        if (brk)
            kick.pat(b, 1, "x.......x.......", 60, 0.25, 0.7);
        if (b >= A1)
            rim.pat(b, 1, "....x.......x...", 60, 0.25, 0.75);
        if (b >= A3)
            clap.pat(b, 1, "....x.......x...", 60, 0.25, 0.75);
        sh.pat(b, 1, "gxgxgxgxgxgxgxgx", 60, 0.25, b >= A1 ? 0.6 : 0.4);
        wb.pat(b, 1, brk ? "x..x..x...x..x.." : "..x.......x....x", brk ? 79 : 84, 0.25, 0.55);
        if (b >= B && b < C)
            cow.pat(b, 1, "x.....x...x.....", 60, 0.25, 0.6);
    }
    return s;
}

// ---------------------------------------------------------------------------------------------
// Puzzle Solve: "Tangram Funk" - crisp clavinet, vibes and light funk in E dorian, 96 BPM.
Song songPuzzleSolve() {
    Song s;
    s.id = "music_puzzle_solve_loop";
    s.title = "Tangram Funk (Puzzle Solve)";
    s.spb = spbFor(96);
    s.bars = 44;
    s.swing16 = 0.12;
    s.sections = {{"intro_clav", 4}, {"A1_vibes", 8},   {"A2_vibes", 8},
                  {"B_steps", 8},    {"C_geometry", 8}, {"A3", 8}};
    s.rev_size = 0.55;
    s.rev_rt60 = 1.3;
    s.rev_damp = 0.45;
    s.dly_beats = 0.75;
    s.dly_fb = 0.28;
    s.comp_ratio = 2.0;
    s.description = "Clever light funk: interlocking clav and bass, vibes theme; stepwise "
                    "sequences and a dotted-eighth "
                    "cross-rhythm section that fits pieces together.";
    const double I = 0, A1 = 16, A2 = 48, B = 80, C = 112, A3 = 144;
    std::vector<ChordAt> hI = prog(I, "Em7 A7 Em7 A7");
    std::vector<ChordAt> h1 = prog(A1, "Em7 A7 Em7 A7 Cmaj7 B7#9 Em7 A7");
    std::vector<ChordAt> h2 = prog(A2, "Em7 A7 Em7 A7 Cmaj7 B7#9 Em7 A7");
    std::vector<ChordAt> hB = prog(B, "Gmaj7 F#m7 Em7 Dmaj7 Cmaj7 Bm7 Am7 B7sus4:2 B7:2");
    std::vector<ChordAt> hC = prog(C, "Cmaj7 D/C Bm7 Em7 Cmaj7 D/C Fmaj7#11 B7sus4:2 B7#9:2");
    std::vector<ChordAt> h3 = prog(A3, "Em7 A7 Em7 A7 Cmaj7 B7#9 Em7 A7");
    std::vector<ChordAt> all = cat({hI, h1, h2, hB, hC, h3});
    s.chords = all;

    const std::string va = "B4:e D5:e E5:e G5:e A5:q G5.:e E5.:e  F#5:e E5:e C#5:e A4:e B4:q r:q  "
                           "B4:e D5:e E5:e G5:e B5:q A5.:e G5.:e  F#5:qd E5:e C#5:q r:q";
    const std::string vb1 = "G5:e B5:e E6:e B5:e G5:q E5:q  D#5:e F#5:e A5:e D6:e B5:q A5:q  G5:e "
                            "F#5:e E5:e D5:e E5:q B4:q  C#5:q E5:q A5:q r:q";
    const std::string vb2 = "E6:e D6:e B5:e G5:e A5:q B5:q  A5:e F#5:e D#5:e F#5:e A5:q B5:q  "
                            "E6:qd D6:e B5:q G5:q  A5:hd r:q";
    const std::string vbr = "D6:qd B5:e F#5:q D5:q  C#6:qd A5:e E5:q C#5:q  B5:qd G5:e D5:q B4:q  "
                            "A5:qd F#5:e C#5:q A4:q  "
                            "E5:e G5:e B5:e E6:e D6:q B5:q  D5:e F#5:e A5:e D6:e C#6:q A5:q  C5:e "
                            "E5:e G5:e C6:e B5:q G5:q  E5:q A5:q D#5:q F#5:q";

    Patch vbP;
    vbP.inst = Inst::Vibes;
    vbP.bright = 0.6;
    vbP.param = 0.25;
    Track& vib = s.add("vibes", vbP);
    vib.gain_db = 1;
    vib.pan = 0.15;
    vib.rev = 0.2;
    vib.dly = 0.1;
    vib.human_ms = 4;
    double e = vib.mel(A1, va, 0, 0.8, 0.9);
    e = vib.mel(e, vb1, 0, 0.8, 0.9);
    expectBeat(e, A2, "puzzle A1");
    e = vib.mel(A2, va, 0, 0.82, 0.9);
    e = vib.mel(e, vb2, 0, 0.82, 0.9);
    expectBeat(e, B, "puzzle A2");
    e = vib.mel(B, vbr, 0, 0.82, 0.9);
    expectBeat(e, C, "puzzle B");
    e = vib.mel(A3, va, 0, 0.85, 0.9);
    e = vib.mel(e, vb1, 0, 0.85, 0.9);
    expectBeat(e, 176, "puzzle A3");
    // C: dotted-eighth cross-rhythm arpeggios (3 against 4) over the shifting chords
    arpChords(vib, hC, 64, 83, "0123 2130", 0.75, 4, 0.7, 0.8);

    Patch clvP;
    clvP.inst = Inst::Clav;
    clvP.bright = 0.55;
    Track& clav = s.add("clav", clvP);
    clav.gain_db = -3;
    clav.pan = -0.3;
    clav.rev = 0.08;
    clav.human_ms = 3;
    clav.human_vel = 0.08;
    compChords(clav, cat({hI, h1, h2}), 59, 71, "x.gx.xg.x.gx.xg.", 3, 0.7, 0.5, 0.25, true);
    compChords(clav, hB, 59, 72, "x..x..x...x..x..", 3, 0.65, 0.5, 0.25, true);
    compChords(clav, hC, 59, 72, "x.g.x.g.x.g.x.gx", 3, 0.6, 0.5, 0.25, true);
    compChords(clav, h3, 59, 71, "x.gx.xg.x.gx.xgx", 3, 0.72, 0.5, 0.25, true);

    Patch epP;
    epP.inst = Inst::EP;
    epP.bright = 0.4;
    epP.param = 0.3;
    Track& ep = s.add("ep_pad", epP);
    ep.gain_db = -1;
    ep.pan = 0.25;
    ep.rev = 0.25;
    ep.hp = 200;
    ep.human_ms = 6;
    compChords(ep, hB, 55, 69, "x...............", 4, 0.5, 1.0, 0.25, true);
    compChords(ep, hC, 55, 69, "x.......x.......", 4, 0.5, 1.0, 0.25, true);

    Patch bsP;
    bsP.inst = Inst::FunkBass;
    bsP.bright = 0.5;
    Track& bass = s.add("bass", bsP);
    bass.gain_db = 0;
    bass.rev = 0.02;
    bass.human_ms = 2;
    bass.human_vel = 0.05;
    bassLine(bass, cat({hI, h1, h2}), 40, "R.RO..R.5.R..7O.", 0.25, 0.85, 0.7);
    bassLine(bass, hB, 40, "R..R..5.R..3.5..", 0.25, 0.85, 0.75);
    bassLine(bass, hC, 36, "R.......R..5..A.", 0.25, 0.85, 0.85);
    bassLine(bass, h3, 40, "R.RO..R.5.R..7O.", 0.25, 0.88, 0.7);

    Patch kP;
    kP.inst = Inst::Kick;
    kP.bright = 0.5;
    kP.decay = 0.75;
    Track& kick = s.add("kick", kP);
    kick.gain_db = -6;
    kick.human_ms = 1;
    Patch snP;
    snP.inst = Inst::Snare;
    snP.bright = 0.65;
    Track& sn = s.add("snare", snP);
    sn.gain_db = -5;
    sn.rev = 0.15;
    sn.human_ms = 2;
    Patch hcP;
    hcP.inst = Inst::HatC;
    hcP.bright = 0.6;
    Track& hat = s.add("hat", hcP);
    hat.gain_db = -1;
    hat.pan = 0.3;
    hat.human_ms = 2;
    Patch hoP;
    hoP.inst = Inst::HatO;
    hoP.decay = 0.5;
    Track& hato = s.add("hat_open", hoP);
    hato.gain_db = -9;
    hato.pan = 0.3;
    Patch rimP;
    rimP.inst = Inst::Rim;
    Track& rim = s.add("rim", rimP);
    rim.gain_db = -1;
    rim.pan = -0.2;
    rim.rev = 0.12;
    Patch shP;
    shP.inst = Inst::Shaker;
    Track& sh = s.add("shaker", shP);
    sh.gain_db = -5;
    sh.pan = -0.4;
    Patch crP;
    crP.inst = Inst::Crash;
    Track& crash = s.add("crash", crP);
    crash.gain_db = -9;
    crash.rev = 0.15;
    for (int bar = 0; bar < s.bars; ++bar) {
        double b = bar * 4.0;
        bool geo = b >= C && b < A3;
        hat.pat(b, 1, "xgxgxgxgxgxgxgxg", 60, 0.25, b < A1 ? 0.55 : 0.7);
        if (b < A1)
            continue;
        kick.pat(b, 1, geo ? "x.....x.....x..." : "x.....x.x..x....", 60, 0.25, 0.95);
        if (!geo)
            sn.pat(b, 1, "....x..g.g..x..g", 60, 0.25, 0.9);
        else
            rim.pat(b, 1, "....x.......x...", 60, 0.25, 0.8);
        hato.pat(b, 1, "..............x.", 60, 0.25, 0.6);
        if (b >= B && b < C)
            sh.pat(b, 1, "gxgxgxgxgxgxgxgx", 60, 0.25, 0.6);
    }
    for (double c : {A1, B, C, A3})
        crash.n(c, 1, 60, 0.75);
    return s;
}

} // namespace ps
