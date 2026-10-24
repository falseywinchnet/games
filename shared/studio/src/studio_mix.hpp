// PlaySuite music synthesizer: rendering, mix bus and mastering (authoring/playsuite_music's
// mix.cpp without its reports and file output). The tracks are rendered a segment at a time
// so that a crew of helper threads can share them; the result is the original's sample for
// sample whatever the crew.
#pragma once
#include "studio_crew.hpp"
#include "studio_dsp.hpp"
#include "studio_score.hpp"

namespace studio {

// Where a song's render spent its time, in seconds.
struct RenderTimings {
    double tracks = 0; // every track's voices, filters, ducking and sends
    double buses = 0;  // ping-pong delay, reverb and master high-pass
    double master = 0; // loop fold, DC, bus compressor, loudness and limiting
};

// The finished master the original program wrote (before its 24-bit quantisation).
Stereo renderSong(const Song& song, RenderTimings* timings = nullptr, const Crew& crew = {});
double integratedLufs(const Stereo& s);
double truePeakDb(const Stereo& s);

} // namespace studio
