// Win / top-score stingers (2-5 s, non-looping, natural decay) in the style of each game's loop.
#include "songs.hpp"

namespace ps {

namespace {
Song base(const std::string& id, const std::string& title, double bpm, int bars) {
    Song s;
    s.id = id;
    s.title = title;
    s.spb = spbFor(bpm);
    s.bars = bars;
    s.loop = false;
    s.tail = 3.0;
    s.lufs = -18.0;
    s.comp_ratio = 1.5;
    s.max_seconds = 4.9;
    return s;
}
Patch P(Inst i, double bright = 0.5) {
    Patch p;
    p.inst = i;
    p.bright = bright;
    return p;
}
} // namespace

// ---- Klondike (F, swing jazz) ----
static Song winKlondike() {
    Song s = base("stinger_win_klondike", "Klondike win", 90, 1);
    s.rev_rt60 = 1.8;
    s.swing = 0.3;
    Track& rh = s.add("piano_rh", P(Inst::Piano, 0.5));
    rh.rev = 0.25;
    rh.human_ms = 6;
    rh.mel(0, "A4:e C5:e F5:e A5:e C6:h", 0, 0.75, 1.6);
    Track& lh = s.add("piano_lh", P(Inst::Piano, 0.45));
    lh.rev = 0.25;
    lh.gain_db = -2;
    lh.mel(2, "[F2,E3,A3,C4,G4]:h", 0, 0.65, 1.0);
    Track& vib = s.add("vibes", P(Inst::Vibes, 0.5));
    vib.rev = 0.3;
    vib.pan = 0.3;
    vib.gain_db = -3;
    vib.mel(2.5, "A5:e F6:hd", 0, 0.6, 1.0);
    Track& ub = s.add("bass", P(Inst::UprightBass, 0.4));
    ub.gain_db = 1;
    ub.n(2, 2, 29, 0.85);
    Track& ride = s.add("ride", P(Inst::Ride));
    ride.gain_db = -9;
    ride.pan = 0.35;
    ride.n(2, 1, 60, 0.7);
    Track& br = s.add("brush", P(Inst::BrushSweep));
    br.gain_db = -5;
    br.n(0, 2, 60, 0.6);
    return s;
}

// ---- Spider (D minor -> D major) ----
static Song winSpider() {
    Song s = base("stinger_win_spider", "Spider win", 100, 2);
    s.rev_rt60 = 1.8;
    Track& pz = s.add("pizz", P(Inst::Pizz, 0.6));
    pz.rev = 0.25;
    pz.pan = -0.25;
    pz.mel(0, "D4.:e F4.:e A4.:e D5.:e", 0, 0.85);
    pz.mel(2, "[D3,A3,D4,F#4]:q", 0, 0.65);
    Track& cl = s.add("clarinet", P(Inst::Clarinet, 0.5));
    cl.rev = 0.25;
    cl.pan = 0.15;
    cl.mel(2, "F#5:hd", 0, 0.8, 1.0);
    Track& ce = s.add("celesta", P(Inst::Celesta, 0.6));
    ce.rev = 0.35;
    ce.dly = 0.15;
    ce.pan = 0.35;
    ce.gain_db = -1;
    ce.mel(2, "A5:e D6:e F#6:q A6:h", 0, 0.7);
    Track& st = s.add("strings", P(Inst::Strings, 0.4));
    st.gain_db = -8;
    st.rev = 0.35;
    st.hp = 150;
    st.mel(2, "[D4,F#4,A4,D5]:h", 0, 0.55, 1.0);
    Track& tri = s.add("triangle", P(Inst::Triangle));
    tri.gain_db = -10;
    tri.pan = 0.5;
    tri.n(2, 1, 60, 0.6);
    s.dly_beats = 0.75;
    return s;
}

// ---- FreeCell (C, piano + strings) ----
static Song winFreecell() {
    Song s = base("stinger_win_freecell", "FreeCell win", 84, 1);
    s.rev_rt60 = 2.2;
    s.rev_size = 0.85;
    Track& pn = s.add("piano", P(Inst::Piano, 0.5));
    pn.rev = 0.25;
    pn.human_ms = 4;
    pn.mel(0, "C4:s E4:s G4:s D5:s E5:s G5:s C6:s E6:s G6:h", 0, 0.65, 2.5);
    pn.mel(2, "[C2,C3]:h", 0, 0.6, 1.0);
    Track& st = s.add("strings", P(Inst::Strings, 0.45));
    st.gain_db = -4;
    st.rev = 0.35;
    st.hp = 140;
    {
        Patch p = st.p;
        p.attack = 0.25;
        st.p = p;
    }
    st.mel(1, "[C4,G4,C5,E5]:hd", 0, 0.6, 1.0);
    return s;
}

// ---- Hearts (G minor, sly swing) ----
static Song winHearts() {
    Song s = base("stinger_win_hearts", "Hearts win", 110, 2);
    s.swing = 0.36;
    s.rev_rt60 = 1.4;
    Track& tp = s.add("muted_trumpet", P(Inst::MutedTrumpet, 0.5));
    tp.rev = 0.22;
    tp.pan = 0.1;
    tp.mel(0, "r:e D5.:e G5.:e Bb5.:e A5.:e F#5.:e G5!:h", 0, 0.85, 0.95);
    Track& cl = s.add("clarinet", P(Inst::Clarinet, 0.45));
    cl.rev = 0.22;
    cl.pan = -0.3;
    cl.gain_db = -3;
    cl.mel(3, "Bb4:h", 0, 0.7, 1.0);
    Track& pn = s.add("piano", P(Inst::Piano, 0.6));
    pn.rev = 0.2;
    pn.gain_db = -3;
    pn.mel(3, "[G2,D3,G3,Bb3,E4]:h", 0, 0.7, 1.0);
    Track& ub = s.add("bass", P(Inst::UprightBass, 0.5));
    ub.n(3, 2, 31, 0.8);
    Track& snap = s.add("snap", P(Inst::Snap));
    snap.gain_db = -6;
    snap.pan = 0.4;
    snap.mel(0, "r:q C4:q r:q", 0, 0.75);
    Track& br = s.add("brush", P(Inst::Brush));
    br.gain_db = -4;
    br.n(3, 1, 60, 0.8);
    Track& cr = s.add("cymbal", P(Inst::Ride));
    cr.gain_db = -9;
    cr.pan = 0.3;
    cr.n(3, 1, 60, 0.75);
    return s;
}

// ---- Sudoku (D major, calm) ----
static Song winSudoku() {
    Song s = base("stinger_win_sudoku", "Sudoku win", 76, 1);
    s.rev_rt60 = 2.4;
    s.rev_size = 0.9;
    s.dly_beats = 1.5;
    Track& ce = s.add("celesta", P(Inst::Celesta, 0.45));
    ce.rev = 0.35;
    ce.dly = 0.15;
    ce.pan = 0.25;
    ce.mel(0, "D5:e F#5:e A5:e D6:e F#6:h", 0, 0.7);
    Track& pn = s.add("piano", P(Inst::Piano, 0.3));
    pn.rev = 0.3;
    pn.gain_db = -2;
    pn.mel(2, "[D3,A3,C#4,E4,F#4]:h", 0, 0.55, 1.0);
    Track& pad = s.add("pad", P(Inst::Pad, 0.3));
    pad.gain_db = -9;
    pad.rev = 0.4;
    pad.hp = 200;
    {
        Patch p = pad.p;
        p.attack = 0.6;
        p.release = 1.2;
        pad.p = p;
    }
    pad.mel(0.5, "[A4,D5,F#5]:hd", 0, 0.5, 1.0);
    return s;
}
static Song topSudoku() {
    Song s = base("stinger_topscore_sudoku", "Sudoku top score", 76, 2);
    s.rev_rt60 = 2.4;
    s.rev_size = 0.9;
    s.dly_beats = 1.5;
    Track& gl = s.add("glock", P(Inst::Glock, 0.45));
    gl.rev = 0.35;
    gl.dly = 0.12;
    gl.pan = 0.3;
    gl.gain_db = -2;
    gl.mel(0, "A5:e D6:e F#6:e A6:e B6:q A6:q", 0, 0.65);
    Track& ce = s.add("celesta", P(Inst::Celesta, 0.5));
    ce.rev = 0.35;
    ce.pan = -0.2;
    ce.mel(0, "A4:e D5:e F#5:e A5:e D6:q E6:e F#6:e A6:h", 0, 0.72);
    Track& pn = s.add("piano", P(Inst::Piano, 0.35));
    pn.rev = 0.3;
    pn.gain_db = -2;
    pn.mel(3, "[D2,D3,A3,E4,F#4]:h", 0, 0.62, 1.0);
    Track& st = s.add("strings", P(Inst::Strings, 0.45));
    st.gain_db = -5;
    st.rev = 0.4;
    st.hp = 150;
    {
        Patch p = st.p;
        p.attack = 0.3;
        st.p = p;
    }
    st.mel(1, "[F#4,A4,D5,F#5]:h [E4,A4,C#5,E5]:q [F#4,A4,D5,F#5]:h", 0, 0.6, 1.0);
    Track& tri = s.add("triangle", P(Inst::Triangle));
    tri.gain_db = -12;
    tri.pan = 0.5;
    tri.n(3, 1, 60, 0.55);
    return s;
}

// ---- Gems (A major, sparkling pop) ----
static Song winGems() {
    Song s = base("stinger_win_gems", "Gems win", 124, 2);
    s.rev_rt60 = 1.6;
    s.dly_beats = 0.75;
    s.dly_fb = 0.3;
    Track& pl = s.add("pluck", P(Inst::PluckSynth, 0.6));
    pl.rev = 0.15;
    pl.dly = 0.2;
    pl.gain_db = -3;
    pl.hp = 300;
    pl.mel(0, "A4:s C#5:s E5:s A5:s C#6:s E6:s A6:e", 0, 0.7);
    Track& bl = s.add("bell", P(Inst::BellLead, 0.6));
    bl.rev = 0.2;
    bl.dly = 0.2;
    bl.mel(2, "E6:e C#6:e A6:h", 0, 0.85);
    Track& gl = s.add("glock", P(Inst::Glock, 0.6));
    gl.rev = 0.25;
    gl.pan = 0.35;
    gl.gain_db = -4;
    gl.mel(2, "C#7:e A6:e E7:h", 0, 0.6);
    Track& pad = s.add("pad", P(Inst::Pad, 0.55));
    pad.gain_db = -8;
    pad.rev = 0.3;
    pad.hp = 400;
    {
        Patch p = pad.p;
        p.attack = 0.05;
        p.release = 0.8;
        pad.p = p;
    }
    pad.mel(2, "[A4,C#5,E5,A5]:h", 0, 0.6, 1.0);
    Track& bs = s.add("bass", P(Inst::SynthBass, 0.5));
    bs.n(2, 1.5, 33, 0.85);
    Track& k = s.add("kick", P(Inst::Kick, 0.5));
    k.gain_db = -4;
    k.n(2, 1, 60, 1.0);
    Track& c = s.add("clap", P(Inst::Clap));
    c.gain_db = -5;
    c.rev = 0.2;
    c.n(2, 1, 60, 0.85);
    Track& cr = s.add("crash", P(Inst::Crash));
    cr.gain_db = -6;
    cr.n(2, 1, 60, 0.8);
    return s;
}
static Song topGems() {
    Song s = base("stinger_topscore_gems", "Gems top score", 124, 2);
    s.rev_rt60 = 1.7;
    s.dly_beats = 0.75;
    s.dly_fb = 0.3;
    Track& bl = s.add("bell", P(Inst::BellLead, 0.6));
    bl.rev = 0.2;
    bl.dly = 0.18;
    bl.mel(0, "A5:e C#6:e E6:e F#6:e  E6:e A6:e B6:e C#7:hd+e", 0, 0.85);
    Track& gl = s.add("glock", P(Inst::Glock, 0.6));
    gl.rev = 0.25;
    gl.pan = 0.35;
    gl.gain_db = -5;
    gl.mel(2, "A6:e B6:e C#7:e E7:e A7:h", 0, 0.55);
    Track& pl = s.add("pluck", P(Inst::PluckSynth, 0.6));
    pl.rev = 0.15;
    pl.dly = 0.2;
    pl.gain_db = -5;
    pl.hp = 300;
    pl.pan = -0.3;
    pl.mel(0, "A4:s E5:s A5:s E5:s B4:s E5:s B5:s E5:s C#5:s E5:s C#6:s E5:s D5:s F#5:s D6:s F#5:s",
           0, 0.6);
    Track& pad = s.add("pad", P(Inst::Pad, 0.6));
    pad.gain_db = -7;
    pad.rev = 0.3;
    pad.hp = 400;
    {
        Patch p = pad.p;
        p.attack = 0.05;
        p.release = 1.0;
        pad.p = p;
    }
    pad.mel(4, "[A4,C#5,E5,A5,C#6]:h", 0, 0.65, 1.0);
    Track& sw = s.add("riser", P(Inst::Swell, 0.8));
    sw.gain_db = -7;
    sw.n(0, 4, 60, 0.7);
    Track& bs = s.add("bass", P(Inst::SynthBass, 0.5));
    bs.n(4, 1.5, 33, 0.9);
    Track& k = s.add("kick", P(Inst::Kick, 0.5));
    k.gain_db = -4;
    k.n(4, 1, 60, 1.0);
    Track& c = s.add("clap", P(Inst::Clap));
    c.gain_db = -5;
    c.rev = 0.2;
    c.n(4, 1, 60, 0.9);
    Track& cr = s.add("crash", P(Inst::Crash));
    cr.gain_db = -5;
    cr.n(4, 1, 60, 0.85);
    return s;
}

// ---- Nature Cube (Eb lydian, airy) ----
static Song winNature() {
    Song s = base("stinger_win_nature_cube", "Nature Cube win", 72, 1);
    s.rev_rt60 = 2.8;
    s.rev_size = 1.0;
    s.dly_beats = 1.5;
    s.dly_fb = 0.3;
    Track& hp = s.add("harp", P(Inst::Harp, 0.5));
    hp.rev = 0.35;
    hp.pan = -0.25;
    hp.mel(0, "Eb4:s G4:s Bb4:s D5:s F5:s A5:s Bb5:s D6:s", 0, 0.7, 4.0);
    Track& fl = s.add("flute", P(Inst::Flute, 0.5));
    fl.rev = 0.4;
    fl.pan = 0.15;
    fl.mel(2, "F5:e G5:e Bb5:h", 0, 0.75, 1.0);
    Track& ch = s.add("choir", P(Inst::Choir));
    ch.gain_db = -6;
    ch.rev = 0.5;
    ch.hp = 180;
    {
        Patch p = ch.p;
        p.param = 0.8;
        p.attack = 0.5;
        p.release = 1.2;
        ch.p = p;
    }
    ch.mel(1, "[Eb4,G4,Bb4,D5]:hd", 0, 0.55, 1.0);
    Track& ce = s.add("chime", P(Inst::Celesta, 0.4));
    ce.rev = 0.5;
    ce.dly = 0.2;
    ce.pan = 0.4;
    ce.gain_db = -5;
    ce.mel(2, "A6:q D7:h", 0, 0.5);
    return s;
}
static Song topNature() {
    Song s = base("stinger_topscore_nature_cube", "Nature Cube top score", 72, 2);
    s.rev_rt60 = 3.0;
    s.rev_size = 1.0;
    s.dly_beats = 1.5;
    s.dly_fb = 0.3;
    Track& hp = s.add("harp", P(Inst::Harp, 0.55));
    hp.rev = 0.35;
    hp.pan = -0.25;
    hp.mel(0, "Bb3:s Eb4:s G4:s Bb4:s D5:s F5:s A5:s Bb5:s D6:s F6:s G6:s Bb6:s", 0, 0.7, 4.0);
    Track& ka = s.add("kalimba", P(Inst::Kalimba, 0.4));
    ka.rev = 0.4;
    ka.dly = 0.15;
    ka.pan = 0.3;
    ka.mel(3, "Bb5:e D6:e F6:e A6:e", 0, 0.65);
    Track& fl = s.add("flute", P(Inst::Flute, 0.5));
    fl.rev = 0.4;
    fl.pan = 0.1;
    fl.mel(3, "D6:e Eb6:e F6:h", 0, 0.75, 1.0);
    Track& ch = s.add("choir", P(Inst::Choir));
    ch.gain_db = -5;
    ch.rev = 0.5;
    ch.hp = 180;
    {
        Patch p = ch.p;
        p.param = 0.6;
        p.attack = 0.6;
        p.release = 1.4;
        ch.p = p;
    }
    ch.mel(1, "[Eb4,G4,Bb4,D5,F5]:w", 0, 0.6, 1.0);
    Track& pad = s.add("pad", P(Inst::Pad, 0.35));
    pad.gain_db = -8;
    pad.rev = 0.5;
    pad.hp = 150;
    pad.mel(0, "[Eb3,Bb3,F4,A4]:w", 0, 0.5, 1.0);
    Track& bird = s.add("bird", P(Inst::Bird));
    bird.gain_db = -12;
    bird.rev = 0.5;
    bird.pan = 0.6;
    bird.n(4.2, 1, 100, 0.6);
    return s;
}

// ---- Untangle (F, elastic) ----
static Song winUntangle() {
    Song s = base("stinger_win_untangle", "Untangle win", 104, 1);
    s.rev_rt60 = 1.3;
    Track& wh = s.add("whistle", P(Inst::Whistle, 0.5));
    wh.rev = 0.25;
    wh.pan = 0.25;
    {
        Patch p = wh.p;
        p.glide = 0.06;
        wh.p = p;
    }
    wh.mel(0, "C5:e ~C6:e ~F6:h", 0, 0.75, 0.95);
    Track& mm = s.add("marimba", P(Inst::Marimba, 0.55));
    mm.rev = 0.2;
    mm.pan = -0.2;
    mm.mel(1, "F5:s A5:s C6:s F6:s", 0, 0.8);
    mm.mel(2, "[F4,A4,C5,F5]:q", 0, 0.85);
    Track& pz = s.add("pizz", P(Inst::Pizz, 0.6));
    pz.rev = 0.2;
    pz.gain_db = -2;
    pz.mel(2, "[F3,C4,A4]:q", 0, 0.85);
    Track& gb = s.add("bass", P(Inst::GlideBass, 0.5));
    {
        Patch p = gb.p;
        p.glide = 0.04;
        gb.p = p;
    }
    gb.mel(1.5, "C2:e ~F2:q", 0, 0.85, 0.9);
    Track& wb = s.add("woodblock", P(Inst::WoodBlock));
    wb.gain_db = -7;
    wb.pan = 0.45;
    wb.mel(0, "C6:e C6:e r:q G5:q", 0, 0.6);
    return s;
}
static Song topUntangle() {
    Song s = base("stinger_topscore_untangle", "Untangle top score", 104, 2);
    s.rev_rt60 = 1.4;
    Track& wh = s.add("whistle", P(Inst::Whistle, 0.5));
    wh.rev = 0.25;
    wh.pan = 0.25;
    {
        Patch p = wh.p;
        p.glide = 0.06;
        wh.p = p;
    }
    wh.mel(0, "F5:e ~C5:e ~A5:e ~F5:e ~C6:e ~A5:e ~F6:h", 0, 0.75, 0.95);
    Track& mm = s.add("marimba", P(Inst::Marimba, 0.55));
    mm.rev = 0.2;
    mm.pan = -0.2;
    mm.mel(0, "F5:e A5:e C6:e A5:e Bb5:e D6:e F6:e C6:e", 0, 0.75);
    mm.mel(4, "[F4,A4,C5,F5]:q", 0, 0.9);
    Track& pz = s.add("pizz", P(Inst::Pizz, 0.6));
    pz.rev = 0.2;
    pz.gain_db = -2;
    pz.mel(4, "[F3,C4,A4,F5]:q", 0, 0.9);
    Track& gb = s.add("bass", P(Inst::GlideBass, 0.5));
    {
        Patch p = gb.p;
        p.glide = 0.04;
        gb.p = p;
    }
    gb.mel(0, "F2:e ~F3:e C2:e ~C3:e Bb1:e ~Bb2:e C2:e ~E2:e F2:q", 0, 0.85, 0.9);
    Track& clap = s.add("clap", P(Inst::Clap));
    clap.gain_db = -5;
    clap.rev = 0.2;
    clap.n(4, 1, 60, 0.9);
    Track& k = s.add("kick", P(Inst::Kick, 0.4));
    k.gain_db = -6;
    k.n(4, 1, 60, 0.9);
    Track& cow = s.add("cowbell", P(Inst::Cowbell));
    cow.gain_db = -10;
    cow.pan = -0.3;
    cow.mel(0, "C4:e C4:e r:e C4:e r:q C4:q", 0, 0.6);
    return s;
}

// ---- Puzzle Solve (E dorian funk) ----
static Song winPuzzle() {
    Song s = base("stinger_win_puzzle_solve", "Puzzle Solve win", 96, 1);
    s.rev_rt60 = 1.4;
    s.dly_beats = 0.75;
    Track& vb = s.add("vibes", P(Inst::Vibes, 0.6));
    vb.rev = 0.25;
    vb.dly = 0.12;
    vb.pan = 0.15;
    vb.mel(0, "E5:s G5:s B5:s D6:s E6:e F#6:e G6:h", 0, 0.8);
    Track& cl = s.add("clav", P(Inst::Clav, 0.55));
    cl.pan = -0.3;
    cl.gain_db = -3;
    cl.mel(2, "[G4,B4,D5,F#5].:e", 0, 0.85);
    Track& ep = s.add("ep", P(Inst::EP, 0.4));
    ep.gain_db = -5;
    ep.rev = 0.3;
    ep.pan = 0.25;
    ep.mel(2, "[E4,G4,B4,D5,F#5]:h", 0, 0.6, 1.0);
    Track& bs = s.add("bass", P(Inst::FunkBass, 0.5));
    bs.mel(1.5, "B1:e E2:h", 0, 0.85, 0.9);
    Track& k = s.add("kick", P(Inst::Kick, 0.5));
    k.gain_db = -5;
    k.n(2, 1, 60, 0.95);
    Track& sn = s.add("snare", P(Inst::Snare, 0.6));
    sn.gain_db = -5;
    sn.rev = 0.15;
    sn.n(2, 1, 60, 0.85);
    Track& ho = s.add("hat_open", P(Inst::HatO));
    ho.gain_db = -8;
    ho.pan = 0.3;
    ho.n(2, 1, 60, 0.7);
    return s;
}
static Song topPuzzle() {
    Song s = base("stinger_topscore_puzzle_solve", "Puzzle Solve top score", 96, 2);
    s.rev_rt60 = 1.5;
    s.dly_beats = 0.75;
    Track& vb = s.add("vibes", P(Inst::Vibes, 0.6));
    vb.rev = 0.25;
    vb.dly = 0.12;
    vb.pan = 0.15;
    vb.mel(0, "B4:e D5:e E5:e G5:e A5:e B5:e D6:e E6:e  G6:h", 0, 0.8);
    Track& cl = s.add("clav", P(Inst::Clav, 0.55));
    cl.pan = -0.3;
    cl.gain_db = -3;
    cl.mel(0, "[D4,G4,B4].:s r:s [D4,G4,B4]:s r:s r:q [E4,A4,C#5].:s r:s [E4,A4,C#5]:s r:s r:q", 0,
           0.7);
    cl.mel(4, "[G4,B4,D5,F#5].:e", 0, 0.9);
    Track& ep = s.add("ep", P(Inst::EP, 0.45));
    ep.gain_db = -4;
    ep.rev = 0.3;
    ep.pan = 0.25;
    ep.mel(4, "[E4,G4,B4,D5,F#5,A5]:h", 0, 0.65, 1.0);
    Track& bs = s.add("bass", P(Inst::FunkBass, 0.5));
    bs.mel(0, "E2:e r:e E3:e D3:e B2:e r:e A2:e B2:e", 0, 0.8, 0.7);
    bs.n(4, 2, 28, 0.9);
    Track& k = s.add("kick", P(Inst::Kick, 0.5));
    k.gain_db = -5;
    k.mel(0, "C4:q r:e C4:e r:h", 0, 0.8);
    k.n(4, 1, 60, 1.0);
    Track& sn = s.add("snare", P(Inst::Snare, 0.6));
    sn.gain_db = -5;
    sn.rev = 0.15;
    sn.mel(0, "r:q C4:q r:q C4:s C4:s C4:s C4:s", 0, 0.75);
    sn.n(4, 1, 60, 0.9);
    Track& hat = s.add("hat", P(Inst::HatC, 0.6));
    hat.gain_db = -6;
    hat.pan = 0.3;
    hat.pat(0, 1, "xgxgxgxgxgxgxgxg", 60, 0.25, 0.6);
    Track& cr = s.add("crash", P(Inst::Crash));
    cr.gain_db = -7;
    cr.n(4, 1, 60, 0.8);
    return s;
}

std::vector<SongFactory> stingers() {
    return {
        {"stinger_win_klondike", winKlondike},   {"stinger_win_spider", winSpider},
        {"stinger_win_freecell", winFreecell},   {"stinger_win_hearts", winHearts},
        {"stinger_win_sudoku", winSudoku},       {"stinger_topscore_sudoku", topSudoku},
        {"stinger_win_gems", winGems},           {"stinger_topscore_gems", topGems},
        {"stinger_win_nature_cube", winNature},  {"stinger_topscore_nature_cube", topNature},
        {"stinger_win_untangle", winUntangle},   {"stinger_topscore_untangle", topUntangle},
        {"stinger_win_puzzle_solve", winPuzzle}, {"stinger_topscore_puzzle_solve", topPuzzle},
    };
}

} // namespace ps
