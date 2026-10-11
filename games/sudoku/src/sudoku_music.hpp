#pragma once
// Sudoku's music: a koto in a quiet garden, making up its phrases as it plays, never the
// same twice, with now and then the long breath of a shakuhachi.
//
// The koto's thirteen silk strings are tuned to the scale and plucked with an ivory pick:
// each is a plucked delay line (Karplus-Strong) with a bright attack that softens as it
// rings, sounding through the long paulownia body (a pair of soft resonances). Phrases are
// short and move mostly from string to string, closing on the tonic or the fifth; a note
// may be pressed behind the bridge after it is struck (oshide), bending up to the next
// tone of the scale; a phrase may open with a sweep down the strings or two strings
// struck together. Between phrases there is room (ma): silence is part of the music.
// The shakuhachi plays long tones, scooping up into each (meri) with a breath at the
// start and a slow vibrato once it settles.
//
// By day the koto is tuned to the bright yo scale; at night to the in scale (hira-joshi),
// slower and with the flute more often.
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace ps_sudoku {

class GardenMusic final {
  public:
    static constexpr int sample_rate = 48000;

    GardenMusic(std::uint32_t seed, bool night);
    // Adds the music to interleaved stereo frames at the given gain.
    void render_add(std::span<float> stereo, double gain) noexcept;
    // Comes up from silence over `seconds` (when the game is opened again).
    void fade_in(double seconds) noexcept;

    // For tools and tests.
    [[nodiscard]] long notes_plucked() const {
        return plucked_;
    }
    [[nodiscard]] long breaths() const {
        return breaths_;
    }
    [[nodiscard]] int phrases() const {
        return phrases_;
    }

  private:
    struct String {
        std::vector<float> line{};
        int write = 0;
        double delay = 100;      // samples: the open string's period
        double bend = 1;         // pitch ratio now (a press raises it)
        double bend_to = 1;
        double bend_rate = 0;    // per-sample approach of bend to bend_to
        int press_in = -1;       // samples until the press starts, -1 none
        double press_to = 1;
        int release_in = -1;     // samples until the press is let go, -1 none
        float last = 0;
        float bright = .5F;      // the loop's low-pass: higher is brighter
        float loss = .9992F;
        int quiet = 1 << 20;     // samples it has been near silent
        double pan = 0;
    };
    struct Flute {
        double phase = 0;
        double hz = 0;
        double target_hz = 0;
        double env = 0;
        double level = 0;
        double age = 0;          // seconds since the breath began
        double length = 0;       // seconds the tone is held
        double vibrato = 0;
        bool on = false;
        double band_low = 0, band_mid = 0;  // the breath noise's band-pass state
    };
    struct Event {
        long at = 0;             // sample
        int string = 0;
        double strength = 0;
        double press = 0;        // semitones of oshide, 0 none
    };

    double uniform() noexcept;
    int pick(int n) noexcept;
    void pluck(int string, double strength, double press) noexcept;
    float run(String& s) noexcept;
    void plan_phrase() noexcept;
    void breathe() noexcept;
    [[nodiscard]] double flute_sample() noexcept;
    void room(double in, double& left, double& right) noexcept;

    std::uint32_t state_;
    bool night_;
    std::array<int, 13> tuning_{};    // MIDI notes of the thirteen strings
    std::array<int, 5> scale_{};      // pitch classes, semitones above the tonic
    std::array<String, 13> strings_{};
    Flute flute_{};
    std::vector<Event> events_{};
    long clock_ = 0;                  // samples rendered
    long next_phrase_ = 0;            // when the next phrase may be planned
    int last_string_ = 6;
    double beat_ = 1;                 // seconds
    int phrases_ = 0;
    long plucked_ = 0;
    long breaths_ = 0;
    double fade_ = 1, fade_rate_ = 0;
    double gain_ = 0;
    // the body's two soft resonances
    double body_a1_[2]{}, body_a2_[2]{}, body_g_[2]{};
    double body_y1_[2]{}, body_y2_[2]{};
    // the room: four damped delay lines mixed by a Hadamard matrix
    std::array<std::vector<float>, 4> lines_{};
    std::array<int, 4> lengths_{};
    std::array<double, 4> feedback_{};
    std::array<double, 4> low_{};
    int room_write_ = 0;
};

}  // namespace ps_sudoku
