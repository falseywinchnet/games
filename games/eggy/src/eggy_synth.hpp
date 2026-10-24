#pragma once
// authoring/eggy/audio_src/eggy_synth.py: Eggy's extra instruments, registered into
// the shared synth.py engine (voice::Synth). EggySynth is one imported synth module
// with eggy_synth's additions in INSTR and DRUMS: it owns the module's _rng (through
// voice::Synth) and render_note / render_drum caches that know the extra names, so a
// cue built on it draws exactly what the Python scripts drew.
//
// Not thread-safe: an EggySynth belongs to one renderer. Standard library only.
#include "voice_synth.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

namespace eggy {

class EggySynth final {
  public:
    // The module as imported (_rng = default_rng(12345)), or with _rng at a recorded state.
    EggySynth();
    explicit EggySynth(const voice::GeneratorState& rng_state);

    [[nodiscard]] voice::Synth& base() {
        return synth_;
    }
    [[nodiscard]] voice::Generator& rng() {
        return synth_.rng();
    }

    // eggy_synth.py's drums (DRUMS: snare, ghost, anvil, frame, washboard).
    [[nodiscard]] voice::Signal snare(double vel = .6, std::optional<std::uint64_t> seed = std::nullopt);
    [[nodiscard]] voice::Signal snare_ghost(double vel = .25, std::optional<std::uint64_t> seed = std::nullopt);
    [[nodiscard]] voice::Signal anvil(double vel = .5, std::optional<std::uint64_t> seed = std::nullopt);
    [[nodiscard]] voice::Signal frame_drum(double vel = .6, std::optional<std::uint64_t> seed = std::nullopt);
    [[nodiscard]] voice::Signal washboard(double vel = .3, std::optional<std::uint64_t> seed = std::nullopt);
    // eggy_synth.py's instruments (INSTR: bowl, whistle, banjo, twang, brass, drone).
    [[nodiscard]] voice::Signal bowl(double m, double dur, double vel = .6);
    [[nodiscard]] voice::Signal whistle(double m, double dur, double vel = .6);
    [[nodiscard]] voice::Signal banjo(double m, double dur, double vel = .6);
    [[nodiscard]] voice::Signal twang(double m, double dur, double vel = .6);
    [[nodiscard]] voice::Signal brass(double m, double dur, double vel = .6);
    [[nodiscard]] voice::Signal drone(double m, double dur, double vel = .5);

    // INSTR[name](m, dur, vel) and DRUMS[name](vel, seed=seed), Eggy's names included.
    [[nodiscard]] voice::Signal instrument(std::string_view name, double m, double dur, double vel);
    [[nodiscard]] voice::Signal drum(std::string_view name, double vel, std::optional<std::uint64_t> seed);

    // synth.render_note / render_drum: quantised and cached as synth.py does it.
    const voice::Signal& render_note(std::string_view inst, double m, double dur, double vel);
    const voice::Signal& render_drum(std::string_view name, double vel, long long rr);
    // A note as if rendered earlier in the script from _rng at `state`; this module's
    // _rng is untouched.
    void prime_note(std::string_view inst, double m, double dur, double vel, const voice::GeneratorState& state);

    // Samples held by the note and drum caches (for measuring memory).
    [[nodiscard]] std::size_t cached_samples() const;
    // Empties both caches (references from render_note / render_drum become invalid).
    void clear_caches();

  private:
    using NoteKey = std::tuple<std::string, double, double, double>;
    using DrumKey = std::tuple<std::string, double, long long>;
    voice::Synth synth_;
    std::map<NoteKey, voice::Signal> notes_{};
    std::map<DrumKey, voice::Signal> drums_{};
};

}  // namespace eggy
