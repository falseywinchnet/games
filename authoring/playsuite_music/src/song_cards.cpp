// Card-table music: Klondike, Spider, FreeCell, Hearts.
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
} // namespace

// ---------------------------------------------------------------------------------------------
// Klondike: "Porch Light Patience" - relaxed swing jazz trio in F, 90 BPM, intro + AABA.
Song songKlondike() {
    Song s;
    s.id = "music_klondike_loop";
    s.title = "Porch Light Patience (Klondike)";
    s.spb = spbFor(90);
    s.bars = 36;
    s.swing = 0.30;
    s.sections = {
        {"intro_vamp", 4}, {"A1_piano", 8}, {"A2_piano", 8}, {"B_bridge", 8}, {"A3_vibes", 8}};
    s.rev_size = 0.65;
    s.rev_rt60 = 1.6;
    s.rev_damp = 0.55;
    s.rev_predelay = 0.015;
    s.dly_beats = 1.0;
    s.dly_fb = 0.2;
    s.comp_ratio = 1.6;
    s.description = "Unhurried brushes-and-upright jazz trio; piano states an AABA tune, vibes "
                    "take the last A.";
    const double I = 0, A1 = 16, A2 = 48, B = 80, A3 = 112;
    std::vector<ChordAt> hI = prog(I, "Gm7:2 C7:2 Am7:2 D7:2 Gm7:2 C7:2 F6:2 C7:2");
    std::vector<ChordAt> h1 = prog(A1, "Fmaj7 D7 Gm7 C7 Am7:2 D7:2 Gm7:2 C7:2 F6 Gm7:2 C7:2");
    std::vector<ChordAt> h2 = prog(A2, "Fmaj7 D7 Gm7 C7 Am7:2 D7:2 Gm7:2 C7:2 F6 Cm7:2 F7:2");
    std::vector<ChordAt> hB = prog(B, "Bbmaj7 Bbm6 Am7 D7 Gm7 C7 Am7:2 Abdim7:2 Gm7:2 C7:2");
    std::vector<ChordAt> h3 = prog(A3, "Fmaj7 D7 Gm7 C7 Am7:2 D7:2 Gm7:2 C7:2 F6 D7");
    std::vector<ChordAt> all = cat({hI, h1, h2, hB, h3});
    s.chords = all;

    const std::string a6 =
        "C5:e D5:e F5:e A5:e+h  r:e A5:e C6:e A5:e F#5:q D5:q  r:e Bb4:e D5:e F5:e A5:q G5:q  "
        "E5:h r:e C5:e D5:e E5:e  G5:q E5:e C5:e F#5:q D5:q  Bb5:q G5:e F5:e E5:q C5:q";
    const std::string end1 = "D5:e F5:e A5:e D6:e+h  C6:e Bb5:e A5:e G5:e E5:q r:q";
    const std::string end2 = "A5:q F5:q D5:q C5:q  Eb5:e G5:e Bb5:e D6:e C6:q r:q";
    const std::string end3 = "D5:e F5:e A5:e D6:e+h  C6:e A5:e F#5:e D5:e+h";
    const std::string bridge = "D6:qd C6:e A5:q F5:q  Db6:qd C6:e Bb5:q G5:q  C6:e B5:e C6:e "
                               "E6:e+h  r:e D6:e C6:e A5:e F#5:q A5:q  "
                               "Bb5:qd A5:e G5:q D5:q  E5:e G5:e Bb5:e C6:e+h  C6:q A5:q B5:q "
                               "Ab5:q  G5:q Bb5:e A5:e G5:q E5:q";

    Patch pianoP;
    pianoP.inst = Inst::Piano;
    pianoP.bright = 0.45;
    Track& mel = s.add("piano_melody", pianoP);
    mel.gain_db = 1;
    mel.pan = -0.1;
    mel.rev = 0.2;
    mel.human_ms = 9;
    mel.human_vel = 0.08;
    double e = mel.mel(A1, a6, 0, 0.72, 0.92);
    e = mel.mel(e, end1, 0, 0.72, 0.92);
    expectBeat(e, A2, "klondike A1");
    e = mel.mel(A2, a6, 0, 0.74, 0.92);
    e = mel.mel(e, end2, 0, 0.74, 0.92);
    expectBeat(e, B, "klondike A2");
    e = mel.mel(B, bridge, 0, 0.76, 0.92);
    expectBeat(e, A3, "klondike B");

    Patch vibP;
    vibP.inst = Inst::Vibes;
    vibP.bright = 0.45;
    vibP.param = 0.35;
    Track& vib = s.add("vibes", vibP);
    vib.gain_db = -1;
    vib.pan = 0.3;
    vib.rev = 0.25;
    vib.dly = 0.06;
    vib.human_ms = 8;
    e = vib.mel(A3, a6, 0, 0.72, 0.95);
    e = vib.mel(e, end3, 0, 0.72, 0.95);
    expectBeat(e, 144, "klondike A3");
    vib.mel(I, "r:h A5:e G5:e F5:e D5:e  E5:h r:h  r:h F5:e E5:e D5:e C5:e  D5:hd r:q", 0, 0.55,
            0.95);

    // piano left hand: rootless shells on a Charleston figure; lighter under the vibes chorus
    Track& comp = s.add("piano_comp", pianoP);
    comp.gain_db = -3;
    comp.pan = -0.15;
    comp.rev = 0.18;
    comp.human_ms = 10;
    comp.human_vel = 0.1;
    compChords(comp, hI, 52, 67, "x..x....", 3, 0.5, 0.85, 0.5, true);
    compChords(comp, h1, 52, 67, "x..x....", 3, 0.48, 0.85, 0.5, true);
    compChords(comp, h2, 52, 67, "x..x..x.", 3, 0.5, 0.85, 0.5, true);
    compChords(comp, hB, 52, 67, "x...x...", 3, 0.52, 0.95, 0.5, true);
    compChords(comp, h3, 53, 70, "x..x...x", 4, 0.55, 0.85, 0.5, true);

    Patch bassP;
    bassP.inst = Inst::UprightBass;
    bassP.bright = 0.45;
    Track& bass = s.add("upright_bass", bassP);
    bass.gain_db = 2;
    bass.rev = 0.06;
    bass.human_ms = 6;
    bass.human_vel = 0.06;
    bassLine(bass, hI, 36, "R-5-", 1.0, 0.8, 0.95);
    bassLine(bass, h1, 36, "R-5-", 1.0, 0.8, 0.95);
    walkingBass(bass, cat({h2, hB, h3}), 36, 52, 7, 0.78);

    // brushes: swish on every half bar, taps on 2 and 4; ride and hi-hat foot from A2
    Patch swP;
    swP.inst = Inst::BrushSweep;
    Track& sweep = s.add("brush_sweep", swP);
    sweep.gain_db = -4;
    sweep.rev = 0.12;
    sweep.human_ms = 0;
    sweep.pan = 0.1;
    Patch brP;
    brP.inst = Inst::Brush;
    brP.bright = 0.4;
    Track& brush = s.add("brush_tap", brP);
    brush.gain_db = -3;
    brush.rev = 0.12;
    brush.human_ms = 6;
    Patch rideP;
    rideP.inst = Inst::Ride;
    rideP.decay = 0.8;
    Track& ride = s.add("ride", rideP);
    ride.gain_db = -7;
    ride.pan = 0.35;
    ride.rev = 0.12;
    ride.human_ms = 5;
    Patch hhP;
    hhP.inst = Inst::HatC;
    hhP.bright = 0.2;
    hhP.decay = 1.5;
    Track& hh = s.add("hat_foot", hhP);
    hh.gain_db = -4;
    hh.pan = 0.25;
    hh.rev = 0.05;
    hh.human_ms = 5;
    Patch kP;
    kP.inst = Inst::Kick;
    kP.bright = 0.1;
    kP.decay = 0.7;
    Track& kick = s.add("kick_feather", kP);
    kick.gain_db = -14;
    kick.human_ms = 6;
    for (int bar = 0; bar < s.bars; ++bar) {
        double b = bar * 4.0;
        for (int k = 0; k < 4; ++k)
            sweep.n(b + k, 1.0, 60, 0.55 + 0.15 * (k % 2));
        brush.pat(b, 1, "..x.g.x.", 60, 0.5, 0.75);
        if (b >= A2) {
            ride.pat(b, 1, "x.xxx.xx", 60, 0.5, 0.6);
            hh.pat(b, 1, "..x...x.", 60, 0.5, 0.7);
        }
        if (b >= A1)
            kick.pat(b, 1, "x.......", 60, 0.5, 0.5);
    }
    // little fills at the turnarounds
    brush.mel(B - 1, "C4:t C4:t C4:t", 0, 0.7);
    brush.mel(A3 - 1, "C4:t C4:t C4:t", 0, 0.75);
    return s;
}

