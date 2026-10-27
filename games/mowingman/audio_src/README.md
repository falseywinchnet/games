# Mowing's sound

Everything is synthesized from a physical description by `make_audio.py` (NumPy
only, deterministic). No recordings are used.

## What a mower sounds like

A ride-on mower is two machines sharing one shaft speed.

**The engine.** A four-stroke fires each cylinder once every two crank revolutions.
Each firing releases a blow-down pulse into the exhaust: a fast rise and a decay
of a few milliseconds, shaped by the muffler's chambers (a Helmholtz resonance near
100 Hz, chamber modes of a few hundred hertz) and rolled off above a few kilohertz.
Valve gear adds faint ticks; the flywheel's cooling fan adds a breathy band near a
kilohertz. Cylinder layout gives each engine its rhythm:

| Mower | Engine | Rated speed | Firing pattern |
|---|---|---|---|
| Orange H | big single | 3300 rpm | one pulse per cycle: 27.5 Hz, a thump |
| Red T | parallel twin | 3400 rpm | two even pulses per cycle: 56.7 Hz, smooth |
| Green JD | 90-degree V-twin | 3600 rpm | pulses 270 then 450 crank degrees apart: 60 Hz with a 30 Hz lope |

**The blades.** Published measurements of mower decks agree on the picture
(Domestic lawn mower blade noise and influence of machine geometry, University of
Southern Queensland; Analysis of Vibrations and Acoustics on Riding Lawn Mower,
SAE 2021-36-0075): structural vibration dominates below about 500 Hz, with deck
resonances spaced roughly 50 Hz apart; above 500 Hz the sound is broadband
aerodynamic noise from the blade tips with no dominant tones; and every pass of a
blade end past the deck's discharge throat makes a pressure pulse at the blade-pass
frequency, BPF = rpm x blades / 60. Cutting grass is at least 8 dB(A) louder than
running free, and the cutting itself is a granular shearing noise synchronized to
the blade passes, with clippings rattling inside the deck.

**Load.** Tall grass loads the blades. The engine's mechanical governor lets the
speed sag a few percent, then opens the throttle: the exhaust note drops in pitch
and turns harder and brighter, then recovers. Because the deck is belt-driven from
the crankshaft, the blades sag with it.

## The mower in the game

The game's mower is the live voice in `src/mower_voice.*`: a 90-degree V-twin on a
governor with a three-spindle deck, synthesized sample by sample (see
`LIVE_VOICE.md`). It runs about 170 times faster than real time on one core.

**Live.** Where the toolkit can play a generator (`GUI_FORMS_AUDIO_GENERATOR`, in GUI.Forms
since `63e7128`), the audio device pulls the
voice directly. The scene posts two things to it: the key (`E`) and the grass under
the deck. The engine is always asked for mowing speed (3600 rpm); the blades go in
1.7 s after the key; the starter, the climb, the governor sagging in long grass and
clearing on mown grass, and the spin-down when the key goes off are all the voice's
own. Nothing is recorded or looped.

**Clips, where there is no generator.** `tools/mower_loops.cpp` (built as
`mm_mower_loops`) renders the same voice to three masters, and `make_audio.py`
encodes them into `assets/audio/`:

| Clip | What it is |
|---|---|
| `mm_mower_run` | 20 s of mowing through a slowly swelling cut; the last second is folded into the first, so it loops without a seam |
| `mm_mower_start` | the key, straight up to speed, blades in, then mowing; its tail fades as the loop comes in |
| `mm_mower_stop` | a moment of mowing as the loop goes out, then the key off and everything spinning down |

All three liveries share this voice. The sections above on the three engines and the
5:6 deck describe the earlier clip beds, which the game no longer plays.

Rerunning `make_audio.py` re-rolls the random detail in the small effects below.

## The rest

| Sound | Made from |
|---|---|
| `mm_gnome_hoo`, `mm_gnome_hoo2` | A formant voice: a breathy "h" onset, then a glottal pulse train shaped by /u/ formants, with a cheeky rise-fall pitch contour. An imitation in the spirit of the garden-gnome meme's "hoo"; the meme's original clip is not ours to ship, so a licensed recording can replace these two files without code changes. |
| `mm_freeze` | A falling icy chirp with sparkles: the gnome stuck mid-pose. |
| `mm_shatter` | A crack and a thud, then dozens of porcelain fragments ringing on high, short modes, thinning out as they settle. |
| `mm_scream` | A tiny, distant, shrill voice: a high gliding /a-i/ with vibrato, low-passed and set back in the space. Played quietly. |
| `mm_puff` | Breath through a seed head. |
| `mm_mushroom` | A soft, wet thock. |
| `mm_bump` | A deck clunk and rattle. |

Run `python3 make_audio.py` (NumPy; AAC encoding uses `afconvert` on macOS or
`ffmpeg`). Masters are written to `masters/` (not committed); the shipped files go
to `../assets/audio/` with `mowingman_audio_manifest.json`, which records each
loop's exact length so the runtime trims it to the sample.

## The gnome's voice

`gnome_voice.mjs` performs the gnome's five clips (`mm_gnome_hoo`, `mm_gnome_hoo2`,
`mm_gnome_giggle`, `mm_gnome_giggle2`, `mm_gnome_nyah`) on the monk choir's HDR voice
(`~/visualizations/monk-choir/engine`): a physical glottis and vocal tract with a nasal
port, glottal and constriction turbulence and closure impacts. He is a tenor's tract at
70% in head voice with a narrow epilarynx. Each sound is a list of timed gestures:
breath ("h") into voice, vowel, pitch, tongue-tip closure and velum for "ny". Run
`node audio_src/gnome_voice.mjs [engine-dir] [out-dir]` (Node 20 or later); `make_audio.py`
leaves these clips alone.
