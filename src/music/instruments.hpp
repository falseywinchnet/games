#pragma once
// The instruments a band plays, each synthesized sample by sample from a few oscillators,
// filters and noise: no recordings. A voice is one sounding note; play() gives its next
// sample and says when it has finished.
#include "music_core.hpp"

#include <cstdint>

namespace games::music {

enum class Instrument : std::uint8_t {
    marimba,       // a soft mallet on a rosewood bar over its tube: the tone, the overtone two octaves up, a knock
    comp_marimba,  // the same bar, a second player comping
    bass_marimba,  // a deep wooden plunk, the tube booming under it
    squeeze,       // a squeezebox: two reeds a hair apart, the bellows' pulse
    kick,          // soft, under the bass
    rim,           // a stick across the rim: a dry, high knock
    hat,           // the hi-hat's foot: a soft chick
    shaker,
    conga,         // a hand on a tight skin: a ring and a slap
    bird,          // a call from the trees: a quick warble
    // the lounge bands' (styles/lounge_band.hpp)
    vibes,           // a vibraphone, motor on: aluminium bars over tubes, the fan's tremolo
    glock,           // glockenspiel: small steel bars, bright and short
    steel_pan,       // a tenor pan: the note's octave and twelfth ring with it, a little "bwong"
    piano,           // a hammer on stretched strings, the damper falling when the key is let go
    guitar,          // nylon strings plucked by the thumb and fingers
    ukulele,         // four short nylon strings, brighter and quicker than the guitar
    upright,         // the double bass plucked: a round thump that blooms and fades
    electric_bass,   // a fingered electric bass: rounder, longer
    electric_piano,  // a tine piano: the bell of the tine, the warm bark of a firm touch
    organ,           // drawbars (16', 8', 5 1/3', 4', 2 2/3', 2'), percussion, a rotating speaker
    brass,           // a small brass section in unison: bright as it speaks, then mellow
    sax,             // a soft tenor saxophone: breathy, scooping into its notes, a late vibrato
    clarinet,        // the chalumeau's hollow odd harmonics, a little breath
    lap_steel,       // a steel guitar: sliding up into each note, a slow wide vibrato
    musette,         // an accordion's three reeds tuned apart: the Paris café's shimmer
    brush_sweep,     // a wire brush swept round the snare head
    brush_tap,       // the brush tapped on the head
    snare,           // a stick on the snare: a crack and its wires
    ride,            // the ride cymbal: a bell-ish ping over a wash
    clave,           // two rosewood sticks
    cowbell,         // the cencerro
    guiro,           // a stick drawn along a ridged gourd
    timbale,         // a stick on the timbale's shell and head
    clap,            // hands, a few at once
    // the written songs' (styles/song_band.hpp)
    synth_lead,      // a monophonic synthesizer: saw and square through a four-pole filter that opens as it speaks
    synth_bass,      // the same machine down low: a round, plucked bass line
    open_hat,        // the hi-hat struck open, then let ring
    crash,           // the crash cymbal
    tom,             // a tom, pitched by its note
};
constexpr int instrument_count = static_cast<int>(Instrument::tom) + 1;

// A note waiting for its moment.
struct Note {
    long at = 0;               // sample
    Instrument kind = Instrument::marimba;
    double midi = 0, vel = 0;
    double length = 0;         // seconds held (0: let it ring)
    double pan = 0;
};

// A sounding note.
struct Voice {
    Instrument kind = Instrument::marimba;
    bool on = false;
    double hz = 0;
    double phase[8]{};
    double age = 0, length = 0;
    double vel = 0, pan = 0;
    double env = 0;
    double lp = 0, lp2 = 0, lp3 = 0;
    double ratio[8]{};           // partials' frequencies over the note's
    double amp[8]{}, fall[8]{};  // partials' levels, and what each is multiplied by a sample
    double aux = 0;              // an instrument's own: a slide's pitch, a strike's bend
    double lp4 = 0;
};

// The voice's next sample; it turns itself off when it has died away.
double play(Voice& v, Random& noise, double dt) noexcept;
// The lounge bands' instruments (instruments_lounge.cpp): the same, for kinds from vibes on.
double play_lounge(Voice& v, Random& noise, double dt) noexcept;

}  // namespace games::music
