#pragma once
// Written songs: a score composed once, note for note, played as written and round
// again for as long as it is wanted. Nothing is made up as it plays; the players are
// only as loose as players are (a few milliseconds, a little in their touch).
//
// A song is sections played in an order (an intro, a theme, a bridge, a tag), then that
// order again from where its loop begins. A section is a number of bars of chords and the
// parts written over them, and the drums it is played with. Parts are written as text:
//
//   chords   "F:4 | Bb7:2 C9sus:2 |"     a symbol and its beats; | marks a bar
//   notes    "C5:.5 D5:.5 E5:1 r:2 |"    a note (or r, a rest) and its beats
//   stabs    "*:1.5 *:1 r:1.5"            * plays the chord, voiced above the part's floor
//   bass     "1:.75 1:.25 5:.5 8:.5"      the chord's root, third, fifth, sixth, seventh, octave
//   arpeggio "^:.5 ^:.5 v:.5"             the next chord tone up, or down
//   drums    "x...x...x...x..."           sixteenths: X loud, x, o a ghost
//
// A part shorter than its section repeats. The chord symbols: C, C6, C7, Cmaj7, C9, Cadd9,
// C69, Cm, Cm6, Cm7, Cm9, Csus, C7sus, C9sus, Cdim7, Cm7b5, any of them over a bass (C/E).
#include "band.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace games::music {

struct SongChord {
    int root = 0;                 // pitch class
    int bass = 0;                 // pitch class in the bass (the root unless written over another)
    std::array<int, 5> tones{};   // semitones above the root
    int count = 3;
};

// A note as written: where in its bar, how long, and what it plays.
struct Written {
    enum class Role : std::uint8_t { pitch, stab, degree, up, down };
    double beat = 0;
    double length = 0;
    Role role = Role::pitch;
    int value = 0;                // the midi note, or the degree (1 3 5 6 7 8)
};

struct SongPart {
    Instrument kind = Instrument::piano;
    double vel = .7, pan = 0;
    double legato = .9;           // a held note sounds this share of its time
    int harmony = 0;              // chord tones added under each note: 0, 1 or 2
    int floor = 60;               // stabs and arpeggios at or above; a bass's lowest root
    std::vector<std::vector<Written>> bars{};
};

struct ChordAt {
    double beat = 0;
    SongChord chord{};
};

struct SongSection {
    std::string name{};
    int bars = 0;
    int shift = 0;                // semitones above the song's key
    std::vector<std::vector<ChordAt>> chords{};
    std::vector<SongPart> parts{};
    int kit = 0;                  // the drums
    bool crash = false;           // a crash on its first beat
    bool fill = false;            // a tom fill through its last beat
    bool stop = false;            // its last bar only a hit on one, then a breath
};

struct DrumLine {
    Instrument kind = Instrument::kick;
    std::string hits{};           // sixteen a bar, or thirty-two for two
    double vel = .7, pan = 0;
    int pitch = 0;                // a pitched drum's note
};

struct MixLevel {
    Instrument kind = Instrument::piano;
    double level = 0, send = 0;
};

struct Song {
    std::string title{};
    double bpm = 118;
    double swing = .5;                     // where an off-beat eighth falls (.5 straight)
    double room_seconds = 1.2, room_dark_hz = 5000;
    std::vector<MixLevel> mix{};
    std::vector<SongSection> sections{};
    std::vector<std::vector<DrumLine>> kits{};
    std::vector<int> order{};
    int loop_from = 0;
    std::vector<std::string> problems{};   // bars that do not add up, symbols not understood
};

// Writing a song.
int add_section(Song& song, const char* name, const char* chords, int shift = 0);
void add_part(Song& song, int section, Instrument kind, const char* notes, double vel, double pan, double legato = .9, int harmony = 0, int floor = 60);
int add_kit(Song& song);
void add_drums(Song& song, int kit, Instrument kind, const char* hits, double vel, double pan = 0, int pitch = 0);
void set_mix(Song& song, Instrument kind, double level, double send);

// The written songs (songs.cpp).
Song velvet_prize();
Song walk_on();

class SongBand final : public Band {
  public:
    SongBand(std::uint32_t seed, Song song);
    // For tests: what in the score does not add up (empty when all is well).
    [[nodiscard]] const std::vector<std::string>& problems() const { return song_.problems; }
    [[nodiscard]] long bars() const { return bars_; }

  private:
    void plan() noexcept override;
    void play_bar() noexcept;
    void at(double beat, Instrument kind, double midi, double vel, double length, double pan) noexcept;
    [[nodiscard]] const SongChord& chord_at(const SongSection& s, int bar, double beat) const noexcept;

    Song song_;
    int place_ = 0;          // in the order
    int bar_ = 0;            // in the section
    double beat_ = .5;
    std::vector<double> last_{};  // each part's last arpeggio note
    long next_bar_ = 0;
    long bars_ = 0;
};

}  // namespace games::music
