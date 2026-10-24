#pragma once
// Live cues: every PlaySuite sound a game names is rendered by code when it is first
// wanted instead of being read from a file. A game lists its cues (a name, as the
// game passes it to its audio calls, and a function that renders it) and adds them
// to the process-wide Bank once; the shell's clip loader (src/audio_loader.cpp) asks
// the Bank before it looks for a file, renders the cue on its background Worker and
// keeps the clip in memory for every later play. Pan, rate, gain and looping are
// applied to it exactly as to a file.
//
// Standard library only; the Bank is safe to use from any thread. Rendering is the
// caller's (the shell renders on its loader Worker, never on the audio callback).
#include "voice_synth.hpp"

#include <cstddef>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace voice {

// Interleaved left, right samples at 48 kHz: gui_forms::AudioClip::copy's layout.
using Interleaved = std::vector<float>;

// A cue: its name, as the game passes it to its audio calls, and the function that
// renders its master in doubles, exactly as the Python script computed it before
// writing the file. A renderer is deterministic and self-contained (it starts its own
// Synth and generators from recorded states) and safe to run on any thread. Give
// `mono` for a mono master (it plays as left = right, as `ffmpeg -ac 2` made the
// runtime files from the mono WAVs) or `stereo` for a stereo one.
struct Cue {
    std::string_view name{};
    Signal (*mono)(){};
    Stereo (*stereo)(){};
};

// The cue's master as the player wants it: interleaved floats.
[[nodiscard]] Interleaved render(const Cue& cue);
// A mono master as left = right floats; a stereo master interleaved.
[[nodiscard]] Interleaved interleave(std::span<const double> mono);
[[nodiscard]] Interleaved interleave(const Stereo& signal);

class Bank final {
  public:
    // The process-wide bank the shell consults.
    [[nodiscard]] static Bank& shared();
    // Adds a game's cues (their names must be string literals). Adding the same cues
    // again changes nothing; a cue with an existing name replaces it.
    void add(std::span<const Cue> cues);
    // The cue called `name`, if a game registered one.
    [[nodiscard]] std::optional<Cue> find(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> names() const;

  private:
    mutable std::mutex mutex_{};
    std::vector<Cue> cues_{};
};

}  // namespace voice
