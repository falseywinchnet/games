#pragma once
// The helpers the audio batch scripts share (authoring/audio/work/src/sfx.py and
// sfx2.py), ported line for line in numpy's order of operations: noise bands, the
// envelope, place_at, the small room reverb every cue goes through, finish() (the
// trim and normalisation each cue gets before it is written), and the note
// sequencers. Standard library and shared/voice only.
#include "voice_synth.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace batch_cues {

// One render_note cache miss of a script run: the note as the script asked for it
// and the state synth._rng had at that moment (tools/batch_reference.py logs them).
struct NoteStart {
    std::string_view inst{};
    double m{};
    double dur{};
    double vel{};
    voice::GeneratorState state{};
};

// The synth module a cue renders with. A cue renders alone, so each note it plays is
// put in the render_note cache from the state the script first rendered it at,
// exactly as the script's cache held it; notes that draw nothing are unaffected.
class Notes final {
  public:
    explicit Notes(std::span<const NoteStart> script);
    // synth.render_note(inst, m, dur, vel), as the script's run returned it.
    const voice::Signal& note(std::string_view inst, double m, double dur, double vel);
    [[nodiscard]] voice::Synth& synth() {
        return synth_;
    }

  private:
    std::span<const NoteStart> script_;
    std::vector<bool> primed_{};
    voice::Synth synth_{};
};

// x * gain, a new array (numpy's `x * g`).
[[nodiscard]] voice::Signal scaled(std::span<const double> x, double gain);

// ------------------------------------------------------------------ sfx.py
// nz(n, seed, lo, hi, order): seeded noise through fft_filter, normalised to 1.
[[nodiscard]] voice::Signal nz(std::size_t n, std::uint64_t seed, double lo, double hi, int order = 2);
// env(n, att, tau) = (1 - exp(-t / max(att, 1e-4))) * exp(-t / tau)
[[nodiscard]] voice::Signal env(std::size_t n, double att, double tau);
// place_at(buf, y, sec): adds y at int(sec * SR), cut at the buffer's end.
void place_at(voice::Signal& buf, std::span<const double> y, double sec);
// verb(x, amt, tail): the dry signal plus amt times the mean of both channels of
// x convolved with IR = make_ir(1.6, 1.2, 1.0, 0.5, 0.01, seed=11), over x and
// `tail` seconds of silence.
[[nodiscard]] voice::Signal verb(std::span<const double> x, double amt, double tail);
// finish(x, peak_db, fade): DC removed, faded, trimmed below -70 dB, normalised.
[[nodiscard]] voice::Signal finish(std::span<const double> x, double peak_db, double fade = 0.01);
// chime(notes, inst, vel, gap, dur)
[[nodiscard]] voice::Signal chime(Notes& notes, std::span<const double> midi, std::string_view inst, double vel = 0.6,
                                  double gap = 0.07, double dur = 0.5);
// card_tick(seed, bright)
[[nodiscard]] voice::Signal card_tick(std::uint64_t seed, double bright = 1.0);

// ------------------------------------------------------------------ sfx2.py
// seq(notes, inst, gap, vel, dur, tail) with notes evenly spaced by `gap`.
[[nodiscard]] voice::Signal seq(Notes& notes, std::span<const double> midi, std::string_view inst, double gap,
                                double vel = .6, double dur = .4, double tail = 1.5);
// One part of mix(*parts): a signal, its offset in seconds and its gain.
struct Part {
    std::span<const double> signal{};
    double offset{};
    double gain{};
};
// mix(*parts)
[[nodiscard]] voice::Signal mix(std::span<const Part> parts);
// swoosh(dur, lo, hi, seed, rise, amp)
[[nodiscard]] voice::Signal swoosh(double dur, double lo, double hi, std::uint64_t seed, bool rise = true,
                                   double amp = .3);
// glide(f0, f1, dur, kind, amp, tau): kind 'sine' (no wave) or a wavetable; tau 0 is None.
enum class GlideKind { sine, flute };
[[nodiscard]] voice::Signal glide(voice::Synth& synth, double f0, double f1, double dur,
                                  GlideKind kind = GlideKind::sine, double amp = .4, double tau = 0);

}  // namespace batch_cues
