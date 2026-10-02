# PlaySuite music (v2)

Original music for the PlaySuite collection, written as code: a self-contained C++20 offline
synthesizer and sequencer (no third-party libraries) plus the scores, rendered to 48 kHz stereo
24-bit masters and packaged under the existing runtime names in `assets/audio`.

This replaces the earlier lounge-style loops (numpy synth in `authoring/audio/work/src`) for eleven
games and fourteen win/top-score stingers. All melodies, harmonies and arrangements here are
original. The menu theme is written in the idiom of a 1970s TV game-show theme; it does not quote
or paraphrase any existing theme.

## Layout

| Path | Contents |
| --- | --- |
| `src/dsp.hpp` | polyBLEP oscillators, SVF and ladder filters, biquads, ADSR, delay lines |
| `src/voice.hpp/.cpp` | instruments: brass section, analog lead, funk/slap/synth/sub/glide basses, FM electric piano, clavinet, organ, strings, pad, choir, FM bells, glockenspiel, celesta, music box, vibraphone, marimba, kalimba, timpani, Karplus-Strong guitar/muted guitar/harp/pizzicato/upright bass, additive piano, pluck synth, bell lead, flute, clarinet, muted trumpet, slide whistle, drum kit and hand percussion, brushes, water/air beds, birds |
| `src/score.hpp/.cpp` | score data model; melody notation (`"D6:qd C6:e Bb5:q"`), drum grids, chord symbols, voice leading |
| `src/songs_common.cpp` | arranging helpers (pads, comping, arpeggios, bass patterns, walking bass, block harmony) and the registry |
| `src/song_menu.cpp` | menu theme |
| `src/song_cards.cpp` | Klondike, Spider, FreeCell, Hearts |
| `src/song_puzzles.cpp` | Sudoku day/night, Gems, Nature Cube, Untangle, Puzzle Solve |
| `src/stingers.cpp` | win and top-score stingers |
| `src/mix.cpp` | rendering, sends, FDN reverb, ping-pong delay, bus compressor, loudness (BS.1770 gating), true-peak limiter, loop folding, analysis, WAV output |
| `src/main.cpp` | command line (`render`, `list`, `lint`, `analyse`) |
| `src/song_test.cpp` | `instrument_test`: one phrase per instrument, used to calibrate levels (rendered only on request) |
| `integrate.py` | encodes AAC, verifies decode alignment and seams, updates manifests, builds the runtime directory |

## Rebuilding

Requires g++ with C++20 (the Windows build used the MSYS2 MinGW-w64 GCC 16.2 toolchain) and an
ffmpeg with the native `aac` and `libvorbis` encoders. Commands below run from the repository root
with POSIX `sh`; outputs go to the ignored `.build/` tree.

```sh
sh authoring/playsuite_music/build.sh                       # one g++ process -> .build/music-v2/bin/playsuite_music.exe
.build/music-v2/bin/playsuite_music.exe lint                # harmonic self-check
.build/music-v2/bin/playsuite_music.exe render .build/music-v2/out music_gems_loop   # one track
.build/music-v2/bin/playsuite_music.exe render .build/music-v2/out                   # everything (~5 min)
python3 authoring/playsuite_music/integrate.py --ffmpeg <ffmpeg>   # package + runtime + verify
```

`render` writes `<id>.wav` (24-bit master), `<id>.f32` (reference PCM for codec checks) and
`<id>.json` (measurements). Rendering is deterministic: every track has its own seeded random stream.

