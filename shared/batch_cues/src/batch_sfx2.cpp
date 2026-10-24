#include "batch_kit.hpp"
#include "batch_parts.hpp"

#include <cstdint>

// The batch 2 effects from authoring/audio/work/src/sfx2.py, line for line, for the
// cues the collection plays. Each note is primed from the state synth._rng had when
// `python3 sfx2.py` first rendered it (notes_sfx2.inc), so every cue renders alone
// and equals its master.
namespace batch_cues {
namespace {

constexpr NoteStart script[] = {
#include "notes_sfx2.inc"
};

voice::Signal done(std::span<const double> x, double amt, double tail, double peak_db) {
    const voice::Signal result = finish(verb(x, amt, tail), peak_db);
    return result;
}

// ---------------------------------------------------------------- Gems (E major)
template <int V>
voice::Signal gems_hover() {
    Notes notes(script);
    const voice::Signal& ping = notes.note("ping", 96 + V * 2, .1, .3);
    const voice::Signal result = done(ping, .2, .3, -26);
    return result;
}

template <int V>
voice::Signal gems_swap() {
    Notes notes(script);
    const voice::Signal air = swoosh(.14, 2500, 7000, 10 + V);
    const double midi[2] = {76 + V, 81 + V};
    const voice::Signal bells = seq(notes, midi, "celesta", .07, .5);
    const Part parts[] = {{air, 0, .4}, {bells, 0, .5}};
    const voice::Signal result = done(mix(parts), .2, .4, -13);
    return result;
}

template <int V>
voice::Signal gems_swap_return() {
    Notes notes(script);
    const voice::Signal air = swoosh(.12, 2000, 6000, 20 + V);
    const double midi[3] = {79 - V, 76 - V, 72 - V};
    const voice::Signal bars = seq(notes, midi, "marimba", .08, .45);
    const Part parts[] = {{air, 0, .3}, {bars, 0, .45}};
    const voice::Signal result = done(mix(parts), .15, .3, -15);
    return result;
}

template <int I>
voice::Signal gems_match() {
    Notes notes(script);
    static constexpr double roots[6] = {76, 78, 80, 81, 83, 85};
    const double r = roots[I];
    const double sparkle_notes[4] = {r, r + 4 - (I % 2), r + 7, r + 12};
    const voice::Signal sp = scaled(seq(notes, sparkle_notes, "celesta", .045, .55, .5), .5);
    const double marimba_notes[2] = {r - 12, r - 5};
    const voice::Signal mar = scaled(seq(notes, marimba_notes, "marimba", .045, .5), .35);
    const voice::Signal air = swoosh(.25, 5000, 11000, 30 + I);
    const Part parts[] = {{sp, 0, 1}, {mar, 0, 1}, {air, 0, .08}};
    const voice::Signal result = done(mix(parts), .3, .7, -11 + I * .5);
    return result;
}

voice::Signal gems_star_line() {
    Notes notes(script);
    double run[13] = {};
    for (int k = 0; k < 13; ++k) {
        run[k] = 76 + 2 * k;  // range(76, 101, 2)
    }
    const voice::Signal gl = scaled(seq(notes, run, "celesta", .025, .5, .3), .5);
    const voice::Signal air = swoosh(.45, 3000, 9000, 40);
    const Part parts[] = {{gl, 0, 1}, {air, 0, .25}};
    const voice::Signal result = done(mix(parts), .35, .9, -9);
    return result;
}

voice::Signal gems_bomb() {
    Notes notes(script);
    const voice::Signal thump = glide(notes.synth(), 160, 50, .4, GlideKind::sine, .9, .12);
    voice::Signal tink(voice::samples(1.6), 0.0);
    voice::Generator r(5);  // r = np.random.default_rng(5)
    static constexpr double sparks[6] = {88, 91, 92, 95, 96, 99};
    for (int k = 0; k < 14; ++k) {
        const double m = sparks[r.choice(6)];
        const voice::Signal ping = scaled(notes.note("ping", m, .1, .35), .35);
        const double at = .02 + k * .03 + r.uniform(0, .02);
        place_at(tink, ping, at);
    }
    const std::size_t n = voice::samples(.3);
    const voice::Signal band = nz(n, 41, 200, 2000);
    const voice::Signal e = env(n, .002, .05);
    voice::Signal crack(n);
    for (std::size_t i = 0; i < n; ++i) {
        crack[i] = band[i] * e[i];
    }
    const Part parts[] = {{thump, 0, 1}, {crack, 0, .4}, {tink, 0.03, 1}};
    const voice::Signal result = done(mix(parts), .35, 1.0, -8);
    return result;
}

voice::Signal gems_hypercube() {
    Notes notes(script);
    voice::Signal sw(voice::samples(2.2), 0.0);
    static constexpr double sweep[9] = {64, 68, 71, 75, 76, 80, 83, 87, 88};
    for (int k = 0; k < 9; ++k) {
        const voice::Signal glass = scaled(notes.note("glass", sweep[k], 1.2, .5), .25);
        place_at(sw, glass, k * .07);
    }
    const double top[3] = {88, 95, 100};
    const voice::Signal bells = seq(notes, top, "celesta", .12, .5);
    const Part parts[] = {{sw, 0, 1}, {bells, .5, .4}};
    const voice::Signal result = done(mix(parts), .45, 1.2, -9);
    return result;
}

// ---------------------------------------------------------------- Sudoku (F lydian, felt)
template <int V>
voice::Signal sudoku_note_place() {
    const std::size_t n = voice::samples(.05);
    const voice::Signal band = nz(n, 60 + V, 2500 + 400 * V, 8000);
    const voice::Signal e = env(n, .0005, .006);
    voice::Signal tickp(n);
    for (std::size_t i = 0; i < n; ++i) {
        tickp[i] = band[i] * e[i];
    }
    const voice::Signal result = done(tickp, .1, .2, -20);
    return result;
}

template <int V>
voice::Signal sudoku_digit_place() {
    Notes notes(script);
    static constexpr double digits[3] = {65, 69, 72};
    const voice::Signal& felt = notes.note("felt", digits[V], .5, .45);
    const voice::Signal result = done(felt, .25, .6, -14);
    return result;
}

voice::Signal sudoku_note_remove() {
    const std::size_t n = voice::samples(.05);
    const voice::Signal band = nz(n, 70, 1500, 5000);
    const voice::Signal e = env(n, .0005, .005);
    voice::Signal tick(n);
    for (std::size_t i = 0; i < n; ++i) {
        tick[i] = band[i] * e[i];
    }
    const voice::Signal result = done(tick, .1, .2, -22);
    return result;
}

voice::Signal sudoku_erase() {
    const voice::Signal air = swoosh(.2, 1500, 6000, 71, false, .4);
    const voice::Signal result = done(air, .1, .2, -20);
    return result;
}

voice::Signal sudoku_error() {
    Notes notes(script);
    const voice::Signal& low = notes.note("felt", 53, .35, .35);
    const voice::Signal& rub = notes.note("felt", 54, .3, .25);
    const Part parts[] = {{low, 0, 1}, {rub, .09, .7}};
    const voice::Signal err = mix(parts);
    const voice::Signal filtered = voice::fft_filter(err, 60, 900, 1);
    const voice::Signal result = done(filtered, .15, .4, -17);
    return result;
}

voice::Signal sudoku_complete(std::span<const double> chord) {
    Notes notes(script);
    voice::Signal g(voice::samples(2.5), 0.0);
    for (std::size_t k = 0; k < chord.size(); ++k) {
        const voice::Signal glass = scaled(notes.note("glass", chord[k] + 12, .6, .4), .3);
        place_at(g, glass, static_cast<double>(k) * .06);
    }
    const voice::Signal result = done(g, .4, 1.0, -15);
    return result;
}

voice::Signal sudoku_row_complete() {
    static constexpr double chord[3] = {65, 72, 76};
    const voice::Signal result = sudoku_complete(chord);
    return result;
}

voice::Signal sudoku_column_complete() {
    static constexpr double chord[3] = {69, 76, 79};
    const voice::Signal result = sudoku_complete(chord);
    return result;
}

voice::Signal sudoku_box_complete() {
    static constexpr double chord[5] = {65, 69, 72, 76, 79};
    const voice::Signal result = sudoku_complete(chord);
    return result;
}

// ---------------------------------------------------------------- Untangle (D)
template <int V>
voice::Signal untangle_grab() {
    Notes notes(script);
    const voice::Signal& pizz = notes.note("pizz", 69 + 2 * V, .2, .5);
    const voice::Signal tick = card_tick(80 + V, .8);
    const Part parts[] = {{pizz, 0, 1}, {tick, 0, .25}};
    const voice::Signal result = done(mix(parts), .15, .3, -16);
    return result;
}

template <int V>
voice::Signal untangle_release() {
    Notes notes(script);
    const voice::Signal& bar = notes.note("marimba", 62 + 2 * V, .3, .4);
    const voice::Signal result = done(bar, .2, .4, -17);
    return result;
}

voice::Signal untangle_edges_clear() {
    Notes notes(script);
    const double midi[3] = {74, 78, 81};
    const voice::Signal bars = seq(notes, midi, "marimba", .05, .4, .4);
    const voice::Signal result = done(bars, .3, .6, -16);
    return result;
}

// ---------------------------------------------------------------- Atom Probe (the generic game-over cue)
voice::Signal atom_probe_check_wrong() {
    Notes notes(script);
    const voice::Signal& first = notes.note("analog", 57, .5, .4);
    const voice::Signal& second = notes.note("analog", 56, .6, .35);
    const Part parts[] = {{first, 0, 1}, {second, .2, 1}};
    const voice::Signal result = done(mix(parts), .25, .6, -15);
    return result;
}

// ---------------------------------------------------------------- Switchbox (cute)
voice::Signal switch_click(Notes& notes, int v) {
    const voice::Signal tick = notes.synth().tick(.6);
    const voice::Signal paper = card_tick(static_cast<std::uint64_t>(100 + v), 1.0);
    const Part parts[] = {{tick, 0, .6}, {paper, 0, .3}};
    const voice::Signal result = mix(parts);
    return result;
}

template <int V>
voice::Signal switchbox_switch_on() {
    Notes notes(script);
    const voice::Signal clk = switch_click(notes, V);
    const voice::Signal up = glide(notes.synth(), 700 + 60 * V, 1200 + 90 * V, .09, GlideKind::sine, .35, .05);
    const Part parts[] = {{clk, 0, 1}, {up, .01, 1}};
    const voice::Signal result = done(mix(parts), .2, .3, -15);
    return result;
}

template <int V>
voice::Signal switchbox_switch_off() {
    Notes notes(script);
    const voice::Signal clk = switch_click(notes, V);
    const voice::Signal down = glide(notes.synth(), 1100 + 60 * V, 650 + 50 * V, .09, GlideKind::sine, .35, .05);
    const Part parts[] = {{clk, 0, 1}, {down, .01, 1}};
    const voice::Signal result = done(mix(parts), .2, .3, -15);
    return result;
}

voice::Signal switchbox_reach() {
    Notes notes(script);
    const voice::Signal whistle = glide(notes.synth(), 500, 1400, .25, GlideKind::flute, .4, .2);
    const double midi[4] = {88, 91, 88, 91};
    const voice::Signal box = seq(notes, midi, "musicbox", .05, .4, .2);
    const voice::Signal block = notes.synth().woodblock(.3, true);
    const Part parts[] = {{whistle, 0, 1}, {box, .2, .4}, {block, .45, .3}};
    const voice::Signal result = done(mix(parts), .25, .5, -12);
    return result;
}

voice::Signal switchbox_reset() {
    Notes notes(script);
    const double midi[7] = {93, 90, 88, 85, 81, 78, 76};
    const voice::Signal box = scaled(seq(notes, midi, "musicbox", .045, .45, .3), .6);
    const voice::Signal result = done(box, .3, .8, -13);
    return result;
}

voice::Signal switchbox_unlock() {
    Notes notes(script);
    const double midi[4] = {81, 85, 88, 93};
    const voice::Signal bells = seq(notes, midi, "celesta", .06, .55, .5);
    const voice::Signal ting = notes.synth().triangle(.15);
    const voice::Signal& vibes = notes.note("vibes", 81, .8, .4);
    const Part parts[] = {{bells, 0, .6}, {ting, .2, 1}, {vibes, .2, .3}};
    const voice::Signal result = done(mix(parts), .4, 1.2, -10);
    return result;
}

voice::Signal switchbox_giggle_boing() {
    Notes notes(script);
    const voice::Signal up = glide(notes.synth(), 400, 900, .12, GlideKind::flute, .4, .08);
    const voice::Signal down = glide(notes.synth(), 900, 500, .12, GlideKind::flute, .3, .08);
    const voice::Signal again = glide(notes.synth(), 500, 1000, .14, GlideKind::flute, .3, .1);
    const Part parts[] = {{up, 0, 1}, {down, .14, 1}, {again, .28, 1}};
    const voice::Signal result = done(mix(parts), .2, .4, -14);
    return result;
}

// ---------------------------------------------------------------- Puzzle Solve (Bb, toy wood)
voice::Signal puzzle_solve_pickup() {
    Notes notes(script);
    const voice::Signal block = notes.synth().woodblock(.35, true);
    const voice::Signal air = swoosh(.1, 2000, 6000, 120);
    const Part parts[] = {{block, 0, .5}, {air, 0, .3}};
    const voice::Signal result = done(mix(parts), .1, .2, -16);
    return result;
}

template <int V>
voice::Signal puzzle_solve_rotate() {
    Notes notes(script);
    voice::Signal rat(voice::samples(.2), 0.0);
    for (int k = 0; k < 3; ++k) {
        const voice::Signal tick = scaled(card_tick(static_cast<std::uint64_t>(130 + 3 * V + k), .9), .5 + .2 * k);
        place_at(rat, tick, k * .03);
    }
    const voice::Signal& bar = notes.note("marimba", 70 + 2 * V, .2, .4);
    const Part parts[] = {{rat, 0, .7}, {bar, .06, .5}};
    const voice::Signal result = done(mix(parts), .1, .25, -15);
    return result;
}

template <int V>
voice::Signal puzzle_solve_flip() {
    Notes notes(script);
    const voice::Signal air = swoosh(.16, 1200, 5000, 140 + V);
    const voice::Signal block = notes.synth().woodblock(.4, V == 0);
    const Part parts[] = {{air, 0, .5}, {block, .14, .5}};
    const voice::Signal result = done(mix(parts), .12, .3, -15);
    return result;
}

voice::Signal puzzle_solve_place() {
    Notes notes(script);
    const voice::Signal thud = glide(notes.synth(), 180, 90, .15, GlideKind::sine, .8, .04);
    const voice::Signal block = notes.synth().woodblock(.3, false);
    const Part parts[] = {{thud, 0, 1}, {block, 0, .4}};
    const voice::Signal result = done(mix(parts), .1, .2, -15);
    return result;
}

voice::Signal puzzle_solve_no_fit() {
    Notes notes(script);
    const voice::Signal first = notes.synth().woodblock(.35, false);
    const voice::Signal second = notes.synth().woodblock(.3, false);
    const Part parts[] = {{first, 0, .5}, {second, .1, .4}};
    const voice::Signal result = done(mix(parts), .1, .2, -18);
    return result;
}

// ---------------------------------------------------------------- shared top-score flow + new game
template <int V>
voice::Signal ui_name_key() {
    Notes notes(script);
    const voice::Signal tick = notes.synth().tick(.5);
    const voice::Signal& vibes = notes.note("vibes", 84 + 2 * V, .1, .3);
    const Part parts[] = {{tick, 0, .5}, {vibes, 0, .4}};
    const voice::Signal result = done(mix(parts), .15, .3, -18);
    return result;
}

voice::Signal ui_name_backspace() {
    Notes notes(script);
    const voice::Signal tick = notes.synth().tick(.4);
    const voice::Signal& vibes = notes.note("vibes", 79, .1, .25);
    const Part parts[] = {{tick, 0, .5}, {vibes, 0, .4}};
    const voice::Signal result = done(mix(parts), .15, .3, -20);
    return result;
}

voice::Signal ui_name_confirm() {
    Notes notes(script);
    const double midi[3] = {79, 84, 88};
    const voice::Signal vibes = seq(notes, midi, "vibes", .07, .5, .6);
    const voice::Signal result = done(vibes, .35, .8, -12);
    return result;
}

voice::Signal ui_new_game() {
    Notes notes(script);
    const voice::Signal air = swoosh(.35, 1500, 7000, 170);
    const double midi[2] = {72, 79};
    const voice::Signal vibes = seq(notes, midi, "vibes", .1, .45, .5);
    const Part parts[] = {{air, 0, .3}, {vibes, .1, .5}};
    const voice::Signal result = done(mix(parts), .3, .7, -14);
    return result;
}

// ---------------------------------------------------------------- Switchbox's stingers
// STING['switchbox'] = ('celesta', 'pizz', 9, [4, 7, 11, 14], 'musicbox'); both are written
// stereo with equal channels, so the mono master plays the same.
constexpr double sting_key = 9;
constexpr double sting_base = 60 + sting_key;

// stinger(lead, comp, key, chord, extra): a rising lead flourish, then a warm held tonic chord
voice::Signal stinger_win_switchbox() {
    Notes notes(script);
    voice::Signal y(voice::samples(6), 0.0);
    static constexpr double run[8] = {0, 4, 7, 12, 11, 12, 16, 19};
    static constexpr double times[8] = {0, .1, .2, .3, .45, .55, .7, .9};
    for (int i = 0; i < 8; ++i) {
        const double m = sting_base + run[i] + 12;  // the lead (celesta) sounds an octave up
        const double dur = i == 7 ? 1.4 : .14;
        const voice::Signal lead = scaled(notes.note("celesta", m, dur, .65), 0.3);
        place_at(y, lead, times[i]);
    }
    static constexpr double chord[4] = {4, 7, 11, 14};
    for (const double m : chord) {
        const voice::Signal comp = scaled(notes.note("pizz", sting_base - 12 + m, 2.2, .5), 0.18);
        place_at(y, comp, 0.9);
    }
    const voice::Signal bass = scaled(notes.note("bass", sting_base - 24 + 12, 2.2, .7), 0.45);
    place_at(y, bass, 0.9);
    const voice::Signal ting = notes.synth().triangle(0.18);
    place_at(y, ting, 0.9);
    const voice::Signal extra = scaled(notes.note("musicbox", sting_base + 24, 1.5, .5), 0.2);
    place_at(y, extra, 0.9);
    const voice::Signal result = done(y, 0.35, 1.5, -6);
    return result;
}

// topscore(lead, comp, key, extra, bass): repeated pickup notes, a leap to the octave, an add9 chord
voice::Signal stinger_topscore_switchbox() {
    Notes notes(script);
    voice::Signal y(voice::samples(5.5), 0.0);
    static constexpr double lead_notes[6][3] = {{7, 0, .1}, {7, .12, .1}, {7, .24, .1},
                                                {12, .4, .3}, {14, .75, .15}, {16, .9, 1.4}};
    for (const double* note : lead_notes) {
        const double m = sting_base + note[0] + 12;  // celesta, an octave up
        const voice::Signal lead = scaled(notes.note("celesta", m, note[2], .62), .3);
        place_at(y, lead, note[1]);
    }
    static constexpr double chord[4] = {0, 4, 7, 14};
    for (const double m : chord) {
        const voice::Signal comp = scaled(notes.note("pizz", sting_base - 12 + m, 2.0, .45), .17);
        place_at(y, comp, .9);
    }
    const voice::Signal bass = scaled(notes.note("bass", sting_base - 24 + 12, 2.0, .6), .4);
    place_at(y, bass, .9);
    const voice::Signal extra = scaled(notes.note("musicbox", sting_base + 28, 1.0, .45), .18);
    place_at(y, extra, .9);
    const voice::Signal result = done(y, .35, 1.5, -6);
    return result;
}

constexpr voice::Cue cues[] = {
    {.name = "gems_hover_01", .mono = gems_hover<0>},
    {.name = "gems_hover_02", .mono = gems_hover<1>},
    {.name = "gems_hover_03", .mono = gems_hover<2>},
    {.name = "gems_swap_01", .mono = gems_swap<0>},
    {.name = "gems_swap_02", .mono = gems_swap<1>},
    {.name = "gems_swap_03", .mono = gems_swap<2>},
    {.name = "gems_swap_return_01", .mono = gems_swap_return<0>},
    {.name = "gems_swap_return_02", .mono = gems_swap_return<1>},
    {.name = "gems_match_01", .mono = gems_match<0>},
    {.name = "gems_match_02", .mono = gems_match<1>},
    {.name = "gems_match_03", .mono = gems_match<2>},
    {.name = "gems_match_04", .mono = gems_match<3>},
    {.name = "gems_match_05", .mono = gems_match<4>},
    {.name = "gems_star_line", .mono = gems_star_line},
    {.name = "gems_bomb", .mono = gems_bomb},
    {.name = "gems_hypercube", .mono = gems_hypercube},
    {.name = "sudoku_note_place_01", .mono = sudoku_note_place<0>},
    {.name = "sudoku_note_place_02", .mono = sudoku_note_place<1>},
    {.name = "sudoku_note_place_03", .mono = sudoku_note_place<2>},
    {.name = "sudoku_digit_place_01", .mono = sudoku_digit_place<0>},
    {.name = "sudoku_digit_place_02", .mono = sudoku_digit_place<1>},
    {.name = "sudoku_digit_place_03", .mono = sudoku_digit_place<2>},
    {.name = "sudoku_note_remove", .mono = sudoku_note_remove},
    {.name = "sudoku_erase", .mono = sudoku_erase},
    {.name = "sudoku_error", .mono = sudoku_error},
    {.name = "sudoku_row_complete", .mono = sudoku_row_complete},
    {.name = "sudoku_column_complete", .mono = sudoku_column_complete},
    {.name = "sudoku_box_complete", .mono = sudoku_box_complete},
    {.name = "untangle_grab_01", .mono = untangle_grab<0>},
    {.name = "untangle_grab_02", .mono = untangle_grab<1>},
    {.name = "untangle_grab_03", .mono = untangle_grab<2>},
    {.name = "untangle_release_01", .mono = untangle_release<0>},
    {.name = "untangle_release_02", .mono = untangle_release<1>},
    {.name = "untangle_release_03", .mono = untangle_release<2>},
    {.name = "untangle_edges_clear", .mono = untangle_edges_clear},
    {.name = "atom_probe_check_wrong", .mono = atom_probe_check_wrong},
    {.name = "switchbox_switch_on_01", .mono = switchbox_switch_on<0>},
    {.name = "switchbox_switch_on_02", .mono = switchbox_switch_on<1>},
    {.name = "switchbox_switch_on_03", .mono = switchbox_switch_on<2>},
    {.name = "switchbox_switch_off_01", .mono = switchbox_switch_off<0>},
    {.name = "switchbox_switch_off_02", .mono = switchbox_switch_off<1>},
    {.name = "switchbox_switch_off_03", .mono = switchbox_switch_off<2>},
    {.name = "switchbox_reach", .mono = switchbox_reach},
    {.name = "switchbox_reset", .mono = switchbox_reset},
    {.name = "switchbox_unlock", .mono = switchbox_unlock},
    {.name = "switchbox_giggle_boing", .mono = switchbox_giggle_boing},
    {.name = "puzzle_solve_pickup", .mono = puzzle_solve_pickup},
    {.name = "puzzle_solve_rotate_01", .mono = puzzle_solve_rotate<0>},
    {.name = "puzzle_solve_rotate_02", .mono = puzzle_solve_rotate<1>},
    {.name = "puzzle_solve_rotate_03", .mono = puzzle_solve_rotate<2>},
    {.name = "puzzle_solve_flip_01", .mono = puzzle_solve_flip<0>},
    {.name = "puzzle_solve_flip_02", .mono = puzzle_solve_flip<1>},
    {.name = "puzzle_solve_place", .mono = puzzle_solve_place},
    {.name = "puzzle_solve_no_fit", .mono = puzzle_solve_no_fit},
    {.name = "ui_name_key_01", .mono = ui_name_key<0>},
    {.name = "ui_name_key_02", .mono = ui_name_key<1>},
    {.name = "ui_name_key_03", .mono = ui_name_key<2>},
    {.name = "ui_name_backspace", .mono = ui_name_backspace},
    {.name = "ui_name_confirm", .mono = ui_name_confirm},
    {.name = "ui_new_game", .mono = ui_new_game},
    {.name = "stinger_win_switchbox", .mono = stinger_win_switchbox},
    {.name = "stinger_topscore_switchbox", .mono = stinger_topscore_switchbox},
};

}  // namespace

std::span<const voice::Cue> sfx2_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

}  // namespace batch_cues
