#pragma once
// Nature Cube's music: a quiet glasshouse ensemble that improvises for as long as the
// game is open. Soft detuned pads breathe in a slow chorus over a drone; a round synth
// bass either holds the tonic (a pedal) or walks in two-beat pulses with the pads
// planing above it; a breeze moves through the glasshouse; a high glass voice sings short motifs and answers them; a soft
// heartbeat taps under the pulses; now and then a bird calls. It plays in sections
// (pedal, pulse, a breath of pads alone, and a glow after a win), in a modal key that
// moves every few sections, and it brightens when a pair is joined and resolves to its
// tonic when the puzzle is solved.
//
// Everything is generated from a seed: progressions, motifs, rhythms, keys and tempo.
// render_add() runs on the audio thread; cue() is called there too, by whoever owns
// the cue ring.
#include "glass_dsp.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace ps_cube {

class GlassMusic final {
  public:
    enum Part { pads, bass, lead, bells, heartbeat, birds, breeze, part_count };
    enum Kind { pedal, pulse, breath, glow };

    explicit GlassMusic(std::uint32_t seed, Harmony* harmony = nullptr);
    // Adds the music to interleaved stereo frames.
    void render_add(std::span<float> stereo, double gain) noexcept;
    void cue(Cue cue) noexcept;
    // Plays one part alone (or every part with -1), for listening to stems.
    void solo(int part) noexcept {
        solo_ = part;
    }
    // Restarts the fade-in, as when the game is shown again.
    void fade_in(double seconds) noexcept;

    // For tests and tools.
    [[nodiscard]] int tonic() const noexcept {
        return tonic_;
    }
    [[nodiscard]] int mode() const noexcept {
        return mode_;
    }
    [[nodiscard]] int kind() const noexcept {
        return kind_;
    }
    [[nodiscard]] int sections_played() const noexcept {
        return sections_;
    }
    [[nodiscard]] double tempo() const noexcept {
        return tempo_;
    }
    [[nodiscard]] double brightness() const noexcept {
        return bright_;
    }
    [[nodiscard]] int chord_mask() const noexcept;

    struct Chord {
        int root = 0;     // semitones above the tonic, may be chromatic
        int tones[4]{};   // semitones above the root
        int count = 3;
    };
    struct Note {
        int start = 0;    // sixteenth within a two-bar motif
        int length = 4;
        int degree = 0;   // scale steps above the tonic, may be negative or past 7
    };
    struct Motif {
        std::array<Note, 6> notes{};
        int count = 0;
    };

  private:
    struct PadVoice {
        double phase[2]{};
        double inc[2]{};
        double level = 0, target = 0;
        double attack = 0, release = 0;
        double pan = 0;
        int midi = 0;
    };
    struct LeadVoice {
        double phase = 0, inc = 0, level = 0, target = 0, attack = 0, release = 0;
        double age = 0, pan = 0, vibrato = 0, tremolo = 0;
        int gate = 0;     // samples left before release
    };
    struct BellVoice {
        double phase[3]{}, inc[3]{}, amp[3]{}, decay[3]{};
        double pan = 0;
        int delay = 0;
        bool on = false;
    };
    struct BeatVoice {
        double phase = 0, freq = 0, level = 0, click = 0;
        bool on = false;
    };
    struct Chirp {
        int delay = 0, length = 0, age = 0;
        double from = 0, peak = 0, to = 0, level = 0, pan = 0;
    };

    // the composition
    void plan_section();
    void plan_pedal();
    void plan_pulse();
    void plan_breath();
    void plan_glow();
    void plan_lead();
    void new_motif(Motif& motif);
    void change_key();
    void step_sixteenth();
    void play_chord(const Chord& chord, double attack);
    void bass_note(int midi, int sixteenths);
    void lead_note(const Note& note, int bar_in_phrase);
    void bell(int midi, double level, int delay);
    void beat(double level);
    void bird_phrase();
    void publish();
    [[nodiscard]] Chord diatonic(int degree, int shape) const;
    [[nodiscard]] Chord planed(int root, int shape) const;
    [[nodiscard]] int scale_pc(int degree) const;
    [[nodiscard]] int degree_midi(int degree) const;
    [[nodiscard]] bool chord_has(int pc) const;
    [[nodiscard]] bool usable(int degree) const;
    [[nodiscard]] double part(int which) const noexcept;

    Dice dice_;
    Harmony* harmony_;
    SineTable sine_{};
    Room room_{2.6, 4200};

    // voices
    std::array<PadVoice, 10> pad_{};
    Svf pad_low_[2]{}, pad_high_[2]{};
    double bass_phase_ = 0, bass_sub_ = 0, bass_inc_ = 0, bass_level_ = 0, bass_target_ = 0;
    int bass_gate_ = 0, bass_dip_ = 0;
    double bass_duck_ = 1, bass_next_inc_ = 0;
    Svf bass_filter_{};
    std::array<LeadVoice, 2> lead_{};
    int next_lead_ = 0;
    std::array<BellVoice, 8> bell_{};
    int next_bell_ = 0;
    std::array<BeatVoice, 2> beat_{};
    int next_beat_ = 0;
    Svf click_filter_{};
    std::array<Chirp, 16> chirps_{};
    double chirp_phase_ = 0;
    Svf breeze_[2]{};
    double breeze_phase_ = 0, breeze_swell_ = 0;
    // the chorus
    std::array<float, 4096> chorus_[2]{};
    int chorus_write_ = 0;
    double chorus_lfo_ = 0;
    double dc_[2]{}, out_low_[2]{};

    // the score
    int tonic_ = 0;          // pitch class
    int mode_ = 0;
    int kind_ = pedal;
    int length_ = 8;         // bars in the section
    int bar_ = 0;
    int sixteenth_ = 0;
    int sections_ = 0;
    int key_sections_ = 0;   // sections since the key changed
    int key_due_ = 9;
    std::array<Chord, 16> chords_{};
    std::array<int, 16> bass_root_{};   // midi of each bar's bass
    int bass_style_ = 0;
    int beat_style_ = -1;
    int lead_plan_[4]{};     // per two bars: -1 rest, else a motif and its variation
    Motif motifs_[3]{};      // this section's motifs
    Motif memory_[4]{};      // motifs from earlier sections
    int memory_count_ = 0;
    int last_pad_[4]{};
    int pad_count_ = 0;
    double tempo_ = 88;
    double clock_ = 0;       // samples until the next sixteenth
    double energy_ = 0.5;
    double bright_ = 0;      // after a pair is joined; decays
    double fade_ = 0, fade_rate_ = 0;
    double bird_wait_ = 20;  // seconds until a bird may call
    bool restart_ = false;   // a level: begin a fresh section on the next bar
    int sparkle_ = 0;        // bell notes left to play after a join
    int solo_ = -1;
    Chord current_{};
};

} // namespace ps_cube
