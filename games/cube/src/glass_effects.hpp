#pragma once
// Nature Cube's sound effects, synthesized as they play and tuned to the music's chord:
// a glass tap when an endpoint is picked, droplets that climb the scale as a path
// grows and fall back as it is erased, a chord that blooms when a pair is joined, a soft
// wooden bump for a move that cannot be made, air across the glass while the cube
// turns, a rising chime for a new level and a sparkling arpeggio over an open fifth for
// the win. Repeats never land on the same pitch twice running.
#include "glass_dsp.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace ps_cube {

class GlassEffects final {
  public:
    explicit GlassEffects(std::uint32_t seed, const Harmony* harmony = nullptr);
    void render_add(std::span<float> stereo, double gain) noexcept;
    void cue(Cue cue) noexcept;
    [[nodiscard]] bool quiet() const noexcept;

  private:
    // A struck tone: up to four sine partials, each with its own level and decay, an
    // optional pitch glide, an attack, an optional hold before a quicker release, and an
    // optional filtered noise burst.
    struct Tone {
        double phase[4]{}, inc[4]{}, amp[4]{}, decay[4]{};
        int partials = 0;
        double glide = 1, glide_to = 1, glide_rate = 0;
        double attack = 1, attack_rate = 1;
        int hold = -1;           // samples before the release, -1 rings out freely
        double release = 1;      // per-sample decay once held
        double noise = 0, noise_decay = 0;
        Svf noise_band{};
        double pan = 0, send = .3;
        int delay = 0;
        bool on = false;
    };

    Tone& voice();
    void glass(int midi, double level, int delay, double length, double pan);
    void droplet(int midi, double level, bool falling);
    void bloom(double level);
    void bump();
    void chime();
    void fanfare();
    [[nodiscard]] int chord_tone_near(int midi, int avoid) const;
    [[nodiscard]] int scale_note(int index) const;
    [[nodiscard]] int pentatonic_index_near(int midi) const;

    Dice dice_;
    const Harmony* harmony_;
    SineTable sine_{};
    Room room_{1.8, 5000};
    std::array<Tone, 28> tones_{};
    int next_ = 0;
    int steps_ = 0;          // cells in the path being drawn
    int step_base_ = 0;      // where its climb starts, in pentatonic steps
    int last_pick_ = -1, joins_ = 0;
    double since_bump_ = 1;
    double air_ = 0, air_target_ = 0, air_phase_ = 0;
    Svf air_band_{};
    double dc_[2]{};
    int tail_ = 0;  // frames of room left to ring
};

} // namespace ps_cube
