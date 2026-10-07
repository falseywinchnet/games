# Catching Thieves integration

## VERIFIED

- Built the complete twenty-game native application on the M4 Mini with the pinned GUI.Forms source, LLVM 22.1.8 and its Skia CPU renderer.
- `catchingthieves_rules`: all 243 campaign solutions replay through the real rules; generation and forward-solver checks pass. The independent folder's Release build and CTest run also pass.
- `catchingthieves_core_contract` and `catchingthieves_view_contract`: reduced-motion complete solution, instant win/new-level autosave, exact reopened position, no duplicate result, all campaign resources, hosted commands, hidden timer/presentation silence, and refusal of production script input pass.
- Real AppKit standalone and hosted native-window scripts completed without frame or native callback faults. The hosted run switched games and opened shared help.
- Inspected native LiveSurface captures at 1180 × 800, 600 × 370 and 600 × 320; the contract also checks 150% scale. Inspected garden 41 and the garden-book controls at 600 × 320. PNGs are in `screens/`, including the hosted capsule and game at 600 × 420.
- Portable text/audio adapters compile against the pinned headers. Source audio and exact producer loop metadata pass the dynamic inventory verifier; the native decoder and Four Pegs transport checks pass with libvorbis quality-6 runtime assets.
- The authoring gate checks newly edited files against house style. `borrowed` records only source files that remained byte-for-byte identical to the original package; original fingerprints are in `SOURCE_PROVENANCE.json`.

Reproduce standalone native input with `games --game catchingthieves --standalone --dev --script games/catchingthieves/tests/native.script`. The full native host recipe is in the root `AGENTS.md`.

## NOT VERIFIED

Human listening, subjective play feel and long sessions have not been certified by these automated runs. Local development uses macOS arm64. Windows x64 and both Linux architectures, and the release macOS compiler/installer, are validated by the publication workflow. A passing local build does not itself establish those results.

## DECISIONS

Preserved the complete user-supplied `thieves` game: original campaign, garden renderer, mesh artwork, critters, seasonal scene, hints, endless generation and sounds. Platform-specific presentation, text and audio were replaced with portable GUI.Forms services. The game owns its module entry, cover, help and resources at permanent ID 18. Hosted commands use the shared capsule/help and master switches. Independent play uses the same view through the standalone host.

Hint and endless workers are cancellation-aware and joined when hidden. Reduced motion settles presentation immediately while retaining puzzle rules and committed moves. The garden book pages at the minimum window size. Save files use the suite state directory and an isolated development file.

The cue checker reports prefixes such as `ct_step_0`, `ct_push_0`, `ct_muffle_0` and `ct_music_` because names are completed at runtime. The corresponding numbered/seasonal files are present. Original generic unused UI cues were prefixed `ct_ui_` to prevent collisions with the suite's other sounds. No campaign, artwork or gameplay feature was removed.
