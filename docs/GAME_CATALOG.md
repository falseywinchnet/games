# PlaySuite catalog

PlaySuite contains seventeen playable games. Each has its own box on the shared shelf; the command capsule provides navigation and each game's supporting controls. Card games use green felt and cream cards; the hanafuda deck has original woodblock-style faces. Switchbox's replacement is integrated; Sticks & Stones is retired.

The four newest games are described in [the integration guide](NEW_GAME_INTEGRATION.md): Koi-Koi (month matching and set collection), The Parrot's Table (truth/lie deduction), Liar's Dice (bidding and bluffing), and Pen the Sheep (generated fence-placement puzzles). They retain their delivered match scores, records, ledger and meadow progress respectively.

## Current card-game scope

| Game | Rules direction |
|---|---|
| Solitaire | Windows 7-style Klondike, draw one or three, alternating-color tableau, suit foundations; verified winnable Easy/Medium/Hard deals |
| Spider Solitaire | One, two, or four suits; same-suit movable runs; ten columns; eight completed runs; verified winnable Easy/Medium/Hard deals |
| FreeCell | Four free cells, eight columns, space-limited run moves, four foundations; verified winnable Easy/Medium/Hard deals |
| Hearts | Four local players with three computer opponents named for US presidents (card memory and sampled guesses at hidden hands; per hand one sharp, one steady, one forgetful), rotating passes, shooting the moon, 100-point match |

Windows 7 establishes behavior and familiarity. Microsoft graphics, sounds, executable code, and deal-number compatibility are not bundled or claimed. Kyodai Mahjongg is the quality and polish reference.

## Collection-wide behavior

Every game must autosave its current position after committed changes and resume it when selected or when the application opens. New game is a separate explicit action and immediately saves the replacement. Switching games preserves each game. Presentation animations follow committed state, so quitting during animation must resume a coherent board.

Card and logic games have no running score HUD or lifetime statistics system. Eggy retains the achievement-only HUD specified in its own handoff, with its separate elapsed-time/stars ranking. A terminal arcade-style Top Scores window accepts a player's name for a qualifying result. Retain only the top ten per game/rules profile, with a persisted one-time submission latch. Closing/reopening or undoing/replaying a finish must not duplicate the result. No win rates, streaks, average times, or play histories. Sudoku mistakes belong to the current game and survive undo.

## Additional playable roster

| Game | Required behavior | Current implementation |
|---|---|---|
| Gems | Glistening gems, hover rotation, click-to-select or drag swaps (the dragged gem follows the pointer). Invalid moves visibly finish the swap and then return. Matching four creates a colored bomb; the user clarified that it later activates through a same-color match, like stars. Include stars and hypercubes, cascades, and additional colors as difficulty rises. | Playable with autosave, named top scores, and delivered audio. |
| Sudoku | Use the handed-off offline generator. Notes and active play; light/dark complementary blue themes; hover a filled square to highlight matching digits and its row/column; row/column/3×3-box completion animation. Right click to note, left click to set. Mistakes remain counted after undo, while corrected/undone cells lose their error highlight. | Native board implemented using the canonical generator, autosave, named top scores, and day/night music. |
| Nature cube (working title) | A reflective 3D cube with exactly three playable faces, tranquil nature background, and a pronounced tilt driven by mouse movement without dragging. Trace paths between distinct colored source/target pairs across the cube. Connect every pair; the user explicitly allows unused squares. Generator must provide and replay a valid solution. | Playable: exactly three visible and playable 4×4 faces; Easy five pairs, Medium seven pairs and three mossy stones, Hard nine pairs, five stones and every open tile filled. Boards are cut from a path through every tile, so the stored solution fills the cube and is replayed through actual tracing rules. Reflective mesh and nature artwork. |
| Untangle | Reposition graph points until no edges cross. Generated graphs require a known planar embedding as a solution witness. | Playable: yarn on pegs, 10/13/16 pegs at Easy/Medium/Hard with one, two or three yarn colors (a thread may cross other colors, never its own), two or three frozen pegs that move only once their threads are clear, and a cat that sits on pegs, chases the dragged one and on Medium and Hard swats neglected pegs. Every layer is planar in the stored solution; crossing checks reject overlapping pegs or pegs lying on threads. |
| Atom Probe | Hidden-atom spatial deduction with boundary probes, following the recovered interview brief. Choose and document precise absorption/deflection/reflection rules; verify the tracer and generated-board deducibility. | Playable: 4×4 field with three atoms. All 560 possible fields are considered for deducibility; generation accepts only a unique complete probe signature. |
| Four Pegs | Four-position code deduction with aggregate exact/wrong-position feedback and color plus symbol cues. Verify duplicate counting against an independent evaluator. | Playable: an original illustrated Curator opponent, a large perspective console, horizontal six-symbol palette, and a compact attempt record with 2×2 dark/light feedback. Repeated symbols are allowed; ten guesses. All 1,679,616 secret/guess pairs checked against an independent evaluator. |
| Puzzle Solve | PicPax-inspired reconstruction: a small blue/yellow target image; rotate, flip, and place geometric pieces into a larger frame to reproduce it. Keep this separate from the Sokoban game and its audio. | Playable: a square frame reconstructed from seven diagonal pieces (five triangles, a rotated square, and a parallelogram), with attached blue/yellow artwork. Exact quarter-square triangles drive rotation, reflection, collision, target matching, fit preview, and construction-witness replay. |

