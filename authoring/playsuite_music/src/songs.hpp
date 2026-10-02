// PlaySuite music: song registry and shared arranging helpers.
#pragma once
#include "score.hpp"
#include <functional>
#include <string>
#include <vector>

namespace ps {

struct SongFactory {
    std::string id;
    std::function<Song()> make;
};
std::vector<SongFactory> allSongs();

// Arranging helpers (songs_common.cpp)
// Sustained, voice-led chords.
void padChords(Track& t, const std::vector<ChordAt>& cs, int lo, int hi, int count = 4,
               double vel = 0.6, double gate = 1.0);
// Rhythmic chord hits: pattern over one bar in 16ths ("x..x" etc), voice-led.
void compChords(Track& t, const std::vector<ChordAt>& cs, int lo, int hi,
                const std::string& pattern, int count = 4, double vel = 0.75, double gate = 0.5,
                double step = 0.25, bool omitRoot = false);
// Arpeggio over chord tones: pattern of indices into the voiced chord ('0'-'9'), '.' rest, per
// step.
void arpChords(Track& t, const std::vector<ChordAt>& cs, int lo, int hi, const std::string& pattern,
               double step, int count = 4, double vel = 0.7, double gate = 0.9);
// Bass line from a per-bar pattern of tokens: R (root) 5 (fifth) O (octave root) 3 (third) 7
// (seventh) A (chromatic approach to next chord) '.' rest; one token per step.
void bassLine(Track& t, const std::vector<ChordAt>& cs, int lo, const std::string& pattern,
              double step, double vel = 0.8, double gate = 0.85);
// Jazz walking bass (quarter notes) with chromatic approaches.
void walkingBass(Track& t, const std::vector<ChordAt>& cs, int lo, int hi, unsigned seed,
                 double vel = 0.75);
// Melody plus (voices-1) chord tones directly beneath each note (close-position section soli).
// Returns the end beat of the melody.
double blockHarmony(Track& t, const std::vector<ChordAt>& cs, double beat, const std::string& mel,
                    int voices, double transpose = 0, double vel = 0.8, double gate = 0.9,
                    int floorPitch = 53);
// Throws if a notated phrase does not end where expected (catches notation mistakes).
void expectBeat(double got, double want, const char* what);
// Utility: transposed copy of notes from [b0,b1) moved by shift beats.
void copyNotes(Track& t, double b0, double b1, double shift, double transpose = 0,
               double velScale = 1.0);

Song songMenu();
Song songKlondike();
Song songSpider();
Song songFreecell();
Song songHearts();
Song songSudokuDay();
Song songSudokuNight();
Song songGems();
Song songNatureCube();
Song songUntangle();
Song songPuzzleSolve();
std::vector<SongFactory> stingers();
Song songInstrumentTest();

} // namespace ps
