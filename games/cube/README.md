# Nature Cube

**The game.** Three faces of a glass cube carry coloured pairs of squares; join each pair
with a line that folds over the cube's edges, around walls of mossy stone and through
linked portals. Lines may not cross or share a square.

**Rule 2.** A classic: our version of AlexMatveev's Flash games *3D Logic* and *3D Logic 2:
Stronghold of Sage*, from which nothing is taken but the idea. Intentional changes: boards
are generated, not a fixed list; difficulty is measured by a solver; and portals are new.
There is no fill-every-square rule (an earlier Hard had one; it was dropped).

**A session.** A board takes from half a minute (Easy) to several minutes (Hard). When it is
solved, the lines light, the cube spins away and the next one arrives. Returning mid-board
finds every line exactly as it was left.

**Replayability.** Every board is generated. Easy: four squares to a face, five pairs, a
stone or two. Medium: five to a face, six pairs, two or three stone walls, a portal pair on
about half the boards (the first one placed right beside the endpoint that needs it). Hard:
six to a face, eight pairs, longer walls and blocked corners, usually one or two portal
pairs, and exactly one way to join every pair without a detour. The solver checks the
levels are ordered: fewer solutions, more wrong turns, longer and more folded lines, fewer
first moves handed over (see HANDOFF.md).

**Passive mode.** None. The solver can solve every board; offering it as a demonstration
was declined for now (see Not included).

**Rules.** A line starts on a coloured square and steps to a neighbouring square, including
across a fold. It may not enter a stone, another pair's square or a square another line
holds. A portal pair is two linked cells. A line that steps into one portal comes out of
its partner and continues from there in any direction; a portal is never an ordinary
square and a line cannot end inside one. The first line through a pair claims it (it takes
that colour) until the line is cut back past it or erased. The board is won when every pair
is joined; squares may stay empty. Every board is cut from lines laid over the open cells
(the witness), which is replayed through the real rules before the board is offered.

**Look, sound and words.** The house's polished 3D: a software-rendered block of dark glass
with mirrored tiles reflecting a painted mountain lake, mossy stone blocks that join into
walls, stone-ringed portal wells with a swirl and studs that match partners. Music and
effects are synthesized live (glass_music, glass_effects). No characters.

**Controls.** Press a coloured square and trace; trace back to shorten; press a cell of a
line to cut it back there and carry on. Move the pointer to tilt the cube. N new board; H or F1 help;
Escape closes the top scores. Capsule: New game, Top scores, Help; Settings: Level.

**Not included.** Keyboard tracing (directions on a turned cube are ambiguous); undo (a line
is undone by tracing back); a passive solver demonstration; per-level score tables (the
existing table is kept as it was).
