// Nature Cube's live music and effects: bounded, varied, deterministic per seed, tuned
// to each other, and quiet when there is nothing to play.
#include "glass_effects.hpp"
#include "glass_music.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

struct Levels {
    double rms = 0, peak = 0;
    bool finite = true;
};

Levels measure(const std::vector<float>& stereo) {
    Levels levels;
    double sum = 0;
    for (float v : stereo) {
        if (!std::isfinite(v))
            levels.finite = false;
        sum += static_cast<double>(v) * v;
        levels.peak = std::fmax(levels.peak, std::fabs(static_cast<double>(v)));
    }
    levels.rms = std::sqrt(sum / std::fmax(1.0, static_cast<double>(stereo.size())));
    return levels;
}

double db(double value) {
    return 20 * std::log10(value + 1e-12);
}

void cue_names() {
    using ps_cube::Cue;
    check(ps_cube::cue_for("nature_cube_pick") == Cue::pick, "pick name");
    check(ps_cube::cue_for("nature_cube_trace") == Cue::step, "step name");
    check(ps_cube::cue_for("nature_cube_erase") == Cue::erase, "erase name");
    check(ps_cube::cue_for("nature_cube_connect") == Cue::connect, "connect name");
    check(ps_cube::cue_for("nature_cube_blocked") == Cue::blocked, "blocked name");
    check(ps_cube::cue_for("nature_cube_turn") == Cue::turn, "turn name");
    check(ps_cube::cue_for("ui_new_game") == Cue::level, "level name");
    check(ps_cube::cue_for("stinger_win_nature_cube") == Cue::win, "win name");
    check(ps_cube::cue_for("stinger_topscore_nature_cube") == Cue::win, "top score name");
    check(ps_cube::cue_for("ui_name_confirm") == Cue::none, "other names fall through");
    ps_cube::CueRing ring;
    ring.push(Cue::pick);
    ring.push(Cue::win);
    Cue cue = Cue::none;
    check(ring.pop(cue) && cue == Cue::pick, "ring keeps order");
    check(ring.pop(cue) && cue == Cue::win, "ring keeps order (2)");
    check(!ring.pop(cue), "ring empties");
    for (int k = 0; k < 200; ++k)
        ring.push(Cue::step);
    int popped = 0;
    while (ring.pop(cue))
        ++popped;
    check(popped == 63, "a full ring drops the newest cues");
}

void music_is_bounded_and_varied() {
    ps_cube::Harmony harmony;
    ps_cube::GlassMusic music(17, &harmony);
    const int block = 480;
    std::vector<float> window(static_cast<std::size_t>(30 * ps_cube::sample_rate) * 2);
    std::set<int> tonics, modes;
    double lowest = 0, highest = -200, peak = 0;
    bool finite = true, harmony_matches = true;
    // Thirty minutes, joining a pair every forty seconds and winning every five minutes.
    for (int w = 0; w < 60; ++w) {
        std::fill(window.begin(), window.end(), 0.0F);
        for (std::size_t frame = 0; frame * 2 < window.size(); frame += block) {
            const std::size_t second = (w * 30 * ps_cube::sample_rate + frame) / ps_cube::sample_rate;
            if (frame % (ps_cube::sample_rate) < static_cast<std::size_t>(block)) {
                if (second % 40 == 39)
                    music.cue(ps_cube::Cue::connect);
                if (second % 300 == 299)
                    music.cue(ps_cube::Cue::win);
                if (second % 300 == 10)
                    music.cue(ps_cube::Cue::level);
            }
            music.render_add(std::span<float>(window.data() + frame * 2, block * 2), 1.0);
            if (harmony.chord.load() != music.chord_mask() || harmony.tonic.load() != music.tonic())
                harmony_matches = false;
        }
        const Levels levels = measure(window);
        finite = finite && levels.finite;
        peak = std::fmax(peak, levels.peak);
        if (w > 0) {
            lowest = std::fmin(lowest, db(levels.rms));
            highest = std::fmax(highest, db(levels.rms));
        }
        tonics.insert(music.tonic());
        modes.insert(music.mode());
        check(music.tempo() >= 82 && music.tempo() <= 95, "tempo stays slow");
    }
    std::printf("music: 30 s windows %.1f..%.1f dBFS, peak %.1f dBFS, %d sections, %zu keys, %zu modes\n",
                lowest, highest, db(peak), music.sections_played(), tonics.size(), modes.size());
    check(finite, "music is finite");
    check(peak < .7, "music leaves headroom");
    check(lowest > -34 && highest < -15, "music stays faint but present");
    check(music.sections_played() > 30, "music moves through sections");
    check(tonics.size() >= 3 && modes.size() >= 2, "music changes key and mode");
    check(harmony_matches, "the published harmony is the sounding chord");
}

