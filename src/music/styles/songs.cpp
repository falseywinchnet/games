// Written songs, the game-show themes the owner approved (2026-10-15). They came from
// studying what makes a 1970s show's theme feel right (The Price Is Right's: F major near
// 118 beats a minute, a narrow singable tune sitting sweetly on rich chords, single Moog
// lines layered with organ, piano and brass, the theme cut into parts with jobs) and from
// the owner's verdict on the attempts: a theme finds one idea and stays on it. None of
// that show's tune is here.
//
//   velvet_prize   two chords (Bb add9, F suspended), an electric piano, bass and light
//                  drums; the tune on the piano says its hook three times, answers once
//   walk_on        a two-bar boogie vamp that can loop as long as a walk takes, the theme
//                  over it (then with the brass in thirds), an arrival, and a breath
#include "song_band.hpp"

namespace games::music {
namespace {
using In = Instrument;
}  // namespace

Song velvet_prize() {
    // The opening's two chords, its electric piano, bass and drums are the whole piece: the
    // tune sits on them, says its hook three times and answers it once, and goes round.
    Song song;
    song.title = "Velvet Prize";
    song.bpm = 112;
    song.room_seconds = 1.6;
    const int kit = add_kit(song);
    add_drums(song, kit, In::kick, "x.......x.x.....", .75);
    add_drums(song, kit, In::rim, "....x.......x...", .7, .1);
    add_drums(song, kit, In::hat, "x.x.x.x.x.x.x.x.", .55, .3);
    add_drums(song, kit, In::shaker, "xoxoxoxoxoxoxoxo", .5, -.3);
    const char* chords = "Bbadd9:4 | F9sus:4 |";
    const char* keys = "*:1.5 *:1 *:1.5 |";
    const char* bass = "1:1.5 5:.5 8:1 5:1 |";

    const int intro = add_section(song, "intro", chords);
    // the piano's chords sit back: the same piano carries the tune
    add_part(song, intro, In::electric_piano, keys, .32, -.2, .95, 0, 55);
    add_part(song, intro, In::synth_bass, bass, .75, 0, .9, 0, 34);

    const int theme = add_section(song, "theme", "Bbadd9:4 | F9sus:4 | Bbadd9:4 | F9sus:4 | Bbadd9:4 | F9sus:4 | Bbadd9:4 | F9sus:4 |");
    add_part(song, theme, In::electric_piano,
             "D5:1.5 C5:.5 D5:1 F5:1 | Eb5:1.5 C5:.5 F5:2 | D5:1.5 C5:.5 D5:1 F5:1 | Eb5:1.5 C5:.5 F5:2 |"
             " D5:1.5 C5:.5 D5:1 F5:1 | Eb5:1.5 C5:.5 F5:2 | D5:1.5 C5:.5 D5:1 F5:1 | G5:1 F5:1 C5:2 |",
             .8, .05, .95);
    add_part(song, theme, In::electric_piano, keys, .32, -.2, .95, 0, 55);
    add_part(song, theme, In::synth_bass, bass, .75, 0, .9, 0, 34);

    song.order = {intro, theme};
    song.loop_from = 1;
    set_mix(song, In::electric_piano, .17, .25);
    set_mix(song, In::synth_bass, 1.15, .03);
    set_mix(song, In::kick, .2, .03);
    set_mix(song, In::rim, .08, .12);
    set_mix(song, In::hat, .07, .05);
    set_mix(song, In::shaker, .04, .05);
    return song;
}

Song walk_on() {
    Song song;
    song.title = "Walk-On";
    song.bpm = 118;
    const int kit = add_kit(song);
    add_drums(song, kit, In::kick, "x...x...x...x...", .8);
    add_drums(song, kit, In::snare, "....x.......x...", .7, .05);
    add_drums(song, kit, In::hat, "x.x.x.x.x.x.x.x.", .6, .3);
    add_drums(song, kit, In::open_hat, "......x.......x.", .55, .3);
    const char* bass = "1:.5 3:.5 5:.5 6:.5 7:.5 6:.5 5:.5 3:.5 |";   // the boogie's walk
    const char* organ = "r:.5 *:.5 r:.5 *:.5 r:.5 *:.5 r:.5 *:.5 |";  // off-beats
    const char* guitar = "r:1 *:.5 r:1.5 *:.5 r:.5 |";                  // two and four

    // the vamp: two bars that can go round for as long as the walk takes
    const int vamp = add_section(song, "vamp", "Eb7:4 | Ab7:4 |");
    add_part(song, vamp, In::synth_bass, bass, .8, 0, .9, 0, 27);
    add_part(song, vamp, In::organ, organ, .55, -.25, .7, 0, 55);
    add_part(song, vamp, In::guitar, guitar, .6, .3, .5, 0, 60);

    const char* t_chords = "Eb:4 | Ab7:4 | Eb:4 | Bb7:4 | Eb:4 | Ab7:4 | Fm7:2 Bb7:2 | Eb:4 |";
    const char* t_tune = "G4:.5 Bb4:.5 Eb5:.5 G5:1.5 F5:.5 Eb5:.5 | Gb5:1 Eb5:.5 C5:.5 Ab4:2 | G4:.5 Bb4:.5 Eb5:.5 G5:.5 Bb5:2 | Ab5:1 F5:.5 D5:.5 Bb4:2 |"
                         " Eb5:.5 Eb5:.5 G5:.5 Bb5:1 G5:.5 Bb5:1 | C6:1.5 Bb5:.5 Ab5:1 Gb5:1 | F5:1 Ab5:1 D5:1 F5:1 | Eb5:2 r:2 |";
    const int theme = add_section(song, "theme", t_chords);
    add_part(song, theme, In::synth_lead, t_tune, .8, .05, .9);
    add_part(song, theme, In::synth_bass, bass, .8, 0, .9, 0, 27);
    add_part(song, theme, In::organ, organ, .5, -.25, .7, 0, 55);
    add_part(song, theme, In::guitar, guitar, .55, .3, .5, 0, 60);
    song.sections[static_cast<std::size_t>(theme)].crash = true;
    song.sections[static_cast<std::size_t>(theme)].fill = true;

    // the theme again with the brass on it, in thirds
    const int theme2 = add_section(song, "theme2", t_chords);
    add_part(song, theme2, In::synth_lead, t_tune, .8, .05, .9);
    add_part(song, theme2, In::brass, t_tune, .55, -.15, .85, 1);
    add_part(song, theme2, In::synth_bass, bass, .8, 0, .9, 0, 27);
    add_part(song, theme2, In::organ, organ, .5, -.25, .7, 0, 55);
    add_part(song, theme2, In::guitar, guitar, .55, .3, .5, 0, 60);
    song.sections[static_cast<std::size_t>(theme2)].crash = true;

    // the arrival: four, five, home, and a breath
    const int arrival = add_section(song, "arrival", "Ab:2 Bb:2 | Eb:4 |");
    add_part(song, arrival, In::brass, "C5:.5 Eb5:.5 Ab5:1 D5:.5 F5:.5 Bb5:1 | Eb6:1 r:3 |", .8, -.1, .85, 2);
    add_part(song, arrival, In::synth_lead, "C5:.5 Eb5:.5 Ab5:1 D5:.5 F5:.5 Bb5:1 | Eb6:1 r:3 |", .8, .05, .9);
    add_part(song, arrival, In::synth_bass, "1:.5 1:.5 1:1 1:.5 1:.5 1:1 | 1:1 r:3 |", .85, 0, .9, 0, 27);
    song.sections[static_cast<std::size_t>(arrival)].stop = true;

    song.order = {vamp, vamp, theme, vamp, theme2, arrival};
    song.loop_from = 0;
    set_mix(song, In::synth_lead, 2.8, .25);
    set_mix(song, In::brass, .45, .25);
    set_mix(song, In::organ, .2, .2);
    set_mix(song, In::guitar, .18, .15);
    set_mix(song, In::synth_bass, .95, .03);
    set_mix(song, In::kick, .22, .03);
    set_mix(song, In::snare, .45, .12);
    set_mix(song, In::hat, .08, .05);
    set_mix(song, In::open_hat, .06, .08);
    set_mix(song, In::crash, .1, .15);
    set_mix(song, In::tom, .25, .1);
    return song;
}

}  // namespace games::music
