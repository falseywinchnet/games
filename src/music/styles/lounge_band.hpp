#pragma once
// Ten lounge bands: the light music of lifts, lobbies, hold lines and game shows, forked
// from the marimba band (marimba_band.hpp) and making itself up the same way. Each plays
// four-bar phrases from the same grammar of progressions, ending open, deceptively or at
// home; a motif (a rhythm and a contour over chord tones) stated, answered, stated again
// and closed on a note that suits the cadence; now and then a phrase at half time or a
// part crossing the rest at half its pace; players a few milliseconds loose and a tempo
// that breathes. Unlike the marimba band they hold one key, G major, and never travel.
//
// What changes from band to band is who plays and how:
//   bossa_lounge      vibes over a nylon guitar's bossa batida, upright bass, rim and shaker
//   smooth_jazz       a soft tenor sax over electric piano, fingered bass, cross-stick and hats
//   swing_combo       clarinet over swing piano, a walking bass, brushes and ride, vibes answering
//   game_show         a bright brass section in thirds, glockenspiel, piano stabs, bouncing
//                     octave bass, a backbeat and claps
//   cocktail_organ    drawbar organ over a guitar chopping four to the bar, bass and brushes
//   steel_calypso     steel pans, the tune rolled and the off-beats strummed, calypso bass,
//                     cowbell, guiro and congas
//   on_hold           electric piano and glockenspiel over muted guitar, pumping bass and claps
//   hawaiian_exotica  lap steel sliding in sixths over a ukulele's strum, bass in two, vibes
//   musette_waltz     an accordion in three-reed musette, in three, over oom-pah-pah
//   cha_cha           marimba over a piano montuno, tumbao bass, guiro, cowbell and timbales
#include "band.hpp"

#include <array>
#include <cstdint>

namespace games::music {

enum class LoungeStyle : std::uint8_t {
    bossa_lounge, smooth_jazz, swing_combo, game_show, cocktail_organ,
    steel_calypso, on_hold, hawaiian_exotica, musette_waltz, cha_cha,
};

class LoungeBand final : public Band {
  public:
    LoungeBand(std::uint32_t seed, LoungeStyle style);

    // How the phrase now playing ends; or a rest with no tune.
    enum class Phrase : std::uint8_t { half, deceptive, home, open, rest };
    // For tools and tests.
    [[nodiscard]] Phrase phrase() const { return phrase_; }
    [[nodiscard]] long bars() const { return bars_; }

    // Who plays the tune and how (one per style, lounge_band.cpp).
    struct Lead {
        Instrument kind;
        int floor;            // the tune's lowest note
        double vel;
        double pan;
        double legato;        // a note sounds this share of its time (0: short, struck)
        bool rolls;           // long notes rolled, as on a marimba or a pan
        int harmony;          // a line under the tune: 0 none, 3 a third below, 6 a sixth below
        double harmony_odds;  // of a phrase having it
        Instrument answer;    // who may take the answer (bar 1); the lead itself for none
        double answer_odds;
        Instrument doubled;   // who may double the tune an octave up; the lead for none
        double doubled_odds;
    };

  private:
    void plan() noexcept override;
    // `beat` in quarters from the bar's start; swung when the style swings.
    void at(double beat, Instrument kind, double midi, double vel, double length = 0, double pan = 0) noexcept;
    void roll(double beat, Instrument kind, double midi, double vel, double length, double pan) noexcept;
    void plan_phrase() noexcept;
    void plan_bar() noexcept;
    void tune_bar(int bar, Instrument kind, int floor, double vel, double pan) noexcept;
    void tune_note(double beat, double midi, double vel, double length, Instrument kind, double pan) noexcept;
    [[nodiscard]] double below(double midi, int interval) const noexcept;
    [[nodiscard]] const Chord& chord_at(int bar, double beat) const noexcept;
    [[nodiscard]] double tone_at(int bar, int floor, double beat, int index) const noexcept { return chord_tone(chord_at(bar, beat), index, floor); }
    // A chord's tones `from`..`to` (0 root .. 4 ninth) at or above `floor`, a little spread.
    void chord(double beat, Instrument kind, const Chord& c, int floor, int from, int to, double vel, double length, double spread, double pan) noexcept;
    // A strum across a chord's tones, down (low to high) or up.
    void strum(double beat, Instrument kind, const Chord& c, int floor, bool down, double vel, double length, double pan) noexcept;
    // The bass: the chord's root or fifth near the bottom of the bass's range.
    [[nodiscard]] double bass_note(const Chord& c, int index) const noexcept;
    void walk(int bar, Instrument kind, double vel) noexcept;
    // The rhythm sections, one a style: bass, comping and drums for one bar.
    void bossa(int bar, int two) noexcept;
    void smooth(int bar, int two) noexcept;
    void swing(int bar, int two) noexcept;
    void show(int bar, int two) noexcept;
    void cocktail(int bar, int two) noexcept;
    void calypso(int bar, int two) noexcept;
    void hold_line(int bar, int two) noexcept;
    void exotica(int bar, int two) noexcept;
    void waltz(int bar, int two) noexcept;
    void cha_cha(int bar, int two) noexcept;

    LoungeStyle style_;
    Lead lead_;
    int beats_ = 4;                   // quarters a bar (the waltz's bar is two of its measures)
    double swing_ = .5;               // where an off-beat eighth falls in its beat (.5 straight)
    Phrase phrase_ = Phrase::rest;
    int key_ = 7;                     // G, held
    int since_home_ = 0;
    std::array<Chord, 8> chords_{};   // this phrase's four bars, two halves each
    int bar_ = 4;
    std::array<int, 7> rhythm_{};
    int rhythm_notes_ = 0;
    std::array<int, 7> contour_{};
    int motif_phrases_ = 0;
    bool harmony_ = false, answer_ = false, doubled_ = false;
    double beat_ = .5;
    double base_beat_ = .5;
    double drift_ = 0;
    bool half_all_ = false, half_bass_ = false, half_comp_ = false, half_tune_ = false;
    long next_bar_ = 0;
    long bars_ = 0;
};

}  // namespace games::music