// ---------------------------------------------------------------------------------------------
// Spider: "Eight Legs, Light Feet" - nimble pizzicato ostinato and clarinet in D minor, 100 BPM.
Song songSpider() {
    Song s;
    s.id = "music_spider_loop";
    s.title = "Eight Legs, Light Feet (Spider)";
    s.spb = spbFor(100);
    s.bars = 40;
    s.sections = {{"intro_ostinato", 4}, {"A1_clarinet", 8}, {"A2_clarinet", 8},
                  {"B_major_glock", 8},  {"A3_tutti", 8},    {"tag", 4}};
    s.rev_size = 0.6;
    s.rev_rt60 = 1.5;
    s.rev_damp = 0.5;
    s.dly_beats = 0.75;
    s.dly_fb = 0.25;
    s.description = "Creeping pizzicato walk, staccato clarinet tiptoes and legato answers; "
                    "brightens to F major for the bridge.";
    const double I = 0, A1 = 16, A2 = 48, B = 80, A3 = 112, T = 144;
    std::vector<ChordAt> hI = prog(I, "Dm Dm Dm A7");
    std::vector<ChordAt> h1 = prog(A1, "Dm Dm/C Bbmaj7 A7 Dm Dm/C Gm6 A7");
    std::vector<ChordAt> h2 = prog(A2, "Dm Dm/C Bbmaj7 A7 Gm A7 Dm Dm");
    std::vector<ChordAt> hB = prog(B, "F C/E Dm7 A7/C# Bb F/A Gm7 A7");
    std::vector<ChordAt> h3 = prog(A3, "Dm Dm/C Bbmaj7 A7 Dm Dm/C Gm6 A7");
    std::vector<ChordAt> hT = prog(T, "Dm Bb:2 A7:2 Dm A7");
    std::vector<ChordAt> all = cat({hI, h1, h2, hB, h3, hT});
    s.chords = all;

    // pizzicato spider-walk ostinato: bass, upper tones, chromatic lower neighbour
    Patch pzP;
    pzP.inst = Inst::Pizz;
    pzP.bright = 0.55;
    Track& walk = s.add("pizz_walk", pzP);
    walk.gain_db = -3;
    walk.pan = -0.3;
    walk.rev = 0.2;
    walk.human_ms = 5;
    walk.human_vel = 0.08;
    {
        std::vector<int> prev;
        for (const ChordAt& c : all) {
            for (double bb = c.beat; bb < c.beat + c.dur - 1e-9; bb += 4.0) {
                int low = bassNote(c.c, 38);
                std::vector<int> u = voice(c.c, 55, 69, prev, 3);
                double len = std::min(4.0, c.beat + c.dur - bb);
                const int seq[8] = {low, u[1], u[0], u[1], low + 12, u[2], low + 11, u[1]};
                for (int k = 0; k < int(len * 2); ++k)
                    walk.n(bb + k * 0.5, 0.4, seq[k], k % 4 == 0 ? 0.85 : 0.62);
            }
        }
    }
    // clarinet theme
    const std::string th1 = "A4.:e A4.:e D5.:e E5.:e F5:q E5.:e D5.:e  E5:qd D5:e A4:h  "
                            "Bb4.:e Bb4.:e D5.:e F5.:e A5:q G5.:e F5.:e  E5:qd C#5:e A4:h";
    const std::string th1b =
        "D5.:e F5.:e A5.:e D6.:e C#6:e D6:e A5:q  G5:e F5:e E5:e D5:e E5:q C5:q  "
        "Bb4:e D5:e G5:e Bb5:e A5:e G5:e E5:e C#5:e  E5:q C#5:q A4:h";
    const std::string th2b = "Bb5.:e A5.:e G5.:e F5.:e E5.:e F5.:e G5:q  A5:e G5:e F5:e E5:e C#5:q "
                             "E5:q  D5:hd r:q  r:h A4.:e C5.:e E5.:e G5.:e";
    Patch clP;
    clP.inst = Inst::Clarinet;
    clP.bright = 0.5;
    clP.glide = 0.02;
    Track& cl = s.add("clarinet", clP);
    cl.gain_db = 0;
    cl.pan = 0.15;
    cl.rev = 0.22;
    cl.dly = 0.05;
    cl.human_ms = 6;
    double e = cl.mel(A1, th1, 0, 0.8, 0.95);
    e = cl.mel(e, th1b, 0, 0.8, 0.95);
    expectBeat(e, A2, "spider A1");
    e = cl.mel(A2, th1, 0, 0.82, 0.95);
    e = cl.mel(e, th2b, 0, 0.82, 0.95);
    expectBeat(e, B, "spider A2");
    // bridge counter-line, legato
    e = cl.mel(B, "A4:h C5:h G4:w F4:h A4:h E4:w F4:h D4:h C4:h F4:h Bb4:h D5:h C#5:h E5:h", 0,
               0.62, 1.0);
    expectBeat(e, A3, "spider B cl");
    e = cl.mel(A3, th1, 0, 0.84, 0.95);
    e = cl.mel(e, th1b, 0, 0.84, 0.95);
    expectBeat(e, T, "spider A3");
    e = cl.mel(T, "D5.:e F5.:e A5.:e D6.:e r:h  D6:q Bb5:q C#6:q A5:q  D6:h r:h  r:w", 0, 0.82,
               0.95);
    expectBeat(e, 160, "spider tag");

    Patch glP;
    glP.inst = Inst::Celesta;
    glP.bright = 0.6;
    Track& gl = s.add("celesta", glP);
    gl.gain_db = -1;
    gl.pan = 0.35;
    gl.rev = 0.3;
    gl.dly = 0.12;
    gl.human_ms = 5;
    e = gl.mel(B,
               "C6:e A5:e F5:e A5:e C6:q F6:q  E6:qd D6:e C6:h  A5:e F5:e D5:e F5:e A5:q D6:q  "
               "C#6:qd B5:e A5:h  "
               "D6:e C6:e Bb5:e A5:e Bb5:q D6:q  C6:e Bb5:e A5:e G5:e A5:q F5:q  G5:e A5:e Bb5:e "
               "D6:e F6:q E6:q  E6:q C#6:q A5:q r:q",
               0, 0.75, 0.9);
    expectBeat(e, A3, "spider B celesta");
    gl.mel(A3, th1, 12, 0.42, 0.6);
    gl.mel(A3 + 16, th1b, 12, 0.42, 0.6);
    // intro sparkle
    gl.mel(I + 4, "r:h r:q A5:e D6:e", 0, 0.5);
    gl.mel(I + 12, "r:h C#6:e E6:e A6:q", 0, 0.5);

    // soft string bed in the bridge and last A
    Patch stP;
    stP.inst = Inst::Strings;
    stP.bright = 0.35;
    stP.attack = 0.4;
    Track& st = s.add("strings", stP);
    st.gain_db = -9;
    st.rev = 0.35;
    st.hp = 160;
    padChords(st, hB, 57, 72, 3, 0.5);
    padChords(st, h3, 55, 69, 3, 0.4);

    // light percussion: tiptoe rim on 2 and 4, shaker, wood block
    Patch rimP;
    rimP.inst = Inst::Rim;
    Track& rim = s.add("rim", rimP);
    rim.gain_db = -3;
    rim.pan = 0.2;
    rim.rev = 0.15;
    rim.human_ms = 4;
    Patch shP;
    shP.inst = Inst::Shaker;
    Track& sh = s.add("shaker", shP);
    sh.gain_db = -6;
    sh.pan = -0.4;
    sh.rev = 0.1;
    sh.human_ms = 4;
    Patch wbP;
    wbP.inst = Inst::WoodBlock;
    Track& wb = s.add("woodblock", wbP);
    wb.gain_db = -5;
    wb.pan = 0.45;
    wb.rev = 0.15;
    Patch kP;
    kP.inst = Inst::Kick;
    kP.bright = 0.2;
    kP.decay = 0.6;
    Track& kick = s.add("kick_soft", kP);
    kick.gain_db = -12;
    kick.human_ms = 3;
    Patch trP;
    trP.inst = Inst::Triangle;
    trP.decay = 0.6;
    Track& tri = s.add("triangle", trP);
    tri.gain_db = -2;
    tri.pan = 0.5;
    tri.rev = 0.25;
    for (int bar = 0; bar < s.bars; ++bar) {
        double b = bar * 4.0;
        if (b >= A1)
            rim.pat(b, 1, "....x.......x...", 60, 0.25, 0.7);
        sh.pat(b, 1, b >= A1 ? "x.g.x.gox.g.x.go" : "x.g.x.g.", 60, b >= A1 ? 0.25 : 0.5, 0.55);
        if (b >= A1)
            kick.pat(b, 1, "x.......x.......", 60, 0.25, 0.6);
        if (b >= B && b < A3)
            wb.pat(b, 1, "..x.......x..x..", 84, 0.25, 0.5);
    }
    for (double c : {A1, B, A3})
        tri.n(c, 1, 60, 0.6);
    return s;
}

