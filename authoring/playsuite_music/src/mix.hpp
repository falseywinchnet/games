// PlaySuite music synthesizer: rendering, mix bus, mastering, analysis and file output.
#pragma once
#include "dsp.hpp"
#include "score.hpp"
#include <string>
#include <vector>

namespace ps {

struct SeamStats {
    double jump = 0, jump_percentile = 0, rms_step_db = 0, d2_percentile = 0;
};

struct Report {
    std::string id, title, file;
    long samples = 0;
    double seconds = 0, bpm = 0;
    int bars = 0;
    double lufs = 0, true_peak_dbtp = 0, peak_dbfs = 0, dc_max = 0;
    long clipped = 0;
    double gain_reduction_max_db = 0;
    std::vector<std::pair<std::string, int>> sections; // name, bars
    std::vector<std::pair<std::string, double>> section_rms_db;
    std::vector<std::pair<std::string, double>> bands_db; // relative to total energy
    SeamStats seam;
    double start_abs = 0, end_abs = 0;
    double tail_residual =
        0; // max |x| in the last 100 ms of the rendered tail (must be ~0 for an exact fold)
    bool loop = true;
};

Stereo renderSong(const Song& song, Report& report);
void writeWav24(const std::string& path, const Stereo& s, std::size_t extraWrap = 0);
void writeF32(const std::string& path, const Stereo& s);
double integratedLufs(const Stereo& s);
double truePeakDb(const Stereo& s);
SeamStats seamStats(const Stereo& s);
std::string reportJson(const Report& r);
// Analyse decoded PCM (interleaved f32) against a master, for codec checks.
std::string analyseDecoded(const std::string& decodedF32, const std::string& masterF32, long frames,
                           bool loop);

} // namespace ps