`integrate.py` only touches files that already exist in `assets/audio` with the same name. For
loops it encodes 2048 frames of the loop's end before sample 0 and a 4096-frame wrap after the
loop, then moves the MP4 edit-list start past the leading context. AAC codes the first frames of a
stream poorly (measured error up to 0.03 near the start, which put a tick at the seam); with the
context the decoded loop starts on fully-coded audio, and encoder padding never lands inside the
loop. Decoders that honour edit lists (ffmpeg, AVFoundation) start exactly at loop sample 0; one
that ignored them would still get a seamless loop because the encoded span is a contiguous piece
of the repeating loop. The decoded file is longer than the loop and is trimmed to
`loop_end_sample_exclusive` by the runtime preparation (as before). Every packaged file is decoded
back and checked for frame count and sample alignment (offset 0 against the master). It then builds
`.build/runtime-playsuite` by copying `.build/runtime-vorbis-atomprobe`, refreshing any non-audio
file that differs from `assets/`, and re-encoding only the audio whose source changed with
`tools/prepare_portable_assets.prepare_audio` (Vorbis q6, trimmed). Pillow is not needed because the
lake pixels are reused after checking that `nature-lake.png` is unchanged. Finally it runs
`tools/verify_portable_assets.verify`.

Note: `tools/package_audio.py` rebuilds `assets/audio` from the preserved `incoming/` deliveries and
would restore the previous recordings; run `integrate.py` again afterwards if it is used.

## Loops and mastering

* Each loop is an exact whole number of bars; the tempo is chosen so that a beat is an integer number
  of samples. The arrangement is rendered once with an 8-10 s tail and the tail (note releases, delay
  and reverb) is added back onto the start, so playback of sample N-1 followed by sample 0 equals
  continuous playback. `tail_residual` in the report confirms the tail had decayed to silence.
  Noise beds use an equal-power crossfade across the seam. Bus compression and limiting run
  circularly (state warmed up from the end of the loop).
* Loudness is normalised to -18 LUFS integrated (BS.1770-4 K-weighting and gating) with a lookahead
  limiter and 4x oversampled true-peak measurement kept below -1 dBTP. Stingers use the same target.
* `lint` checks every sustained melodic note against the chord plan and reports semitone clashes
  that are not available tensions. The only remaining report is an intentional 4-3 appoggiatura in
  the menu fanfare.

## Provenance and validation (2026-10-01 render)

Composed and rendered on Shadow with this source; no samples, recordings or third-party scores are
used. Masters: `.build/music-v2/out` (ignored). Packaged: 11 `music_*_loop.m4a` and 14 `stinger_*`
files in `assets/audio`, with entries in `audio_manifest.json` marked `"batch": "playsuite-v2"` and
their hashes in `PACKAGED_SHA256.json`. The remaining tracks (atom probe, four pegs, switchbox,
sticks & stones, puzzle main/tiptoe, menu win stinger) and all effects are unchanged.

| Track | Key / tempo | Bars (s) | Seam jump pct. master / runtime Vorbis |
| --- | --- | --- | --- |
| menu | Bb (to C), 136 | 46 (81.2) | 99.3 / 98.4 (section downbeat; within the track's own downbeats) |
| klondike | F, 90 swing | 36 (96.0) | 88.6 / 86.2 |
| spider | D minor, 100 | 40 (96.0) | 7.8 / 10.0 |
| freecell | C, 84 | 32 (91.4) | 36.9 / 77.8 |
| hearts | G minor, 110 swing | 36 (78.5) | 41.9 / 61.6 |
| sudoku_day | D, 76 | 32 (101.1) | 62.8 / 57.2 |
| sudoku_night | C, 68 | 32 (112.9) | 64.9 / 88.2 |
| gems | A, 124 | 48 (92.9) | 10.3 / 33.3 |
| nature_cube | Eb lydian, 72 | 32 (106.7) | 67.9 / 32.1 |
| untangle | F, 104 | 44 (101.5) | 49.0 / 59.4 |
| puzzle_solve | E dorian, 96 | 44 (110.0) | 46.7 / 64.6 |

Loop masters: -18.0 LUFS integrated, true peak -1.9 to -6.1 dBTP, zero clipped samples, DC < 4e-6,
circular tail residual < 2e-5. Stingers: 3.1-4.9 s (capped at 4.9 s with a 0.6 s fade), -18 LUFS,
true peak -1.9 to -9.1 dBTP, start and end at zero. Decoded AAC: alignment offset 0 for all 25 files, waveform SNR
22-44 dB. `tools/verify_portable_assets.py` passes on `.build/runtime-playsuite` (298 audio files).
These are numeric checks; the music has not been reviewed by listening.
