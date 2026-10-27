# Specimen Cabinet

## The game

A walnut cabinet of N × N sockets, a felt workbench and a tumbler that keeps dropping
polished stones onto it: every stone is one of N shapes and one of N materials, each
pairing once. Lay every stone in the cabinet so that no row and no column repeats a
shape or a material.

## Rule 2

A classic puzzle made physical: the Graeco-Latin square (Euler's officers). The rule is
unchanged; what is new is that the symbols are stones, two at once, read by eye: a shape
and a material, chosen apart, so a stone must be looked at twice. There is no 6 × 6 game,
because no 6 × 6 square exists (Tarry, 1900; the help tells the story).

## A session

The cabinet opens with a few stones pinned in it as clues and the tumbler turning. Stones
drop in a small pile at the head of the felt; the player takes them, sorts them on the
felt as they like, and sets them in sockets. A 4 × 4 takes a few minutes, 5 × 5 a quarter
of an hour, 7 × 7 an evening. Returning finds every stone where it was left, the pile
and the tumbler as they were.

## Replayability

Every cabinet is a new square, new shapes (from the six stone generators, infinitely
varied) and new materials (from twenty-four). Three sizes: 4 × 4 (16 stones), 5 × 5 (25)
and 7 × 7 (49).

## Passive mode

None: a puzzle with no clock waits for the player.

## Rules

- The cabinet is N × N. There are N shapes and N materials; the N² stones are every
  shape in every material, once each.
- A full cabinet is solved when every row and every column holds each shape once and
  each material once.
- Clue stones are pinned and cannot be moved. Any other stone may be moved between the
  cabinet, the felt and back, as often as liked; a stone set on an occupied socket trades
  places with the one there.
- The tumbler holds the stones not yet out; while the pile holds fewer than four, it
  drops the next.
- Each puzzle is generated from a random Graeco-Latin square (cyclic over a prime, or
  over the field of four, with rows, columns and both symbol sets permuted), and its
  clues are removed one by one only while the solution stays unique: an exact search
  counts solutions to two after every removal, so every puzzle has exactly one answer.
- A move is a stone set in a socket; sorting on the felt is free. The best game for each
  size is the fewest moves, then the shortest time; the time is only kept, never pressed.

## Look, sound and words

Seen from above under the card table's window light: a cabinet of figured walnut with
rounded sockets; the card table's green baize; a black rubber tumbler drum, tipped with
its mouth raised over the felt, its far end beyond the window's edge, turning while it
drops a stone. Stones from the stones engine (`shared/stones`). Soft clicks of stone on
wood and on felt, the tumbler's rumble, and calm, unhurried music.

## Controls

- Pointer: drag a stone from the pile, the felt or the cabinet; drop it on a socket or on
  the felt. A click lifts a stone and a second click sets it down.
- Keyboard: arrow keys move among the sockets; Enter takes or sets the stone there;
  Escape puts a lifted stone back; N for a new cabinet; F1 for help.
- Command bar: New cabinet (choose the size), Help.

## Not included

- Hints that place a stone: the puzzle is the deduction; clashes are shown instead.
- A 6 × 6 cabinet: none can be solved.
- Timers and scores for speed: the record is moves, then time, for the curious.
