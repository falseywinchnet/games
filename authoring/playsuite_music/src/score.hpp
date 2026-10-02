// PlaySuite music synthesizer: score data model and notation helpers.
#pragma once
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ps {

enum class Inst {
    // pitched, polyphonic
    Brass,
    EP,
    Clav,
    Organ,
    Strings,
    Pad,
    Choir,
    FMBell,
    Glock,
    Celesta,
    MusicBox,
    Vibes,
    Marimba,
    Guitar,
    MutedGuitar,
    Harp,
    Pizz,
    UprightBass,
    Piano,
    PluckSynth,
    BellLead,
    Timpani,
    FunkBass,
    SlapBass,
    SubBass,
    SynthBass,
    Kalimba,
    // monophonic lines (portamento / legato)
    Lead,
    Flute,
    Clarinet,
    MutedTrumpet,
    Whistle,
    GlideBass,
    // percussion (pitch selects variation where meaningful)
    Kick,
    Snare,
    Clap,
    HatC,
    HatO,
    Ride,
    Crash,
    Swell,
    Tom,
    Shaker,
    Tamb,
    Cowbell,
    Triangle,
    Rim,
    WoodBlock,
    Brush,
    BrushSweep,
    Snap,
    Clave,
    // textures
    Water,
    Air,
    Bird,
};

bool isMono(Inst i);

struct Patch {
    Inst inst = Inst::Piano;
    double bright = 0.5; // timbre brightness 0..1
    double decay = 1.0;  // decay-time multiplier
    double attack = -1;  // seconds; <0 selects instrument default
    double release = -1; // seconds; <0 selects instrument default
    double detune = 1.0; // unison detune multiplier
    double width = 0.5;  // stereo spread of internal voices 0..1
    double vib = 1.0;    // vibrato depth multiplier
    double glide = 0.05; // portamento time constant (mono lines)
    double param = 0.0;  // instrument-specific (vowel, tremolo depth, registration...)
};

struct Note {
    double beat = 0, dur = 1, pitch = 60, vel = 0.8;
    double pan = 0;     // added to track pan
    bool glide = false; // mono lines: slide from previous note without retrigger
    bool exact = false; // no swing / humanize
};

struct Track {
    std::string name;
    Patch p;
    std::vector<Note> notes;
    double gain_db = 0, pan = 0;
    double rev = 0.12, dly = 0.0; // send levels (linear)
    double hp = 0, lp = 0;        // track filters (Hz, 0 = off)
    double human_ms = 5, human_vel = 0.05;
    double duck = 0; // sidechain ducking depth from duck sources (0..1)
    bool duck_source = false;
    bool bed = false; // circular noise bed (equal-power crossfaded across the seam)
    double eq_low_db = 0, eq_high_db = 0;   // shelves at 200 Hz / 5 kHz
    double eq_mid_db = 0, eq_mid_hz = 1000; // peaking cut/boost

    Track() = default;
    Track(std::string n, Patch patch) : name(std::move(n)), p(patch) {}

    Track& n(double beat, double dur, double pitch, double vel = 0.8, double pan = 0) {
        Note x;
        x.beat = beat;
        x.dur = dur;
        x.pitch = pitch;
        x.vel = vel;
        x.pan = pan;
        notes.push_back(x);
        return *this;
    }
    // Melody notation: tokens "C5:1.5", "Bb4!:e", "r:q", "[C4 E4 G4]:h", "~D5:e" (glide), "F#5.:s"
    // (staccato) durations: number of beats or w h q e s t(=triplet eighth) with optional 'd'
    // (dotted) / 't' (triplet) marks: '!' accent, '\'' soft, '.' staccato, '_' legato/tenuto, '~'
    // prefix glide
    double mel(double beat, const std::string& text, double transpose = 0, double vel = 0.8,
               double gate = 0.9);
    // Percussion grid: one character per step; X=1.0 x=.8 o=.6 g=.32 '.'/'-' rest; spaces ignored.
    double pat(double beat, int bars, const std::string& pattern, double pitch = 60,
               double step = 0.25, double vel = 1.0, int beats_per_bar = 4);
};

// ---- Harmony helpers ----
struct Chord {
    int root = 0;           // pitch class
    std::vector<int> tones; // intervals above root (0 included)
    int bass = -1;          // slash bass pitch class (-1 = root)
    std::string name;
    int bassPc() const {
        return bass >= 0 ? bass : root;
    }
};
Chord chord(const std::string& symbol);
int pitchClass(const std::string& name); // "Bb" -> 10
double noteNum(const std::string& name); // "Bb4" -> 70

struct ChordAt {
    double beat, dur;
    Chord c;
};
// "Bb:4 Gm7:2 C7:2" -> list starting at beat; returns total via last element
std::vector<ChordAt> prog(double beat, const std::string& text, int transpose = 0);

struct Song {
    std::string id, title;
    long spb = 24000; // samples per beat (integer -> exact bar lengths)
    int bars = 32, bpb = 4;
    double swing = 0; // eighth-note swing: offbeat delay as fraction of an eighth (0.33 ~ triplet)
    double swing16 = 0; // sixteenth swing
    std::vector<std::pair<std::string, int>> sections;
    std::vector<Track> tracks;
    double rev_size = 0.7, rev_rt60 = 2.2, rev_damp = 0.5, rev_predelay = 0.02;
    double dly_beats = 0.75, dly_fb = 0.35, dly_lp = 4500;
    double comp_ratio = 2.0,
           comp_thresh_db = 2.0; // bus compression threshold relative to the mix RMS (dB)
    double lufs = -18.0;
    bool loop = true;
    double tail = 8.0;      // seconds rendered beyond the end (folded for loops)
    double max_seconds = 0; // non-looping cues: hard cap with a smooth fade (0 = none)
    double master_hp = 30;
    unsigned seed = 1;
    std::string description;
    std::vector<ChordAt> chords; // harmonic plan, used by the lint self-check

    double bpm() const {
        return 48000.0 * 60.0 / double(spb);
    }
    long loopSamples() const {
        return long(bars) * bpb * spb;
    }
    // Track references stay valid while composing: capacity is reserved up front (max 64 tracks).
    Track& add(const std::string& name, Patch p) {
        if (tracks.capacity() < 64)
            tracks.reserve(64);
        if (tracks.size() >= 64)
            throw std::runtime_error("too many tracks");
        tracks.emplace_back(name, p);
        return tracks.back();
    }
};

// Choose samples-per-beat for a target tempo.
inline long spbFor(double bpm) {
    return long(48000.0 * 60.0 / bpm + 0.5);
}

// Voice-led chord: notes within [lo, hi], closest to previous voicing (updated in place)
std::vector<int> voice(const Chord& c, int lo, int hi, std::vector<int>& prev, int count = 4,
                       bool omitRoot = false);
// Lowest instance of the chord bass at or above lo
int bassNote(const Chord& c, int lo);
int nearestPc(int pc, int around);

} // namespace ps
