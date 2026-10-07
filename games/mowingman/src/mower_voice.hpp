#pragma once
// The ride-on mower's voice, synthesized sample by sample: a 90-degree V-twin on a
// mechanical governor, its intake and cooling air, and a three-spindle deck whose
// blades drone like propellers and roar when they bite. Nothing is recorded or
// looped. The governor lives here, so the engine bogs when the blades engage, sags
// in heavy grass and climbs back as it clears, and the blades follow the belt.
//
// The exhaust curve was measured from a recording of a lawn tractor at 3486 rpm
// (see audio_src/README.md). Pure and deterministic for a seed; no allocation after
// construction. control() and render() belong to one thread, or to one lock.
#include <cstdint>
#include <memory>
#include <span>

namespace mm {

struct MowerControls {
    bool ignition{};             // key on; the engine catches, and spins down when it goes off
    double governed_rpm{1500};   // where the throttle lever sits: 1500 idle .. 3600 mowing
    bool blades{};               // the deck clutch
    double load{};               // 0 no grass .. 1 a full deck of tall grass
};

class MowerVoice final {
  public:
    static constexpr int sample_rate = 48000;

    explicit MowerVoice(std::uint64_t seed = 1);
    ~MowerVoice();
    MowerVoice(const MowerVoice&) = delete;
    MowerVoice& operator=(const MowerVoice&) = delete;

    // May be called every frame; the voice glides to what it is given.
    void control(const MowerControls& controls);
    // Fills interleaved left, right frames at 48 kHz, within [-0.75, 0.75].
    void render(std::span<float> stereo);

    [[nodiscard]] double rpm() const;
    [[nodiscard]] double blade_revs_per_second() const;
    // True once the key is off and everything has stopped ringing.
    [[nodiscard]] bool silent() const;

    // Re-measures the component levels behind the constants in mower_voice.cpp and
    // returns the worst relative difference. Slow; for the tests.
    [[nodiscard]] static double calibration_drift();

  private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace mm
