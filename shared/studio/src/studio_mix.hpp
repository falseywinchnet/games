// PlaySuite music synthesizer: rendering, mix bus and mastering (authoring/playsuite_music's
// mix.cpp without its reports and file output).
#pragma once
#include "studio_dsp.hpp"
#include "studio_score.hpp"
#include <functional>
#include <vector>

namespace studio {

// Where a song's render spent its time, in seconds.
struct RenderTimings {
    double tracks = 0; // every track's voices, filters, ducking and sends
    double buses = 0;  // ping-pong delay, reverb and master high-pass
    double master = 0; // loop fold, DC, bus compressor, loudness and limiting
};

// One track's contribution to the dry, reverb-send and delay-send buses: exactly the floats
// the original added into them (the sends only when the track has them).
struct TrackStem {
    Stereo dry, rsend, dsend;
    bool rev = false, dly = false;
};

// The ducking trigger times every ducked track follows.
std::vector<long> duckTriggers(const Song& song);
// The render length of a song in frames: its loop (or cue) and the tail rendered after it.
inline std::size_t renderFrames(const Song& song) {
    return std::size_t(song.loopSamples() + long(song.tail * SR));
}
// Renders track k into `stem`, `frames` long.
void renderStem(const Song& song, std::size_t k, const std::vector<long>& triggers,
                std::size_t frames, TrackStem& stem);
// Adds a stem to the buses. Stems must be added in track order: the buses are float sums.
void addStem(const TrackStem& stem, Stereo& dry, Stereo& rsend, Stereo& dsend);

// Fills the three buses (already sized to the render length) from every track, in track
// order. The default renders the tracks one after another on the calling thread.
using StemRenderer = std::function<void(const Song&, const std::vector<long>& triggers,
                                        Stereo& dry, Stereo& rsend, Stereo& dsend)>;

// The finished master the original program wrote (before its 24-bit quantisation).
Stereo renderSong(const Song& song, RenderTimings* timings = nullptr,
                  const StemRenderer& stems = {});
double integratedLufs(const Stereo& s);
double truePeakDb(const Stereo& s);

} // namespace studio
