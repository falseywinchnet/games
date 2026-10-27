#pragma once
// The Paradoxiary's music: a little marimba band playing light jazz in the shade, making
// itself up as it plays, never the same twice and never looping.
//
// It borrows from three casual traditions. From the marimba bands of Central America:
// the tune doubled in parallel thirds now and then, long notes rolled (the mallets
// alternating quickly, since a marimba bar cannot hold a note), a second marimba playing
// the harmony, and a bass marimba's wooden plunk under it all. From marimba and vibes
// jazz: seventh chords and their ii-V motion, the second marimba comping in rootless
// four-mallet voicings (third, fifth, seventh, ninth), lines that aim chord tones on the
// strong beats. From bossa nova: straight eighths at an easy walking pace (about a
// hundred and five beats a minute), the comping in a syncopated two-bar pattern, the bass
// on one and the "and" of two, a rim click instead of a snare, a soft shaker.
//
// The harmony goes somewhere. Phrases are four bars from a grammar of progressions in the
// current key, and they end in different ways: open on the dominant, deceptively on the
// sixth, on a borrowed chord, and only some of the time at home; every few phrases a
// ii-V carries the music into a neighbouring key (a fourth or a fifth either side of where
// it began), so home itself moves. The tune is built from a motif (a rhythm and a contour
// over chord tones) stated, answered, stated again on the new chords, and closed on a note
// that suits the cadence. Now and then a phrase drops to half time, or a part crosses the
// rest at half its pace; the players' timing is a few milliseconds loose and the tempo
// breathes. A squeezebox answers the tune once in a while. No pads, no flute.
#include "band.hpp"

#include <array>
#include <cstdint>

namespace games::music {

class MarimbaBand final : public Band {
  public:
    explicit MarimbaBand(std::uint32_t seed);

    // How the phrase now playing ends: open on the dominant, on the sixth instead of home,
    // at home, on a borrowed chord, or carrying into a new key; or a rest with no tune.
    enum class Phrase : std::uint8_t { half, deceptive, home, open, travel, rest };
    // For tools and tests.
    [[nodiscard]] Phrase phrase() const { return phrase_; }
    [[nodiscard]] int key() const { return key_; }
    [[nodiscard]] long bars() const { return bars_; }

  private:
    void plan() noexcept override;
    // `beat` in quarters from the bar's start
    void at(double beat, Instrument kind, double midi, double vel, double length = 0, double pan = 0) noexcept {
        note(next_bar_, beat * beat_, kind, midi, vel, length * beat_, pan);
    }
    // A note held by rolling it: strokes a little apart for `length` beats.
    void roll(double beat, Instrument kind, double midi, double vel, double length, double pan) noexcept;
    void plan_phrase() noexcept;
    void plan_bar() noexcept;
    void tune_bar(int bar, Instrument lead, double vel, double pan) noexcept;
    // A note of the tune, with its third below when the phrase is in thirds.
    void tune_note(double beat, double midi, double vel, double length, Instrument lead, double pan) noexcept;
    [[nodiscard]] double third_below(double midi) const noexcept;
    [[nodiscard]] const Chord& chord_at(int bar, double beat) const noexcept;
    static double tone_of(const Chord& c, int index, int floor) { return chord_tone(c, index, floor); }
    // The tune's chord tone `index` at `beat` of `bar`, at or above `floor`.
    [[nodiscard]] double tone_at(int bar, int floor, double beat, int index) const noexcept { return tone_of(chord_at(bar, beat), index, floor); }

    Phrase phrase_ = Phrase::rest;
    int key_ = 7;                     // the key's tonic, a pitch class (G)
    int next_key_ = 7;
    int anchor_ = 7;                  // the key the piece began in: travels stay near it
    int key_phrases_ = 0;             // phrases in this key so far
    int since_home_ = 0;              // phrases since one ended at home
    std::array<Chord, 8> chords_{};   // this phrase's four bars, two halves each
    int bar_ = 4;                     // the bar within the phrase
    // the motif: a rhythm in eighths over a bar, and a contour over chord tones
    std::array<int, 7> rhythm_{};
    int rhythm_notes_ = 0;
    std::array<int, 7> contour_{};
    int motif_phrases_ = 0;           // phrases the motif has left before it changes
    bool thirds_ = false;             // this phrase's tune doubled a third below
    bool squeeze_answer_ = false;     // the squeezebox takes this phrase's answer
    double beat_ = .5;                // seconds: a quarter, as it is now
    double base_beat_ = .5;           // and as the piece set out
    double drift_ = 0;                // the tempo's slow wander
    // this phrase's variety: the whole band at half time, or parts at half their pace
    bool half_all_ = false, half_bass_ = false, half_comp_ = false, half_tune_ = false;
    std::int64_t next_bar_ = 0;
    long bars_ = 0;
};

}  // namespace games::music