| Eggy and the Very, Very Tall Mountain | Host the delivered `eggy::EggyView`; preserve its own world, story, audio, saves, scores, and portable live-surface presentation. | Integrated as Eggy in the collection. Arrows/WASD, Space, and mouse guidance; slower autopilot and offline progress; original title/base camp, weather, biomes, and summit ceremony. Shared audio/motion masters and immediate hide/silence on tab changes. |

Rendezvous Riders is explicitly excluded by the current user instruction. Checkers and Crossword below are historical possibilities, not additions to the newly selected deployment roster.

## Recovered historical modules

The recovered File Manager games planning index lists nine modules. Solitaire and FreeCell overlap the current scope. Its other seven intended modules are:

- Checkers: local play and a non-neural opponent.
- Sudoku: generated puzzles with solving and explanatory hints.
- Crossword: a versioned Shakespeare corpus first; later user-selected subsets.
- Atom Probe: hidden-atom spatial deduction using boundary probes.
- Four Pegs: four-position code deduction, with symbols as well as colors.
- Switchbox: a character resets switches while the player discovers a bounded hidden sequence.
- Rendezvous Riders: historical proposal, explicitly excluded from the current collection.

Source: `/Users/joshuahkuttenkuler/Developer/Projects/gui-forms-investigation/source/file_manager/games/planning/README.md`, `CHARTER.md`, and `games/*.md`. The historical documents contain proposed rules and unresolved decisions; they do not establish those details as accepted final behavior.

## Included in the deployed-package plan: Catching Thieves reference

The user requested this addition through the coordinating chat `01a0f5c2-fdd4-7822-b80c-743377e48f50`. This is an inclusion and planning request. It does not interrupt the four card games or require immediate implementation of the puzzle.

### User requirements

- Include an early isometric, Sokoban-like block-pushing puzzle with pre-rendered pseudo-3D graphics, entertaining art, and many levels.
- Use the recovered 도적잡기 (roughly “Catching Thieves”) as the reference and working label. The public title remains undecided.
- Support reuse of appropriately licensed open-source level packs, preserving their licenses, attribution, and provenance.
- Generated levels must have proven solvability. Store a solution and replay it through the actual movement rules, including any altered box, hole, or target behavior. Plausible-looking boards are insufficient.
- Reuse the collection's presentation, input, animation, sound, help, and puzzle-state infrastructure where appropriate.
- Exclude the original game's racist Japanese/American caricatures.
- Whimsical fantastical creatures, a small bear protagonist or similar, and comic character interaction are creative guidance. Astra retains responsibility for the actual cast, rendering, palette, and final execution. Develop original characters.
- The existing Neo/Opus coordinator owns original puzzle music and effects, as well as the card-game audio. Extracted source audio is reference material, not cleared shipping content.

