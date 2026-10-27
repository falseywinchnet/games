#pragma once
// The sound of the falls and the forest round them, synthesized as it plays, in layers
// that can each be heard alone:
//
//   roar     the falls across the clearing: not shaped noise but thousands of bubbles
//            a second, each ringing at its own pitch as it is torn open and rising as it
//            shrinks (Minnaert's resonance), spread across the curtain's width, with
//            the impact's rumble and thumps beneath and the spray's hiss above, all
//            swelling with the surges that come down the falls, and softened by the
//            85 m of air between;
//   pool     the water just below the walkway: nearer, larger bubbles, gurgles, and
//            the lap of the pool against the piles;
//   birds    the forest's birds: a piha's ringing call, a tinamou's tremulous falling
//            whistles, a trogon's soft repeated notes, toucans croaking, parakeets
//            chattering past, a small bird's trill; each its own distance and place;
//   insects  a cicada swelling and fading, and a cricket chorus under everything;
//   frogs    tree frogs' chirps and the pool frogs' twanging croaks;
//   air      the wind in the canopy, leaves fluttering, drops falling from them.
//
// Every layer goes into one space: a clearing walled by forest and cliff, its echo a
// long soft tail. Everything is deterministic from its seed and far faster than real
// time.
#include <cstdint>
#include <memory>
#include <span>

namespace cf {

class FallsVoice final {
  public:
    static constexpr int sample_rate = 48000;
    enum Part : int { roar = 0, pool = 1, birds = 2, insects = 3, frogs = 4, air = 5, parts = 6 };

    explicit FallsVoice(std::uint32_t seed = 1);
    ~FallsVoice();
    FallsVoice(const FallsVoice&) = delete;
    FallsVoice& operator=(const FallsVoice&) = delete;

    // Adds the scene's sound to interleaved stereo frames at `gain`.
    void render_add(std::span<float> stereo, double gain);
    // For tools: hear one part alone (a Part), or -1 for everything.
    void solo(int part);
    // For tools: silence pieces of the roar to hear what each contributes (bits:
    // 1 bubbles, 2 spray bubbles, 4 splashes, 8 sloshes, 16 hiss, 32 rumble and
    // thumps, 64 the moving equalizer).
    void mute_roar(unsigned pieces);

  private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace cf
