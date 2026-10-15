// The music engine (src/music): every score plays, plays the same for the same seed
// whatever the block size the device asks for, stays inside full scale at its game's
// level, keeps playing (no score runs dry or stalls), and comes up from silence when
// asked. Then what each game's score promises of its own.
#include "repertoire.hpp"
#include "styles/koto_garden.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace games::music;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<float> render(Composer& score, double seconds, std::size_t block, double gain = 1.0) {
    std::vector<float> all(static_cast<std::size_t>(seconds * sample_rate) * 2, 0.0F);
    for (std::size_t at = 0; at < all.size(); at += block * 2) {
        const std::size_t n = std::min(block * 2, all.size() - at);
        score.render_add(std::span<float>(all.data() + at, n), gain);
    }
    return all;
}
float peak_of(const std::vector<float>& s, std::size_t from, std::size_t to) {
    float peak = 0;
    for (std::size_t i = from; i < to; ++i) peak = std::max(peak, std::fabs(s[i]));
    return peak;
}
double rms(const std::vector<float>& s, std::size_t from, std::size_t to) {
    double sum = 0;
    for (std::size_t i = from; i < to; ++i) sum += static_cast<double>(s[i]) * s[i];
    return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, to - from)));
}
void check_score(std::string_view style) {
    const std::string name(style);
    std::unique_ptr<Composer> a = make_score(style, 7), b = make_score(style, 7), c = make_score(style, 8);
    require(a && b && c, name + ": made");
    // at the level its game plays it
    const double level = score_level(style);
    require(level > 0, name + ": has a level");
    const std::vector<float> first = render(*a, 30, 512, level), second = render(*b, 30, 441, level), other = render(*c, 10, 512, level);
    require(first == second, name + ": a seed plays the same whatever the block size");
    require(!std::equal(other.begin(), other.end(), first.begin()), name + ": another seed plays other music");
    for (const float s : first) require(std::isfinite(s), name + ": finite");
    const float peak = peak_of(first, 0, first.size());
    require(peak < 1.0F, name + ": inside full scale");
    // a note in every ten-second stretch: no score runs dry (the quiet ones rest between
    // phrases, but never for ten seconds)
    for (std::size_t k = 0; k < 3; ++k) {
        const std::size_t span = static_cast<std::size_t>(10 * sample_rate) * 2;
        require(peak_of(first, k * span, (k + 1) * span) > .02F, name + ": still playing in stretch " + std::to_string(k));
    }
    // asked to come up from silence, it starts quiet and is back to full in its time
    (*a).fade_in(2.0);
    const std::vector<float> faded = render(*a, 4, 512, level);
    const std::size_t tenth = static_cast<std::size_t>(sample_rate / 10) * 2;
    require(rms(faded, 0, tenth) < rms(faded, faded.size() - 10 * tenth, faded.size()) * .5, name + ": fades in");
    std::printf("%s: peak %.2f, rms %.3f\n", name.c_str(), peak, rms(first, 0, first.size()));
}
// Sudoku's koto garden makes up phrase after phrase and leaves room (ma) between them.
void check_koto(bool night) {
    KotoGarden koto(7, night);
    const std::vector<float> heard = render(koto, 60, 480);
    int quiet = 0, windows = 0;
    for (std::size_t at = 0; at + 960 <= heard.size(); at += 960, ++windows) quiet += rms(heard, at, at + 960) < 1e-3 ? 1 : 0;
    const double whole = rms(heard, 0, heard.size());
    require(koto.phrases() >= 5, "koto: phrase after phrase");
    require(koto.notes_plucked() > 25, "koto: with notes in them");
    require(static_cast<double>(quiet) / windows > .15, "koto: room between the phrases");
    require(whole > .01 && whole < .2, "koto: quiet music, but music");
    std::printf("koto %s: %d phrases, %ld notes, %ld breaths, %.0f%% near silent\n", night ? "night" : "day", koto.phrases(), koto.notes_plucked(),
                koto.breaths(), 100.0 * quiet / windows);
}
}  // namespace

int main() {
    try {
        require(!score_names().empty(), "the engine has scores");
        require(make_score("no such style", 1) == nullptr, "an unknown style makes nothing");
        for (const std::string_view style : score_names()) check_score(style);
        check_koto(false);
        check_koto(true);
    } catch (const std::exception& error) {
        std::cerr << "music: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
