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
};
constexpr int instrument_count = static_cast<int>(Instrument::bird) + 1;

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
    double phase[4]{};
    double age = 0, length = 0;
    double vel = 0, pan = 0;
    double env = 0;
    double lp = 0, lp2 = 0;
};

// The voice's next sample; it turns itself off when it has died away.
double play(Voice& v, Random& noise, double dt) noexcept;

}  // namespace games::music