// ---------------------------------------------------------------------------------------------
// FreeCell: "Clear Columns" - thoughtful piano and strings in C major, 84 BPM.
Song songFreecell() {
    Song s;
    s.id = "music_freecell_loop";
    s.title = "Clear Columns (FreeCell)";
    s.spb = spbFor(84);
    s.bars = 32;
    s.sections = {{"A1_piano", 8}, {"B_minor", 8}, {"A2_strings_melody", 8}, {"C_development", 8}};
    s.rev_size = 0.85;
    s.rev_rt60 = 2.2;
    s.rev_damp = 0.5;
    s.rev_predelay = 0.025;
    s.dly_beats = 1.5;
    s.dly_fb = 0.18;
    s.comp_ratio = 1.5;
    s.description = "Orderly broken-chord piano with a clear stepwise melody; strings take the "
                    "theme on the return.";
    const double A1 = 0, B = 32, A2 = 64, C = 96;
    std::vector<ChordAt> hA1 = prog(A1, "C G/B Am7 Em7/G Fmaj7 C/E Dm7 G7sus4:2 G7:2");
    std::vector<ChordAt> hB = prog(B, "Am Em/G Fmaj7 C/E Dm7 Am7 Bbmaj7 G7sus4:2 G:2");
    std::vector<ChordAt> hA2 = prog(A2, "C G/B Am7 Em7/G Fmaj7 C/E Dm7 G7sus4:2 G7:2");
    std::vector<ChordAt> hC = prog(C, "Fmaj7 G/F Em7 Am7 Dm7 G/D Ebmaj7 G7sus4:2 G7:2");
    std::vector<ChordAt> all = cat({hA1, hB, hA2, hC});
    s.chords = all;

    const std::string ma = "G5:h E5:q D5:q  D5:qd C5:e B4:h  C5:q E5:q A5:qd G5:e  G5:hd E5:q  "
                           "F5:qd E5:e C5:q A4:q  G4:qd C5:e E5:h  F5:q E5:q D5:q C5:q  C5:h B4:h";
    const std::string mb =
        "E5:e A5:e B5:e C6:e B5:q A5:q  B5:qd G5:e E5:h  F5:e A5:e C6:e E6:e D6:q C6:q  G5:hd E5:q "
        " "
        "F5:e A5:e C6:e A5:e F5:q D5:q  E5:qd D5:e C5:q E5:q  D5:q F5:q A5:q Bb5:q  C6:h B5:h";
    const std::string mc =
        "A5:qd G5:e F5:q C5:q  B4:qd C5:e D5:h  G5:qd F5:e E5:q B4:q  C5:hd E5:q  "
        "F5:q A5:q C6:q A5:q  B5:qd A5:e G5:h  G5:q Bb5:q D6:q Bb5:q  C6:h B5:h";

    Patch pianoP;
    pianoP.inst = Inst::Piano;
    pianoP.bright = 0.5;
    Track& mel = s.add("piano_melody", pianoP);
    mel.gain_db = 1;
    mel.pan = -0.05;
    mel.rev = 0.22;
    mel.human_ms = 7;
    mel.human_vel = 0.06;
    double e = mel.mel(A1, ma, 0, 0.7, 0.95);
    expectBeat(e, B, "freecell A1");
    e = mel.mel(B, mb, 0, 0.74, 0.95);
    expectBeat(e, A2, "freecell B");
    e = mel.mel(C, mc, 0, 0.74, 0.95);
    expectBeat(e, 128, "freecell C");
    // on the return the piano answers the strings an octave up, sparsely
    mel.mel(A2 + 6, "B5:e C6:e", 0, 0.45);
    mel.mel(A2 + 14, "G5:e B5:e E6:q", 0, 0.45);
    mel.mel(A2 + 22, "C6:e E6:e", 0, 0.45);
    mel.mel(A2 + 30, "D6:e B5:e G5:e F5:e", 0, 0.45);

    Track& arp = s.add("piano_arpeggio", pianoP);
    arp.gain_db = -4;
    arp.pan = -0.15;
    arp.rev = 0.2;
    arp.human_ms = 6;
    arp.human_vel = 0.08;
    arpChords(arp, hA1, 48, 67, "02312321", 0.5, 4, 0.5, 1.4);
    arpChords(arp, hB, 48, 67, "02312321", 0.5, 4, 0.52, 1.4);
    arpChords(arp, hA2, 48, 69, "0231 4231 0231 4232", 0.25, 4, 0.42, 1.6);
    arpChords(arp, hC, 48, 67, "02312321", 0.5, 4, 0.52, 1.4);
    Track& lh = s.add("piano_bass", pianoP);
    lh.gain_db = -3;
    lh.rev = 0.2;
    lh.human_ms = 6;
    bassLine(lh, all, 36, "R-------", 0.5, 0.55, 1.0);

    Patch stP;
    stP.inst = Inst::Strings;
    stP.bright = 0.45;
    stP.attack = 0.35;
    stP.width = 0.8;
    Track& pad = s.add("strings_pad", stP);
    pad.gain_db = -11;
    pad.rev = 0.35;
    pad.hp = 150;
    padChords(pad, hA1, 55, 72, 3, 0.4);
    padChords(pad, hB, 55, 72, 4, 0.5);
    padChords(pad, hA2, 52, 69, 4, 0.5);
    padChords(pad, hC, 55, 72, 4, 0.55);
    Patch smP = stP;
    smP.attack = 0.12;
    smP.bright = 0.55;
    Track& sm = s.add("strings_melody", smP);
    sm.gain_db = -2;
    sm.pan = 0.15;
    sm.rev = 0.32;
    sm.hp = 140;
    sm.human_ms = 4;
    e = sm.mel(A2, ma, 0, 0.72, 1.0);
    expectBeat(e, C, "freecell A2 strings");
    Patch ceP = stP;
    ceP.attack = 0.15;
    Track& cello = s.add("cello", ceP);
    cello.gain_db = -6;
    cello.pan = 0.1;
    cello.rev = 0.3;
    cello.hp = 45;
    bassLine(cello, cat({hB, hA2, hC}), 36, "R---5---", 0.5, 0.55, 1.0);
    return s;
}

