#pragma once
// The old lady's voice, synthesized as she speaks: a formant synthesizer with an old
// woman's wide, uneven tremor, a rasp from every other pulse, breath, and a creak at
// the ends of phrases. She has four things to say: the shout when she spots the mower,
// a scolding as she chases it, a shriek at the gnome, and a wail as she runs off.
#include <atomic>
#include <cstdint>
#include <span>

namespace mm {

enum class GrannyLine : int { shout, scold, shriek, wail };
constexpr int granny_line_count = 4;

class GrannyVoice final {
  public:
    static constexpr int sample_rate = 48000;
    explicit GrannyVoice(std::uint32_t seed = 5);
    // From the scene's thread: asks for a line; the renderer begins it at its next block.
    void say(GrannyLine line, double pan);
    // On the audio thread: adds whatever she is saying to interleaved stereo frames.
    void render_add(std::span<float> stereo);
    // Renders one whole line on its own, for the clips: returns frames written.
    std::size_t render_line(GrannyLine line, std::span<float> stereo);

  private:
    struct Res {
        double y1{}, y2{}, a1{}, a2{}, g{1};
        void tune(double hz, double bw);
        double run(double x);
    };
    struct Syllable {
        double f1, f2, f3;  // the vowel
        double f0a, f0b;    // pitch at its start and end, Hz
        double length;      // seconds
        double loud;
        double gap;         // silence after, seconds
    };
    double uniform();
    double normal();
    double sample(const Syllable& s, double t, double dt);
    std::uint32_t state_;
    std::atomic<int> asked_[granny_line_count]{};
    int heard_[granny_line_count]{};
    std::atomic<float> pan_{0};
    int line_{-1};
    int syllable_{};
    std::size_t at_{};  // frames into the syllable (or its gap)
    double phase_{}, vib_{}, trem_rate_{5.5}, trem_target_{5.5}, trem_wait_{}, period_{1}, lp_{};
    double cf1_{500}, cf2_{1500}, cf3_{2600};
    bool odd_{};
    Res f1_{}, f2_{}, f3_{}, nasal_{};
};

} // namespace mm
