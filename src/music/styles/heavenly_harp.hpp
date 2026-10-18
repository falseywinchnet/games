#pragma once
// Canopy Falls' music (the style "heavenly_harp"): something heavenly over the falls, composed as it plays and never
// the same twice. Slow and bright, in three, in a major key with the ninth and now and
// then the raised fourth for light; four-bar phrases on simple progressions, each ending
// in a bar that breathes, and every few phrases a step to a neighbouring key and back.
//
// Every voice is synthesized here, sample by sample (nothing recorded):
//
//   harp      a ten-string harp, tuned to D major's pentatonic notes from D4 to B5,
//             so every string sits in each key the music visits: each string a
//             waveguide (Karplus and Strong's delay line, tuned between samples by an
//             all-pass, losing its upper partials a little each pass), ringing in two
//             planes a hair apart so its tone blooms as it dies; plucked with the pad
//             of the finger near its middle; a ringing string stopped a moment before
//             it is plucked again; all ten on one bridge, so strings in tune with what
//             sounds ring in sympathy; heard mostly as the strings' own swing, round
//             and warm, through the soundboard's low resonances. It plays as a
//             harpist does on so few strings: strokes across those that belong to the
//             chord, the chord's root low down, raindrops on the upper strings;
//   celesta   small struck bells: the tune's long notes an octave up, and sparkles at
//             the ends of phrases;
//   pad       a soft, airy synthesizer under it all: each chord tone a cluster of warm,
//             slightly detuned saw waves spread across the stereo field, through a low
//             filter that breathes slowly, a faint pure tone an octave up for air;
//             swelling in over two seconds and letting go over three, so each chord
//             dissolves into the next;
//   chimes    a set of six tuned aluminium tubes hanging from the walkway's roof, struck
//             by a clapper the breeze swings: little bouncing runs of notes, then quiet;
//             each tube ringing long, wavering as its twin vibrations beat, swaying in
//             loudness as it swings. Tuned to D major's pentatonic notes, which sit in
//             every key the music visits.
//
// All go through one hall. Deterministic from its seed; far faster than real time.
#include "music_core.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace games::music {

namespace heavenly {
struct Score;
}

class HeavenlyHarp final : public Composer {
  public:
    enum Voice : int { harp = 0, celesta = 1, chimes = 2, pad = 3, voices = 4 };

    explicit HeavenlyHarp(std::uint32_t seed);
    ~HeavenlyHarp() override;
    HeavenlyHarp(const HeavenlyHarp&) = delete;
    HeavenlyHarp& operator=(const HeavenlyHarp&) = delete;

    void render_add(std::span<float> stereo, double gain) noexcept override;
    void fade_in(double seconds) noexcept override {
        fade_.start(seconds);
    }
    // For tools: hear one voice alone (a Voice), or -1 for all.
    void solo(int voice) noexcept;
    // Phrases played so far (for tools and tests).
    [[nodiscard]] long phrases() const noexcept;

  private:
    std::unique_ptr<heavenly::Score> score_;
    Fade fade_{};
};

} // namespace games::music
