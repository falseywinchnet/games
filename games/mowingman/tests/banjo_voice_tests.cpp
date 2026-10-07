// The live banjo band: bounded and deterministic in every style, at its calibrated
// level, picking a Scruggs roll's sixteenths (or the clawhammer's bum-ditty), playing
// tune after tune, hearing one part alone, silent at no gain and fading to silence
// when the gain is taken away, and far cheaper than real time.
#include "banjo_voice.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int checks = 0;
void require(bool condition, const char* what) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

constexpr int rate = mm::BanjoVoice::sample_rate;
constexpr mm::BanjoStyle styles[3] = {mm::BanjoStyle::scruggs, mm::BanjoStyle::clawhammer, mm::BanjoStyle::porch};

// Plays the band in 10 ms blocks, as the device asks for them.
std::vector<float> play(mm::BanjoVoice& band, double seconds, double gain) {
    std::vector<float> sound;
    std::vector<float> block(static_cast<std::size_t>(rate / 100) * 2);
    for (int k = 0; k < static_cast<int>(seconds * 100); ++k) {
        std::fill(block.begin(), block.end(), 0.0F);
        band.render_add(block, gain);
        sound.insert(sound.end(), block.begin(), block.end());
    }
    return sound;
}

double decibels(const std::vector<float>& sound, std::size_t from, std::size_t to) {
    double sum = 0;
    for (std::size_t k = from; k < to; ++k)
        sum += static_cast<double>(sound[k]) * sound[k];
    return 10 * std::log10(sum / static_cast<double>(to - from) + 1e-30);
}

void test_styles() {
    for (mm::BanjoStyle style : styles) {
        mm::BanjoVoice band(5, style);
        const std::vector<float> sound = play(band, 40, 1.0);
        bool finite = true;
        float peak = 0;
        for (float sample : sound) {
            finite = finite && std::isfinite(sample);
            peak = std::max(peak, std::abs(sample));
        }
        require(finite, "the band never plays a NaN or an infinity");
        require(peak < 0.95F, "the band leaves headroom");
        // Background music: about where the band was accepted, never loud.
        const double level = decibels(sound, 0, sound.size());
        require(level > -30 && level < -20, "the band plays at its calibrated level");
        const double per_second = static_cast<double>(band.notes_picked(mm::BanjoVoice::part_banjo)) / 40;
        if (style == mm::BanjoStyle::clawhammer)
            require(per_second > 3 && per_second < 7, "the clawhammer frails bum-ditty: a note, a brush, the thumb");
        else
            require(per_second > 6 && per_second < 9.5, "the Scruggs roll picks steady sixteenths");
        require(band.tempo() > 100 && band.tempo() < 130, "the tempo stays relaxed bluegrass");
        require(band.notes_picked(mm::BanjoVoice::part_bass) > 30, "the upright bass plays under the banjo");
    }
}

void test_deterministic() {
    mm::BanjoVoice first(9, mm::BanjoStyle::porch);
    mm::BanjoVoice second(9, mm::BanjoStyle::porch);
    require(play(first, 12, 1.0) == play(second, 12, 1.0), "one seed plays one performance");
    mm::BanjoVoice other(10, mm::BanjoStyle::porch);
    mm::BanjoVoice again(9, mm::BanjoStyle::porch);
    require(play(other, 12, 1.0) != play(again, 12, 1.0), "another seed plays another");
}

void test_tunes() {
    mm::BanjoVoice band(3, mm::BanjoStyle::porch);
    play(band, 150, 1.0);
    require(band.tunes_played() >= 3, "the band moves on from tune to tune");
}

void test_solo() {
    mm::BanjoVoice whole(4, mm::BanjoStyle::scruggs);
    mm::BanjoVoice banjo(4, mm::BanjoStyle::scruggs);
    mm::BanjoVoice bass(4, mm::BanjoStyle::scruggs);
    banjo.solo(mm::BanjoVoice::part_banjo);
    bass.solo(mm::BanjoVoice::part_bass);
    const std::vector<float> all = play(whole, 20, 1.0);
    const std::vector<float> lead = play(banjo, 20, 1.0);
    const std::vector<float> low = play(bass, 20, 1.0);
    const double whole_level = decibels(all, 0, all.size());
    const double banjo_level = decibels(lead, 0, lead.size());
    const double bass_level = decibels(low, 0, low.size());
    require(banjo_level < whole_level && banjo_level > whole_level - 4, "the banjo carries the band");
    require(bass_level < banjo_level - 5, "the bass stays under the banjo");
    mm::BanjoVoice porch(4, mm::BanjoStyle::porch);
    porch.solo(mm::BanjoVoice::part_mandolin);
    const std::vector<float> none = play(porch, 10, 1.0);
    require(decibels(none, 0, none.size()) < -200, "the porch band has no mandolin");
}

void test_gain() {
    mm::BanjoVoice band(6, mm::BanjoStyle::porch);
    const std::vector<float> silent = play(band, 3, 0.0);
    require(decibels(silent, 0, silent.size()) < -200, "no gain, no sound");
    std::vector<float> sound = play(band, 6, 1.0);
    const std::vector<float> after = play(band, 4, 0.0);
    sound.insert(sound.end(), after.begin(), after.end());
    const std::size_t second = static_cast<std::size_t>(rate) * 2;
    require(decibels(sound, 4 * second, 6 * second) > -40, "the band plays at full gain");
    require(decibels(sound, 9 * second, 10 * second) < -70, "taking the gain away fades the band out");
}

void test_cost() {
    mm::BanjoVoice band(7, mm::BanjoStyle::scruggs);
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    play(band, 60, 1.0);
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("banjo band: 60 s of sound in %.3f s\n", took);
    // generous for a busy machine; it measures well under 1% of a core
    require(took < 6, "the band costs a small fraction of one core");
}

} // namespace

int main() {
    test_styles();
    test_deterministic();
    test_tunes();
    test_solo();
    test_gain();
    test_cost();
    std::printf("banjo voice: %d checks passed\n", checks);
    return 0;
}
