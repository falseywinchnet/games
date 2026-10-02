// "Come On In!" - PlaySuite title/menu theme.
// An original 1970s TV game-show style theme: Bb major, 136 BPM, brass fanfares, analog lead,
// octave disco bass, four-on-the-floor; final chorus modulates up a whole step to C.
#include "songs.hpp"

namespace ps {

Song songMenu() {
    Song s;
    s.id = "music_menu_loop";
    s.title = "Come On In! (PlaySuite Menu)";
    s.spb = spbFor(136);
    s.bars = 46;
    s.sections = {{"intro_fanfare", 4}, {"A1", 8},        {"A2", 8},  {"B_call_response", 8},
                  {"A3_brass", 8},      {"A4_key_up", 8}, {"link", 2}};
    s.rev_size = 0.8;
    s.rev_rt60 = 1.7;
    s.rev_damp = 0.45;
    s.rev_predelay = 0.018;
    s.dly_beats = 0.75;
    s.dly_fb = 0.28;
    s.dly_lp = 5000;
    s.comp_ratio = 2.2;
    s.description =
        "Original 70s game-show theme: brass fanfare intro, hummable Moog-style lead over disco "
        "groove, "
        "brass call-and-response bridge, brass-doubled chorus and a whole-step key change.";
    const double I = 0, A1 = 16, A2 = 48, B = 80, A3 = 112, A4 = 144, LK = 176;

    struct JoinChords {

        std::vector<ChordAt> operator()(std::vector<std::vector<ChordAt>> parts) const {
            std::vector<ChordAt> r;
            for (const std::vector<ChordAt>& p : parts)
                r.insert(r.end(), p.begin(), p.end());
            return r;
        }
    };
    JoinChords cat{};
    std::vector<ChordAt> hI = prog(I, "Bb Eb/Bb Bb F7sus4:2 F7:2");
    std::vector<ChordAt> h1 = prog(A1, "Bb Gm7 Eb F7 Bb/D G7 Cm7:2 F7:2 Bb:2 F7:2");
    std::vector<ChordAt> h2 = prog(A2, "Bb Gm7 Eb F7 Bb/D G7 Cm7:2 F7:2 Bb:2 Bb7:2");
    std::vector<ChordAt> hB =
        prog(B, "Ebmaj7 F/Eb Dm7 G7 Cm7 Am7b5:2 D7:2 Gm7:2 C7:2 F7sus4:2 F7:2");
    std::vector<ChordAt> h3 = prog(A3, "Bb Gm7 Eb F7 Bb/D G7 Cm7:2 F7:2 Ab:2 Bb:2");
    std::vector<ChordAt> h4 = prog(A4, "C Am7 F G7 C/E A7 Dm7:2 G7:2 C");
    std::vector<ChordAt> hL = prog(LK, "Cm7 F7");
    std::vector<ChordAt> all = cat({hI, h1, h2, hB, h3, h4, hL});
    s.chords = all;

    // ---- melody material ----
    const std::string ante = "D6:qd C6:e Bb5:q F5:q  G5:e A5:e Bb5:q D6:qd r:e  Eb6:qd D6:e C6:q "
                             "G5:q  A5:e Bb5:e C6:q F6:q Eb6:e C6:e";
    const std::string cons1 =
        "D6:qd C6:e Bb5:q F5:q  G5:e B5:e D6:q F6:qd r:e  Eb6:q D6:e C6:e C6:e A5:e F5:e A5:e";
    const std::string cons2 =
        "D6:qd C6:e Bb5:q D6:q  F6:qd D6:e B5:q G5:q  C6:e Eb6:e G6:q F6:e Eb6:e C6:e A5:e";
    const std::string pickup = "F5:e Bb5:e";

    Patch leadP;
    leadP.inst = Inst::Lead;
    leadP.bright = 0.62;
    leadP.glide = 0.028;
    leadP.vib = 1.0;
    Track& lead = s.add("lead", leadP);
    lead.gain_db = 2.5;
    lead.rev = 0.16;
    lead.dly = 0.14;
    lead.human_ms = 3;
    lead.hp = 150;
    double e;
    lead.mel(A1 - 1, pickup, 0, 0.85, 1.0);
    e = lead.mel(A1, ante, 0, 0.85, 0.97);
    e = lead.mel(e, cons1, 0, 0.85, 0.97);
    expectBeat(e, A1 + 28, "menu A1");
    lead.mel(e, "Bb5:h r:q", 0, 0.85, 0.95);
    lead.mel(A2 - 1, pickup, 0, 0.85, 1.0);
    e = lead.mel(A2, ante, 0, 0.88, 0.97);
    e = lead.mel(e, cons2, 0, 0.9, 0.97);
    expectBeat(e, A2 + 28, "menu A2");
    lead.mel(e, "Bb5:h r:h", 0, 0.88, 0.95);
    // bridge responses
    lead.mel(B + 4, "C6:e D6:e Eb6:e F6:e G6:q F6:e D6:e", 0, 0.82, 0.95);
    lead.mel(B + 12, "G5:e B5:e D6:e F6:e G6:q F6:e D6:e", 0, 0.84, 0.95);
    lead.mel(B + 20, "C6:e Eb6:e G5:e C6:e F#5:e A5:e C6:e Eb6:e", 0, 0.86, 0.95);
    e = lead.mel(B + 24, "D6!:e r:e Bb5:e D6:e  E6!:e r:e C6:e E6:e", 0, 0.9, 0.9);
    expectBeat(e, B + 28, "menu B7");
    lead.mel(A3 - 1, pickup, 0, 0.9, 1.0);
    e = lead.mel(A3, ante, 0, 0.92, 0.97);
    e = lead.mel(
        e, "D6:qd C6:e Bb5:q F5:q  G5:e B5:e D6:q F6:qd r:e  Eb6:q D6:e C6:e C6:e A5:e F5:e A5:e",
        0, 0.92, 0.97);
    e = lead.mel(e, "C6!:qd r:e D6!:q G5:e C6:e", 0, 0.95, 0.95);
    expectBeat(e, A4, "menu A3");
    e = lead.mel(A4, ante, 2, 0.95, 0.97);
    e = lead.mel(e, cons1, 2, 0.95, 0.97);
    expectBeat(e, A4 + 28, "menu A4");
    e = lead.mel(e, "C6!:qd G5:e C6:e E6:e G6!:q", 0, 0.95, 0.95);
    e = lead.mel(e, "G6:qd Eb6:e C6:q G5:q  F5:e A5:e C6:e Eb6:e F6!:q r:q", 0, 0.9, 0.95);
    expectBeat(e, 184, "menu link");

    // ---- brass section ----
    Patch brassP;
    brassP.inst = Inst::Brass;
    brassP.bright = 0.6;
    brassP.width = 0.7;
    Track& brass = s.add("brass", brassP);
    brass.gain_db = 0.0;
    brass.rev = 0.2;
    brass.human_ms = 4;
    brass.hp = 120;
    brass.eq_mid_db = -1.5;
    brass.eq_mid_hz = 500;
    // intro fanfare (4-part close voicing)
    e = blockHarmony(brass, hI, I, "D5!:e F5:e Bb5:e D6!:h r:e", 4, 0, 0.95);
    e = blockHarmony(brass, hI, e, "Eb6!:e D6:e C6:e Bb5:e G5!:q Bb5:q", 4, 0, 0.9);
    e = blockHarmony(brass, hI, e, "D5!:e F5:e Bb5:e D6:e Eb6!:qd D6:e", 4, 0, 0.95);
    e = blockHarmony(brass, hI, e, "Eb6!:e Eb6:e Eb6:e Eb6:e C6!.:q r:q", 4, 0, 0.95);
    expectBeat(e, A1, "menu intro brass");
    // disco stabs behind the A1/A2 lead
    compChords(brass, h1, 55, 70, "......x.....x...", 3, 0.62, 0.55, 0.25, true);
    compChords(brass, h2, 55, 70, "......x.....x..x", 3, 0.66, 0.55, 0.25, true);
    blockHarmony(brass, h2, A2 + 30, "Bb4!:e D5:e F5:e Ab5!:e", 3, 0, 0.9);
    // bridge calls
    blockHarmony(brass, hB, B, "G5!:e r:e Bb5!:e r:e D6!.:q r:q", 4, 0, 0.92);
    blockHarmony(brass, hB, B + 8, "F5!:e r:e A5!:e r:e C6!.:q r:q", 4, 0, 0.92);
    blockHarmony(brass, hB, B + 16, "Eb5!:e r:e G5!:e r:e Bb5!.:q r:q", 4, 0, 0.94);
    blockHarmony(brass, hB, B + 24, "D6!:e r:e Bb5:e D6:e  E6!:e r:e C6:e E6:e", 3, -12, 0.9);
    e = blockHarmony(brass, hB, B + 28, "Bb5!:e Bb5:e Bb5:e Bb5:e A5!:q r:q", 4, 0, 0.96);
    expectBeat(e, A3, "menu B brass");
    // A3: brass doubles the lead an octave down in 3-part harmony
    e = blockHarmony(brass, h3, A3, ante, 3, -12, 0.8);
    e = blockHarmony(
        brass, h3, e,
        "D6:qd C6:e Bb5:q F5:q  G5:e B5:e D6:q F6:qd r:e  Eb6:q D6:e C6:e C6:e A5:e F5:e A5:e", 3,
        -12, 0.8);
    blockHarmony(brass, h3, e, "[Ab4,C5,Eb5]!.:qd r:e [Bb4,D5,F5]!:q r:q", 1, 0, 1.0);
    // A4 (key up): brass doubles again, louder; final hit and link stabs
    e = blockHarmony(brass, h4, A4, ante, 3, -10, 0.86);
    e = blockHarmony(brass, h4, e, cons1, 3, -10, 0.86);
    blockHarmony(brass, h4, e, "[G4,C5,E5]!:qd r:e r:h", 1, 0, 1.0);
    compChords(brass, hL, 55, 72, "x.....x...x.....", 4, 0.8, 0.6, 0.25, false);

    // ---- strings ----
    Patch strP;
    strP.inst = Inst::Strings;
    strP.bright = 0.55;
    strP.width = 0.8;
    strP.attack = 0.18;
    Track& str = s.add("strings", strP);
    str.gain_db = -7.0;
    str.rev = 0.3;
    str.hp = 180;
    str.duck = 0.3;
    str.human_ms = 0;
    padChords(str, hB, 62, 79, 4, 0.55);
    padChords(str, h3, 62, 79, 4, 0.5);
    padChords(str, h4, 64, 81, 4, 0.62);
    // rising string run into the key change
    str.mel(A3 + 30, "G5:s A5:s Bb5:s C6:s D6:s E6:s F#6:s G6:s", 0, 0.6, 1.0);

    // ---- bass ----
    Patch bassP;
    bassP.inst = Inst::FunkBass;
    bassP.bright = 0.55;
    Track& bass = s.add("bass", bassP);
    bass.gain_db = 0.5;
    bass.rev = 0.02;
    bass.human_ms = 3;
    bass.human_vel = 0.04;
    bassLine(bass, hI, 34, "R-RORORA", 0.5, 0.9, 0.8);
    bassLine(bass, h1, 34, "ROROROR5", 0.5, 0.85, 0.75);
    bassLine(bass, h2, 34, "ROROROR5", 0.5, 0.88, 0.75);
    bassLine(bass, hB, 34, "R.RORO5A", 0.5, 0.88, 0.8);
    bassLine(bass, h3, 34, "ROROROR5", 0.5, 0.9, 0.75);
    bassLine(bass, h4, 36, "ROROROR5", 0.5, 0.92, 0.75);
    bassLine(bass, hL, 36, "R.RORORA", 0.5, 0.9, 0.8);

    // ---- muted guitar chicken-scratch ----
    Patch gtrP;
    gtrP.inst = Inst::MutedGuitar;
    gtrP.bright = 0.8;
    Track& gtr = s.add("scratch_guitar", gtrP);
    gtr.gain_db = 3;
    gtr.pan = 0.55;
    gtr.rev = 0.08;
    gtr.hp = 300;
    gtr.human_ms = 3;
    compChords(gtr, cat({h1, h2}), 62, 74, ".gx.g.x..gx.g.x.", 3, 0.7, 0.4);
    compChords(gtr, cat({h3, h4}), 62, 76, ".gx.g.x..gx.g.xg", 3, 0.75, 0.4);

    // ---- glockenspiel sparkle on the final chorus ----
    Patch glP;
    glP.inst = Inst::Glock;
    glP.bright = 0.5;
    Track& gl = s.add("glock", glP);
    gl.gain_db = -5;
    gl.pan = -0.35;
    gl.rev = 0.25;
    gl.dly = 0.1;
    gl.mel(A4, ante, 14, 0.45, 0.9);

    // ---- timpani ----
    Patch tP;
    tP.inst = Inst::Timpani;
    tP.bright = 0.5;
    Track& timp = s.add("timpani", tP);
    timp.gain_db = -3;
    timp.rev = 0.25;
    timp.hp = 35;
    timp.n(I, 2, 46, 1.0).n(I + 8, 2, 46, 0.9).n(I + 12, 1, 41, 0.85).n(I + 14, 1, 41, 0.9);
    timp.n(A3 + 28, 1.5, 44, 0.95).n(A3 + 30, 1.5, 46, 1.0).n(A4, 2, 48, 1.0);
    for (int k = 0; k < 8; ++k)
        timp.n(LK + 6 + k * 0.25, 0.25, 41, 0.45 + 0.07 * k);

    // ---- drums ----
    Patch kP;
    kP.inst = Inst::Kick;
    kP.bright = 0.6;
    Track& kick = s.add("kick", kP);
    kick.gain_db = -7;
    kick.rev = 0.0;
    kick.human_ms = 1;
    kick.duck_source = true;
    Patch cP;
    cP.inst = Inst::Clap;
    Track& clap = s.add("clap", cP);
    clap.gain_db = -0.5;
    clap.rev = 0.18;
    clap.human_ms = 2;
    Patch snP;
    snP.inst = Inst::Snare;
    snP.bright = 0.6;
    Track& snare = s.add("snare", snP);
    snare.gain_db = -5;
    snare.rev = 0.15;
    snare.human_ms = 2;
    Patch hcP;
    hcP.inst = Inst::HatC;
    hcP.bright = 0.5;
    Track& hatc = s.add("hat_closed", hcP);
    hatc.gain_db = 2;
    hatc.pan = 0.3;
    hatc.rev = 0.05;
    hatc.human_ms = 2;
    Patch hoP;
    hoP.inst = Inst::HatO;
    hoP.decay = 0.7;
    Track& hato = s.add("hat_open", hoP);
    hato.gain_db = -1;
    hato.pan = 0.3;
    hato.rev = 0.08;
    hato.human_ms = 2;
    Patch taP;
    taP.inst = Inst::Tamb;
    Track& tamb = s.add("tambourine", taP);
    tamb.gain_db = -6;
    tamb.pan = -0.45;
    tamb.rev = 0.08;
    tamb.human_ms = 3;
    Patch cbP;
    cbP.inst = Inst::Cowbell;
    Track& cow = s.add("cowbell", cbP);
    cow.gain_db = -7;
    cow.pan = -0.3;
    cow.rev = 0.1;
    Patch crP;
    crP.inst = Inst::Crash;
    crP.decay = 1.0;
    Track& crash = s.add("crash", crP);
    crash.gain_db = -3;
    crash.pan = -0.2;
    crash.rev = 0.15;
    crash.human_ms = 0;
    Patch swP;
    swP.inst = Inst::Swell;
    swP.bright = 0.6;
    Track& swell = s.add("cymbal_swell", swP);
    swell.gain_db = -2.5;
    swell.rev = 0.2;
    swell.human_ms = 0;
    Patch tmP;
    tmP.inst = Inst::Tom;
    Track& toms = s.add("toms", tmP);
    toms.gain_db = -8;
    toms.rev = 0.15;
    toms.pan = 0.1;

    for (int bar = 0; bar < s.bars; ++bar) {
        double b = bar * 4.0;
        bool inB = b >= B && b < A3;
        bool kicksBar = b == A3 + 28;
        if (!kicksBar) {
            kick.pat(b, 1, "x...x...x...x...", 60, 0.25, 1.0);
            clap.pat(b, 1, "....x.......x...", 60, 0.25, b < A1 ? 0.8 : 0.9);
            snare.pat(b, 1, "....x.......x...", 60, 0.25, 0.7);
            hatc.pat(b, 1, "xg.gxg.gxg.gxg.g", 60, 0.25, 0.8);
            if (b >= A1)
                hato.pat(b, 1, "..x...x...x...x.", 60, 0.25, 0.75);
        }
        if (b >= A3 && !kicksBar)
            tamb.pat(b, 1, "g.x.g.x.g.x.g.x.", 60, 0.25, 0.8);
        if (inB)
            cow.pat(b, 1, "x..x..x...x.x...", 60, 0.25, 0.7);
    }
    for (double c : {I, A1, A2, B, A3, A4, LK})
        crash.n(c, 1, 60, 0.9);
    // fills
    for (int k = 0; k < 4; ++k)
        snare.n(A1 - 1 + k * 0.25, 0.25, 60, 0.45 + 0.12 * k);
    for (int k = 0; k < 4; ++k)
        snare.n(A2 - 1 + k * 0.25, 0.25, 60, 0.5 + 0.12 * k);
    {
        const int tp[8] = {55, 55, 52, 52, 48, 48, 45, 45};
        for (int k = 0; k < 8; ++k)
            toms.n(B - 2 + k * 0.25, 0.25, tp[k], 0.65 + 0.04 * k);
    }
    for (int k = 0; k < 16; ++k)
        snare.n(A3 - 4 + k * 0.25, 0.25, 60, 0.25 + 0.045 * k);
    swell.n(A3 - 4, 4, 60, 0.7);
    // A3 bar 8 "kicks" on the Ab and Bb hits, then a fill into the key change
    for (double h : {A3 + 28, A3 + 30}) {
        kick.n(h, 1, 60, 1.0);
        crash.n(h, 1, 60, 0.85);
        snare.n(h, 1, 60, 0.9);
        clap.n(h, 1, 60, 0.8);
    }
    for (int k = 0; k < 4; ++k)
        snare.n(A3 + 31 + k * 0.25, 0.25, 60, 0.55 + 0.1 * k);
    swell.n(A3 + 28, 4, 60, 0.8);
    for (int k = 0; k < 4; ++k)
        snare.n(A4 + 31 + k * 0.25, 0.25, 60, 0.5 + 0.12 * k);
    {
        const int tp[8] = {57, 57, 53, 53, 50, 50, 45, 45};
        for (int k = 0; k < 8; ++k)
            toms.n(LK + 6 + k * 0.25, 0.25, tp[k], 0.65 + 0.045 * k);
    }
    swell.n(LK + 4, 4, 60, 0.6);
    return s;
}

} // namespace ps