### Observations reported by the reference investigation

These are the other chat's observed reference details, not independent measurements by this project and not additional user requirements:

- Fixed raised-looking courtyard with dimensional walls, shrubs, faceted glossy red pushable blocks, upright character sprites, and dark ground holes.
- Cardinal grid movement changes the player's directional pose. Pushing advances a block by one square while the player takes its former square.
- Target figures periodically appear and disappear into holes, creating comic staging on an otherwise largely static board.
- Separate walk, push, and locked sound events; explicit directional character states; distinct win and fail clips.
- Reported SWF frame rate: 30 fps. Two outcome clips: 110 frames each, approximately 3.7 seconds.
- It is a fixed pseudo-3D board, not free-roaming 3D. No unmeasured animation curve is prescribed.

### Reference provenance and next implementation gate

The reference investigator ran and interacted with the SWF on the Neo. These files are on the Neo, not the M4:

- Screenshot: `/Users/ultimussecundai/Documents/Codex/2026-09-30/find-x20/outputs/catching-thieves-game.png`
- Audio record: `/Users/ultimussecundai/Documents/Codex/2026-09-30/find-x20/outputs/catching-thieves-all-audio.zip`
- Original SWF: `/Users/ultimussecundai/Library/Mobile Documents/com~apple~CloudDocs/nk 2/1.swf`
- Reference music: `catching-thieves-music-179.mp3` (reported 36.45 seconds) and `catching-thieves-music-165.mp3` (reported 14.68 seconds), in the outputs directory.

Before implementing: fix the movement and victory rules, choose the new original cast, audit each proposed level pack's actual license, and establish a solver/replay oracle. Keep imported and generated levels traceable to their sources and rules version. The package roadmap includes this module; no puzzle code or shipped assets are claimed yet.

## Sudoku engine provenance

The user-authorized handoff came from “Build an offline Sudoku generator.” The replaceable M4 mirror was `/Users/joshuahkuttenkuler/Developer/CodexBuilds/sudoku-offline-f74d1316f7bd`. Its canonical engine, calibration, independent oracle/tests, and original reports are preserved under `vendor/sudoku/`. The engine SHA-256 is `e6c1324a58ab7ebe3521ede26cda78a179843ed86af47a7eb2a268438f0a642d`.

The app uses pinned QuickJS in a background job; no server or external Node installation is required. The runtime adapter removes only ES-module export syntax and supplies a clock. Three native integration fixtures match the original Node engine exactly and pass the independent Algorithm X oracle and logical trace replay. Original warmed-Node benchmark timings are not native cold-start timings.

## Implemented rule details

Gems begins with five colors and adds a color each 1,800 internal points, up to eight. Three in a line clears; four creates a colored area bomb; five in a line creates a hypercube; a T/L intersection creates a row/column star. Colored powers trigger in a matching-color match and can chain. A hypercube swaps with a color to clear it; two hypercubes clear the board. Invalid swaps animate fully outward and back. Clears and falling refills animate in sequence. No available move ends the game and opens Top Scores.

Atom Probe gives an immediate forward atom priority (absorption). A single forward diagonal atom turns the ray away; two forward diagonals reflect it. An entry-edge deflection, loop, or return through the same port is a reflection. Other exits identify the paired boundary port. These rules are in Help and are exhaustively compared with a separately written tracer.

Nature Cube allows unused cells. Its generated witness establishes existence of a valid connection set, not uniqueness. Untangle and Puzzle Solve also use construction witnesses. Card deals use this application's seeded shuffle, without Microsoft deal numbering. Solitaire, Spider and FreeCell deal from tables of seeds whose winning lines were found by `src/solitaire_solver.cpp` and replayed through the game rules, graded Easy, Medium and Hard (`tools/deal_grader.cpp` regenerates `src/deal_tables.cpp`).