void music_fades_in_and_resolves() {
    ps_cube::Harmony harmony;
    ps_cube::GlassMusic first(5, &harmony), second(5, nullptr);
    std::vector<float> a(ps_cube::sample_rate / 20 * 2, 0.0F), b(a.size(), 0.0F);
    first.render_add(a, 1.0);
    second.render_add(b, 1.0);
    check(measure(a).peak < .003, "the music fades in");
    std::vector<float> c(ps_cube::sample_rate * 10 * 2, 0.0F), d(c.size(), 0.0F);
    first.render_add(c, 1.0);
    second.render_add(d, 1.0);
    check(c == d, "a seed always plays the same music");
    first.cue(ps_cube::Cue::win);
    std::vector<float> e(ps_cube::sample_rate * 2, 0.0F);
    first.render_add(e, 1.0);
    check(first.kind() == ps_cube::GlassMusic::glow, "a win settles into the glow");
    check((first.chord_mask() & (1 << first.tonic())) != 0, "a win resolves to the tonic");
    check(first.brightness() > .5, "a win brightens the music");
    ps_cube::GlassMusic third(9, nullptr);
    const double before = third.brightness();
    third.cue(ps_cube::Cue::connect);
    check(third.brightness() > before + .3, "a joined pair brightens the music");
}

void effects_sound_and_settle() {
    ps_cube::Harmony harmony;
    ps_cube::GlassMusic music(3, &harmony);
    std::vector<float> scratch(960, 0.0F);
    music.render_add(scratch, 1.0);
    ps_cube::GlassEffects effects(4, &harmony);
    std::vector<float> idle(ps_cube::sample_rate * 2, 0.0F);
    effects.render_add(idle, 1.0);
    check(measure(idle).peak == 0, "idle effects are silent");
    const ps_cube::Cue cues[8] = {ps_cube::Cue::pick,    ps_cube::Cue::step,  ps_cube::Cue::erase,
                                  ps_cube::Cue::connect, ps_cube::Cue::blocked, ps_cube::Cue::turn,
                                  ps_cube::Cue::level,   ps_cube::Cue::win};
    const char* names[8] = {"pick", "step", "erase", "connect", "blocked", "turn", "level", "win"};
    for (int k = 0; k < 8; ++k) {
        for (int n = 0; n < (cues[k] == ps_cube::Cue::turn ? 15 : 1); ++n)
            effects.cue(cues[k]);  // the air needs a sustained sweep
        std::vector<float> sound(ps_cube::sample_rate * 6 * 2, 0.0F);
        effects.render_add(std::span<float>(sound.data(), ps_cube::sample_rate / 5 * 2), 1.0);
        const Levels onset = measure(std::vector<float>(sound.begin(), sound.begin() + ps_cube::sample_rate / 5 * 2));
        effects.render_add(std::span<float>(sound.data() + ps_cube::sample_rate / 5 * 2,
                                            sound.size() - ps_cube::sample_rate / 5 * 2), 1.0);
        const Levels all = measure(sound);
        std::printf("effect %-8s peak %.1f dBFS\n", names[k], db(all.peak));
        check(onset.peak > .003, names[k]);
        check(all.finite && all.peak < .8, "effects are bounded");
        // and then the voice rests: it renders nothing at all
        std::vector<float> after(ps_cube::sample_rate / 2 * 2, 0.0F);
        double waited = 0;
        for (; waited < 30; waited += .5) {
            std::fill(after.begin(), after.end(), 0.0F);
            effects.render_add(after, 1.0);
            if (measure(after).peak == 0)
                break;
        }
        std::printf("effect %-8s rests after %.1f s\n", names[k], waited + 6);
        check(effects.quiet() && waited < 30, "effects settle to silence");
    }
    // A win is the loudest thing; a step is among the softest.
    ps_cube::GlassEffects compare(6, &harmony);
    std::vector<float> step(ps_cube::sample_rate * 2, 0.0F), win(ps_cube::sample_rate * 6 * 2, 0.0F);
    compare.cue(ps_cube::Cue::step);
    compare.render_add(step, 1.0);
    compare.cue(ps_cube::Cue::win);
    compare.render_add(win, 1.0);
    check(measure(step).rms < measure(win).rms, "a step is softer than the win");
}

} // namespace

int main() {
    cue_names();
    music_fades_in_and_resolves();
    effects_sound_and_settle();
    music_is_bounded_and_varied();
    if (failures)
        std::printf("%d failure(s)\n", failures);
    else
        std::printf("cube audio: all checks passed\n");
    return failures ? 1 : 0;
}
