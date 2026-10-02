// Development aid: one phrase per instrument, used to calibrate relative output levels.
#include "songs.hpp"
namespace ps {
Song songInstrumentTest() {
    Song s;
    s.id = "instrument_test";
    s.spb = spbFor(120);
    s.loop = false;
    struct I {
        const char* n;
        Inst i;
        int lo;
    };
    std::vector<I> list = {{"ep", Inst::EP, 60},
                           {"clav", Inst::Clav, 60},
                           {"organ", Inst::Organ, 60},
                           {"pad", Inst::Pad, 60},
                           {"choir", Inst::Choir, 60},
                           {"fmbell", Inst::FMBell, 72},
                           {"celesta", Inst::Celesta, 72},
                           {"musicbox", Inst::MusicBox, 72},
                           {"vibes", Inst::Vibes, 65},
                           {"marimba", Inst::Marimba, 60},
                           {"guitar", Inst::Guitar, 52},
                           {"harp", Inst::Harp, 55},
                           {"pizz", Inst::Pizz, 55},
                           {"upright", Inst::UprightBass, 36},
                           {"piano", Inst::Piano, 60},
                           {"pluck", Inst::PluckSynth, 72},
                           {"belllead", Inst::BellLead, 72},
                           {"slap", Inst::SlapBass, 36},
                           {"sub", Inst::SubBass, 36},
                           {"synthbass", Inst::SynthBass, 36},
                           {"kalimba", Inst::Kalimba, 67},
                           {"flute", Inst::Flute, 72},
                           {"clarinet", Inst::Clarinet, 62},
                           {"mtrumpet", Inst::MutedTrumpet, 65},
                           {"whistle", Inst::Whistle, 79},
                           {"glidebass", Inst::GlideBass, 36},
                           {"strings", Inst::Strings, 60},
                           {"brass", Inst::Brass, 60},
                           {"lead", Inst::Lead, 72},
                           {"funkbass", Inst::FunkBass, 36},
                           {"glock", Inst::Glock, 79},
                           {"timpani", Inst::Timpani, 43}};
    double b = 0;
    for (const I& x : list) {
        Patch p;
        p.inst = x.i;
        Track& t = s.add(x.n, p);
        t.rev = 0;
        t.human_ms = 0;
        t.human_vel = 0;
        t.mel(b, "C4:q E4:q G4:q C5:q", x.lo - 60, 0.8, 0.9);
        t.n(b + 4, 2, x.lo, 0.8);
        b += 8;
    }
    struct D {
        const char* n;
        Inst i;
    };
    std::vector<D> drums = {{"kick", Inst::Kick},       {"snare", Inst::Snare},
                            {"clap", Inst::Clap},       {"hatc", Inst::HatC},
                            {"hato", Inst::HatO},       {"ride", Inst::Ride},
                            {"crash", Inst::Crash},     {"tom", Inst::Tom},
                            {"shaker", Inst::Shaker},   {"tamb", Inst::Tamb},
                            {"cowbell", Inst::Cowbell}, {"triangle", Inst::Triangle},
                            {"rim", Inst::Rim},         {"woodblock", Inst::WoodBlock},
                            {"brush", Inst::Brush},     {"snap", Inst::Snap},
                            {"clave", Inst::Clave},     {"bird", Inst::Bird}};
    for (const D& d : drums) {
        Patch p;
        p.inst = d.i;
        Track& t = s.add(d.n, p);
        t.rev = 0;
        t.human_ms = 0;
        t.human_vel = 0;
        double pitch = d.i == Inst::Tom         ? 50
                       : d.i == Inst::WoodBlock ? 84
                       : d.i == Inst::Bird      ? 100
                                                : 60;
        t.pat(b, 2, "x.x.x.x.", pitch, 0.5, 0.8);
        b += 8;
    }
    {
        Patch p;
        p.inst = Inst::Water;
        Track& t = s.add("water", p);
        t.rev = 0;
        t.n(b, 8, 60, 0.8);
        b += 12;
    }
    {
        Patch p;
        p.inst = Inst::Air;
        Track& t = s.add("air", p);
        t.rev = 0;
        t.n(b, 8, 60, 0.8);
        b += 12;
    }
    {
        Patch p;
        p.inst = Inst::BrushSweep;
        Track& t = s.add("brushsweep", p);
        t.rev = 0;
        t.n(b, 2, 60, 0.8);
        t.n(b + 2, 2, 60, 0.8);
        b += 8;
    }
    s.bars = int(b / 4) + 1;
    s.lufs = -18;
    s.comp_ratio = 1.0;
    return s;
}
} // namespace ps
