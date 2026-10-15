# Tumbled Stones

## The game

A tray of polished stones, each one of a few solid shapes cut from any mineral. Turn
stones over and round until neighbours of one shape lie exactly the same way, and make
the one great pull of the size you chose at the start, in as few moves as you can.

## Rule 2

New. It keeps match-three's tray, gravity and refill, and replaces colour with shape:
every stone is one of up to five solid families and one of eight orientations (two
faces, four quarter turns), and only shape and orientation match; the mineral never
does. Its decisions stay interesting because nothing matches by itself: the player
sets each match off by setting a stone down, so a group can be built quietly and pulled
at the right moment, and a swap can be prepared by turning a stone so it does not match
on arrival. The chosen target replaces timers and lives as the pressure: the player
decides how ambitious to be.

## A session

The first minute: choose a pull size (7 is offered), see the stones pour into the tray,
click a stone and watch it turn over in the hand. Moving away sets it down and pulls
any pair it now makes. A pull of 7 takes a few minutes; 20 is a long project. Returning
mid-game finds the tray exactly as left; a stone that was in hand is back in its socket
as it was.

## Replayability

Every game is a fresh tray from a new seed. Families are generated (six styles: broken
shell, petrified wood, a chunk of pulled candy, faceted tumble, agate nodule, knapped
flake, each with its own measures; the grown ones are solids broken as real pieces are), and each family's run is limited, so
new families keep arriving; later ones are more and more often near relations of
families in the tray, so the shapes grow harder to tell apart. Difficulty is the size
chosen, 2 to 20. Pulls under 7 keep no best.

## Passive mode

None needed: the game is turn-based and waits.

## Rules

- Tray of 10 x 10 sockets, always full. Each stone has a family (its shape), an
  orientation (face up or down, 0 to 3 quarter turns clockwise), a mineral (looks only)
  and a swap mark.
- At most five families are in the tray; one more may wait to come in. Each family has a
  run of 20 to 80 stones; new stones are drawn from families with stones left, weighed
  by how many they have left. When a family runs out, another is brought in; it comes
  into the tray when a slot frees.
- Setting a stone down (after any flips and turns) finds its group: the stones joined
  to it orthogonally that share its family and its orientation exactly. Two or more are
  pulled. Stones above fall keeping their orientations; new stones fill from the top.
  Nothing else is pulled until a stone is set down.
- A swap exchanges two orthogonal neighbours, neither marked; both are then marked.
  The moved stone's group is checked first, then the other's.
- A juggle (on request, or when no match can be made by a set-down or a swap) reshuffles
  the stones, gives them random orientations, clears the marks, and turns the stones of
  any family that has run out into the incoming family. Afterwards a match can always
  be made.
- Moves: a set-down that changed the orientation or pulled, a swap, a requested juggle.
  Points: each pull of n stones costs round(400 / n^2): 100 for two, 44 for three, 1
  for twenty. The game is won when a single pull reaches the chosen size; the best for
  that size is the lowest moves plus points.
- Generation is seeded (splitmix64) and integer-only; the tests replay hundreds of
  moves and check the tray stays sound and playable. Every family's eight orientations
  are measured apart from above, and a family too near symmetric is drawn again.

## Look, sound and words

A dark varnished walnut tray with narrow-walled rounded sockets, in the card tables'
sunlit room, path traced once from the tray (window and ceiling lights, no direct sun):
the polished stones mirror the window and lights. Stones are rendered in 3D by the
render engine, with fine relief (cracks, fibres, pits, agate bands), minerals that run
through the stone, translucent ones that show the wood and cast coloured shade.
Palette: walnut browns, cream text, brass gold for what matters. Sound: soft clicks of
stone on wood (modal synthesis), a clattering juggle, a stone-chime win. Music:
"Polishing Day", D major at 104 BPM, kalimba and lithophone over marimba and log drum.
No characters.

## Controls

Pointer: click a stone to pick it up and turn it over; right-click turns it round; move
away to set it down; drag onto a neighbour to swap. Keys: arrows, F, R, Space/Enter,
Shift+arrow, Escape (put back), J (juggle), N (new game). Capsule: New game, Juggle.

## Not included

- Undo: the score is a count of moves, and a best must be earned.
- Hints: recognising shapes across minerals is the game.
- Bombs, special stones, timers, lives: declined by the owner.
- An endless mode: every game has a target; the owner chose that.
