# The live mower voice

`src/mower_voice.hpp` is the ride-on mower synthesized sample by sample, accepted by
ear on 2026-10-05 as the mower's sound. It replaces the idea behind the four looped
beds: instead of pitch-shifting recordings of a fixed engine, it runs the governor
itself, so blade engagement, heavy grass, lulls and recovery are all heard as one
machine. `live_reference/` holds the NumPy model it was ported from
(`python3 mower_riding.py out.wav`), kept as the listening reference.

## State

- Built and tested on its own; it is **not** in `build.cmake`, `GAME.json` or
  `mowing_view.cpp`. Nothing the game does today has changed.
- To build it into the core, add `src/mower_voice.cpp` to `mm_core` (and to
  `core_sources`) and `tests/mower_voice_tests.cpp` as a second rules test.
- `tools/mower_voice_render.cpp` writes a 32 second working day to a WAV.

      c++ -std=c++20 -O2 -Isrc src/mower_voice.cpp tests/mower_voice_tests.cpp -o voice_tests && ./voice_tests
      c++ -std=c++20 -O2 -Isrc src/mower_voice.cpp tools/mower_voice_render.cpp -o voice_render && ./voice_render day.wav

## What stands between it and the speakers

GUI.Forms audio plays immutable clips only (`audio.hpp`: "No streaming"), so there
is nowhere to hand a running generator to the device. That is a toolkit request,
not something to work round in the shared checkout: a voice that pulls stereo
frames from a callback or a ring buffer. Until then the choices are to keep the
looped beds, or to render loops from this voice at start-up with
`AudioClip::copy` and play those as beds (which loses the live governor).

## Driving it

Give it `MowerControls` every frame: `ignition`, `governed_rpm` (1500 idle, 3600
mowing), `blades`, and `load` (`EngineVoice::load` as it is). It keeps its own
governor, so `EngineVoice::rpm`, `rate` and `throttle` are not inputs; read
`rpm()` back if the picture should follow the sound. It costs under 2% of one
core at 48 kHz.
