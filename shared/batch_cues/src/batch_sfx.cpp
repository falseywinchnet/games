#include "batch_kit.hpp"
#include "batch_parts.hpp"

#include <algorithm>
#include <cmath>

// The card games' interface cues from authoring/audio/work/src/sfx.py, line for line.
// Each note is primed from the state synth._rng had when `python3 sfx.py` first
// rendered it (notes_sfx.inc), so every cue renders alone and equals its master.
namespace batch_cues {
namespace {

constexpr NoteStart script[] = {
#include "notes_sfx.inc"
};

// undo(v): a rising swish into two falling vibes notes
template <int V>
voice::Signal undo() {
    Notes notes(script);
    const std::size_t n = voice::samples(0.3);
    const voice::Signal t = voice::tarr(n);
    const voice::Signal band = nz(n, 300 + V, 1500, 6000);
    voice::Signal sw(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double ramp = t[i] / t[n - 1];
        const double cut = std::exp(-std::max(t[i] - 0.22, 0.0) / 0.01);
        sw[i] = band[i] * (ramp * ramp) * cut * 0.4;
    }
    const double midi[2] = {76 - 2 * V, 71 - 2 * V};
    const voice::Signal c = scaled(chime(notes, midi, "vibes", 0.4, 0.07, 0.3), 0.35);
    voice::Signal y(c.size() + n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] += sw[i];
    }
    place_at(y, c, 0.18);
    const voice::Signal result = finish(verb(y, 0.2, 0.4), -12);
    return result;
}

// hint(v): a rising celesta triad
template <int V>
voice::Signal hint() {
    Notes notes(script);
    static constexpr double sequences[2][3] = {{84, 88, 91}, {86, 91, 95}};
    const voice::Signal x = scaled(chime(notes, sequences[V], "celesta", 0.45, 0.08), 0.5);
    const voice::Signal result = finish(verb(x, 0.3, 0.8), -12);
    return result;
}

// invalid(v): a soft falling marimba pair, darkened
template <int V>
voice::Signal invalid() {
    Notes notes(script);
    static constexpr double sequences[2][2] = {{55, 52}, {53, 50}};
    const voice::Signal x = chime(notes, sequences[V], "marimba", 0.45, 0.11, 0.3);
    const voice::Signal filtered = voice::fft_filter(x, 80, 1400, 1);
    const voice::Signal result = finish(verb(filtered, 0.1, 0.3), -14);
    return result;
}

constexpr voice::Cue cues[] = {
    {.name = "ui_undo_01", .mono = undo<0>},       {.name = "ui_undo_02", .mono = undo<1>},
    {.name = "ui_hint_01", .mono = hint<0>},       {.name = "ui_hint_02", .mono = hint<1>},
    {.name = "ui_invalid_01", .mono = invalid<0>}, {.name = "ui_invalid_02", .mono = invalid<1>},
};

}  // namespace

std::span<const voice::Cue> sfx_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

}  // namespace batch_cues