// ---------------------------------------------------------------------------------------------
// Hearts: "Passing Three" - a sly, swinging parlour tune in G minor, 110 BPM.
Song songHearts() {
    Song s;
    s.id = "music_hearts_loop";
    s.title = "Passing Three (Hearts)";
    s.spb = spbFor(110);
    s.bars = 36;
    s.swing = 0.36;
    s.sections = {{"intro_vamp", 4},
                  {"A1_trumpet", 8},
                  {"A2_trumpet_clarinet", 8},
                  {"B_bridge", 8},
                  {"A3", 8}};
    s.rev_size = 0.55;
    s.rev_rt60 = 1.3;
    s.rev_damp = 0.55;
    s.dly_beats = 0.75;
    s.dly_fb = 0.2;
    s.description = "Cheeky muted trumpet over stride piano, finger snaps and brushes; clarinet "
                    "sneaks in harmony.";
    const double I = 0, A1 = 16, A2 = 48, B = 80, A3 = 112;
    std::vector<ChordAt> hI = prog(I, "Gm:2 Eb7:2 D7 Gm:2 Eb7:2 D7");
    std::vector<ChordAt> h1 =
        prog(A1, "Gm Gm/F Ebmaj7 D7 Gm Cm7:2 F7:2 Bbmaj7:2 Ebmaj7:2 Am7b5:2 D7:2");
    std::vector<ChordAt> h2 = prog(A2, "Gm Gm/F Ebmaj7 D7 Gm Cm7:2 F7:2 Gm:2 D7:2 Gm");
    std::vector<ChordAt> hB = prog(B, "Cm7 F7 Bbmaj7 Ebmaj7 Am7b5 D7 Gm:2 Gm/F:2 Ebmaj7:2 D7:2");
    std::vector<ChordAt> h3 =
        prog(A3, "Gm Gm/F Ebmaj7 D7 Gm Cm7:2 F7:2 Bbmaj7:2 Ebmaj7:2 Am7b5:2 D7:2");
    std::vector<ChordAt> all = cat({hI, h1, h2, hB, h3});
    s.chords = all;

    const std::string a =
        "r:e D5.:e G5.:e Bb5.:e A5:q G5.:e D5.:e  F5:e Eb5:e D5:e C5:e Bb4:h  "
        "r:e Eb5.:e G5.:e Bb5.:e D6:q C6.:e Bb5.:e  A5:qd F#5:e D5:h  "
        "G5.:e r:e G5.:e A5:e Bb5:e A5:e G5:e F#5:e  Eb5:e G5:e C6:e Bb5:e A5:e F5:e Eb5:e C5:e";
    const std::string end1 = "D5:qd F5:e G5:qd Bb5:e  A5:q C6:e A5:e F#5:q D5:q";
    const std::string end2 = "Bb5:q G5:e D5:e F#5:q A5:q  G5:hd r:q";
    const std::string br = "C5:e Eb5:e G5:e Bb5:e C6:q G5:q  F5:qd Eb5:e C5:h  D5:e F5:e A5:e C6:e "
                           "Bb5:q A5:q  G5:hd r:q  "
                           "C6:e Bb5:e A5:e G5:e Eb5:q C5:q  F#5:e G5:e A5:e C6:e A5:q F#5:q  G5:q "
                           "Bb5:q A5:q F5:q  G5:q Eb5:q F#5:q r:q";

    Patch tpP;
    tpP.inst = Inst::MutedTrumpet;
    tpP.bright = 0.5;
    tpP.glide = 0.03;
    Track& tp = s.add("muted_trumpet", tpP);
    tp.gain_db = 0;
    tp.pan = 0.1;
    tp.rev = 0.18;
    tp.dly = 0.06;
    tp.human_ms = 8;
    tp.human_vel = 0.07;
    double e = tp.mel(A1, a, 0, 0.8, 0.92);
    e = tp.mel(e, end1, 0, 0.8, 0.92);
    expectBeat(e, A2, "hearts A1");
    e = tp.mel(A2, a, 0, 0.82, 0.92);
    e = tp.mel(e, end2, 0, 0.82, 0.92);
    expectBeat(e, B, "hearts A2");
    e = tp.mel(B, br, 0, 0.82, 0.95);
    expectBeat(e, A3, "hearts B");
    e = tp.mel(A3, a, 0, 0.85, 0.92);
    e = tp.mel(e, end1, 0, 0.85, 0.92);
    expectBeat(e, 144, "hearts A3");

    Patch clP;
    clP.inst = Inst::Clarinet;
    clP.bright = 0.45;
    clP.glide = 0.02;
    Track& cl = s.add("clarinet", clP);
    cl.gain_db = -3;
    cl.pan = -0.3;
    cl.rev = 0.2;
    cl.human_ms = 8;
    cl.mel(I,
           "D4:e G4:e Bb4:e D5:e+h  C5:q A4:q F#4:q D4:q  D4:e G4:e Bb4:e D5:e Db5:h  D5:q C5:q "
           "A4:q F#4:q",
           0, 0.7, 0.92);
    {
        Track tmp;
        blockHarmony(tmp, h2, A2, a + "  " + end2, 2, 0, 0.7, 0.92, 50);
        blockHarmony(tmp, h3, A3, a + "  " + end1, 2, 0, 0.72, 0.92, 50);
        // keep only the harmony voice (second note at each onset)
        for (std::size_t i = 0; i + 1 < tmp.notes.size(); ++i)
            if (std::fabs(tmp.notes[i + 1].beat - tmp.notes[i].beat) < 1e-9 &&
                tmp.notes[i + 1].pitch < tmp.notes[i].pitch) {
                Note h = tmp.notes[i + 1];
                h.pan = 0;
                cl.notes.push_back(h);
                ++i;
            }
    }
    // bridge: clarinet low answers
    cl.mel(B + 6, "C5:e Bb4:e", 0, 0.6);
    cl.mel(B + 14, "D5:e C5:e Bb4:e A4:e", 0, 0.6);
    cl.mel(B + 22, "A4:e Bb4:e C5:e", 0, 0.6);

    // stride piano: bass note on 1 and 3, chord on 2 and 4
    Patch pnP;
    pnP.inst = Inst::Piano;
    pnP.bright = 0.6;
    Track& lh = s.add("stride_bass", pnP);
    lh.gain_db = -3;
    lh.pan = -0.1;
    lh.rev = 0.15;
    lh.human_ms = 8;
    lh.human_vel = 0.07;
    bassLine(lh, all, 38, "R.5.", 1.0, 0.65, 0.8);
    Track& rh = s.add("stride_chords", pnP);
    rh.gain_db = -5;
    rh.pan = -0.15;
    rh.rev = 0.15;
    rh.human_ms = 8;
    rh.human_vel = 0.08;
    compChords(rh, all, 55, 70, ".x.x", 3, 0.55, 0.55, 1.0, true);

    Patch ubP;
    ubP.inst = Inst::UprightBass;
    ubP.bright = 0.5;
    Track& ub = s.add("upright_bass", ubP);
    ub.gain_db = -1;
    ub.rev = 0.06;
    ub.human_ms = 6;
    walkingBass(ub, cat({h2, hB, h3}), 36, 52, 3, 0.72);

    Patch snP;
    snP.inst = Inst::Snap;
    Track& snap = s.add("finger_snaps", snP);
    snap.gain_db = 0;
    snap.pan = 0.4;
    snap.rev = 0.2;
    snap.human_ms = 7;
    Patch brP;
    brP.inst = Inst::Brush;
    brP.bright = 0.5;
    Track& brush = s.add("brush", brP);
    brush.gain_db = 0;
    brush.rev = 0.12;
    brush.human_ms = 6;
    Patch swP;
    swP.inst = Inst::BrushSweep;
    Track& sweep = s.add("brush_sweep", swP);
    sweep.gain_db = -4;
    sweep.rev = 0.1;
    sweep.human_ms = 0;
    Patch hhP;
    hhP.inst = Inst::HatC;
    hhP.bright = 0.3;
    hhP.decay = 1.3;
    Track& hh = s.add("hat_foot", hhP);
    hh.gain_db = -4;
    hh.pan = 0.3;
    hh.human_ms = 5;
    Patch rideP;
    rideP.inst = Inst::Ride;
    rideP.decay = 0.7;
    Track& ride = s.add("ride", rideP);
    ride.gain_db = -8;
    ride.pan = 0.35;
    ride.rev = 0.1;
    ride.human_ms = 5;
    for (int bar = 0; bar < s.bars; ++bar) {
        double b = bar * 4.0;
        bool bridge = b >= B && b < A3;
        if (!bridge)
            snap.pat(b, 1, "..x...x.", 60, 0.5, 0.75);
        brush.pat(b, 1, bridge ? "..x.g.xg" : "..x...x.", 60, 0.5, 0.6);
        for (int k = 0; k < 2; ++k)
            sweep.n(b + 2 * k, 2.0, 60, 0.5);
        hh.pat(b, 1, "..x...x.", 60, 0.5, 0.65);
        if (bridge || b >= A3)
            ride.pat(b, 1, "x.xxx.xx", 60, 0.5, 0.55);
    }
    return s;
}

} // namespace ps
