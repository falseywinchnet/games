# Atom Probe

A classic Black Box puzzle. Four atoms hide inside a sealed 8×8 chamber of
smoked glass and drifting fog, ringed by 32 beam emitters. Fire a beam and the
fog flares while it is inside. Then it streaks out of another emitter, snaps
back to its own, or the chamber thumps as an atom swallows it. The answer
lights the emitter's cap: **H** for absorbed, **R** for reflected, or a
coloured number shared by both ends of a detour. Mark the four squares you
suspect on the glass and pull the lever. The vents hiss, the lid lifts, the
atoms hang in the clear air with their electrons orbiting, and every beam you
fired replays along its true path, bending around them. Then each marker
either locks onto its atom or doesn't.

Rules are classic Black Box:

- A beam that runs straight into an atom is absorbed.
- An atom diagonally ahead of a beam turns it 90° away; two at once send it straight back.
- An atom beside the square a beam would enter turns it back at the door.

Points: 1 for each H or R, 2 for each detour. Find all four atoms in the
fewest points to sign the lab log. A box with atoms missed shows the classic
penalty (+5 each) but is not ranked. Every box is generated so that the full
set of 32 probes identifies it exactly: no luck, ever.

Native C++20 on GUI.Forms, using the techniques Eggy, Switchbox and Four Pegs
proved out: a software 3D renderer with perspective, toon light and ink
outlines, pixel-art presentation, plus camera-facing glow sprites and a soft
bloom pass added for this game.

## Play

```sh
open build/AtomProbe.app
```

Click an emitter to fire. Hover one with a detour to see its partner pulse.
Click a square on the glass to place a marker. Right-click (or Control-click)
crosses a square out. With four markers placed, click the lever. Keys: Enter
opens the box (or starts a new one), N new box, T top scores, M music, S sound,
F1 help.

Saves go to `~/Library/Application Support/Rainstar/Games/atom_probe-v2.txt`
(`GAMES_STATE_DIR` overrides the folder). The earlier 4×4 game's
`atom_probe-v1.txt` is never touched. With `--dev` (separate save),
`AP_SCRIPT="1:f3,2.5:m9,4:a,4.5:o"` replays inputs:

- `f<port>` fires a port
- `m<cell>` marks a cell; `x<cell>` crosses one out
- `a` marks the answer; `w` marks three right and one wrong
- `o` pulls the lever
- `n` starts a new box
- `help` and `scores` open those dialogs

`AP_PRINT_WID=1` prints the window id.

## Build

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=$PWD/../eggy/sdk && cmake --build build -j 8
ctest --test-dir build      # rules: an independent tracer on all 635,376 boxes x 32 ports, fairness, scoring, saves
./build/preview play|reveal out.ppm [W H]   # headless stills of the chamber
python3 audio_src/lab_music.py && python3 audio_src/make_sfx.py   # score, stingers, effects
```

## How it works

| Part | Files |
|---|---|
| Rules: the tracer, fair-box generation, probes, markers, scoring | `box.*` |
| The chamber: lab, emitters, fog, glass, atoms, console and lever, beams, bloom, picking | `chamber.*` |
| Window, input, the probe and reveal sequences, words on the scene, dialogs, autosave | `atomprobe_view.*`, `save.*` |
| Shared platform (copied from Four Pegs, namespace `ap`) | `platform/` |

Fairness: every arrangement's 32 reports are hashed once at startup, across
threads, in about 0.1 s. A box is dealt only if no other arrangement shares
its reports, which holds for 99.4% of arrangements. A probe's animation takes
the same time whatever the beam's path, so its timing gives nothing away.

All audio is synthesized (`audio_src/`):

- "Black Box", a 32-bar loop at 100 BPM in E minor, ships as AAC; its decoded length matches the manifest exactly.
- Stingers: all found, atoms missed, and top score.
- Effects for the emitters, the fog, each answer, the markers, the lever, the vents, the atoms and the reckoning.

## Not yet verified

- **Listening:** nobody has listened to the score or the effects yet. The checks were numerical only (levels, no clipping, a clean loop seam).
- **By hand:** the user has played a box through to the reveal (2026-10-01). Scripted sessions covered the rest.
- **Reduced motion:** not implemented.
- **Cabinet integration and other platforms:** not done.
