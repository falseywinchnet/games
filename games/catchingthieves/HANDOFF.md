# Catching Thieves integration

## VERIFIED

- Built the complete twenty-game native application on the M4 Mini with the pinned GUI.Forms source, LLVM 22.1.8 and its Skia CPU renderer.
- `catchingthieves_rules`: all 251 table gardens (11 lessons, 72 Easy, 78 Medium, 90 Hard) replay their witness to a win in exactly their par; 155 pars and solver efforts are re-solved; every generated garden measures into its own tier and the tier medians rise at every step for pumpkins, soil, pushes, moves, switches, solver states, trap pushes and score; fresh gardens grow at each tier, measure into it and replay; each lesson shows its mechanism (the obvious push wedges, the near pumpkin first is unsolvable, two in a row won't budge); 500 lines are unique, short, ASCII and free of unkind words; the save's new fields round-trip and nonsense is refused; an old book save migrates (garden in play move for move, tier, season, records, cleared and perfect counts, old endless gardens).
- `catchingthieves_core_contract`: a simulated 18-minute session says about 110 lines with none repeated; repeated wedges escalate from laughing to cheering to kind pointers at Undo, the last without the laugh sound.
- `catchingthieves_view_contract`: a first game starts with a lesson; the difficulty is a choice on the shell's Settings screen, not a command; choosing it re-deals an untouched garden in about 1 ms at every tier and waits for the next garden otherwise; a fresh Medium garden grows, saves, resumes exactly and its witness wins it; an old book save halfway through garden 150 reopens move for move at Hard and is upgraded in place; instant win/new-garden autosave, no duplicate result, hidden timer/presentation silence and refusal of production script input pass.
- Real AppKit standalone and hosted native-window scripts completed without frame or native callback faults. The hosted run switched games and opened shared help.
- Inspected native LiveSurface captures at 1180 × 800, 600 × 370 and 600 × 320; the contract also checks 150% scale. Inspected garden 41 and the garden-book controls at 600 × 320. PNGs are in `screens/`, including the hosted capsule and game at 600 × 420.
- Portable text/audio adapters compile against the pinned headers. Source audio and exact producer loop metadata pass the dynamic inventory verifier; the native decoder and Four Pegs transport checks pass with libvorbis quality-6 runtime assets.
- The authoring gate checks newly edited files against house style. `borrowed` records only source files that remained byte-for-byte identical to the original package; original fingerprints are in `SOURCE_PROVENANCE.json`.

Reproduce standalone native input with `games --game catchingthieves --standalone --dev --script games/catchingthieves/tests/native.script`. The full native host recipe is in the root `AGENTS.md`.

## NOT VERIFIED

Human listening, subjective play feel and long sessions have not been certified by these automated runs. Local development uses macOS arm64. Windows x64 and both Linux architectures, and the release macOS compiler/installer, are validated by the publication workflow. A passing local build does not itself establish those results.

## DECISIONS

The sequential book of 243 gardens became four difficulties (README "The gardens"). The book's gardens are kept, measured and filed by tier in `assets/levels/gardens.txt` under their old indices as permanent ids, so old saves resume; they are the instant fallback while a fresh garden for the chosen tier grows on a worker. Seasons are random per garden (never twice running; lessons in spring) because difficulty is named on the card and the calendar would show half the world the wrong season and hide four of the five musical arrangements for months. Catching Thieves is not a long real-time game, so it has no passive mode. The `ct_stinger_book` cue now marks having seen every lesson.

Preserved the complete user-supplied `thieves` game: original campaign, garden renderer, mesh artwork, critters, seasonal scene, hints, endless generation and sounds. Platform-specific presentation, text and audio were replaced with portable GUI.Forms services. The game owns its module entry, cover, help and resources at permanent ID 18. Hosted commands use the shared capsule/help and master switches. Independent play uses the same view through the standalone host.

Hint and endless workers are cancellation-aware and joined when hidden. Reduced motion settles presentation immediately while retaining puzzle rules and committed moves. The garden book pages at the minimum window size. Save files use the suite state directory and an isolated development file.

The cue checker reports prefixes such as `ct_step_0`, `ct_push_0`, `ct_muffle_0` and `ct_music_` because names are completed at runtime. The corresponding numbered/seasonal files are present. Original generic unused UI cues were prefixed `ct_ui_` to prevent collisions with the suite's other sounds. No campaign, artwork or gameplay feature was removed.
