# The Paradoxiary

The parrots have moved from the tea table into a jungle grown inside a great Victorian
glasshouse dome. Five to ten of them fly in and settle on the perches, and something has
happened: the keeper's mango is gone, the orchids have been nibbled, someone rang the
dinner bell at dawn. One bird did it.

- **One or two liars:** everything they say about the case is false, even what they say
  of their own perches.
- **Two who know the truth:** everything they say is true.
- **The rest only chatter:** about themselves, their kind of parrot, the weather beyond
  the glass, what day it is, the fountain, the fruit, and a great many jokes. They never
  lie, but they are no help at all.

The player is told how many lie and that two know the truth, but not who is playing.

## Play

- **Talk:** click a bird and its line appears in a popup beside it, typed out with a
  squawky babble. Click again for another line. A playing bird comes back to the same
  two or three facts in new words, now and then breaking off to chatter; a chatterer
  never says anything about the case.
- **Look:** some claims can be checked by eye: who sits on wood, rope, brass, iron or
  stone, in the shade of the canopy or in the sun, high or low, beside whom. A liar
  caught out by looking is a liar for certain. A liar even fibs about its own likings
  ("I do love the shade") from a perch in full sun.
- **Ask:** from table 3, one playing bird may refuse to volunteer anything. Its popup
  offers three or four questions; it answers one. Exactly one of them settles the case.
- **Mark:** right-click a bird to mark it truthful (its name tag turns green), again
  for liar (red), again to clear it. Popups have no buttons.
- **BAD POLLY!:** a big comic burst in the bottom right. Click it: it presses in and
  brightens, and a red crosshair takes the pointer. Click the culprit and a paintball
  arcs over and splats on its chest; the bird flaps, and the verdict shows every bird's
  part.
  - **Right:** the culprit confesses, the aviary cheers.
  - **Wrong:** the wrongly accused protests, and the culprit gloats and flies off into
    the dome.
- **Points:** ten for the culprit, and five more for each liar marked red.
- **Records:** points, the best table, cases solved, the current streak and the best
  streak, the last three carried over from The Parrot's Table.

## The aviary (`src/aviary.*`, `src/perching.*`)

- **The house:** a palm house about twenty-five metres across, seen from low on its
  floor looking up. White-painted iron ribs, glass between them with the sky and the
  garden's trees beyond, a gallery round the far wall.
- **The jungle:** broadleaf trees with buttress roots and dense crowns, palms, lianas
  between them, orchids and bromeliads on the trunks, banana clumps and tree ferns two
  rings deep round the wall, low planting over sand from the soil engine
  (`Preset::sand`), a gravel path (`river_gravel`) winding to the fountain, a feeding
  table with fruit, a bunch of bananas, dropped feathers, butterflies, and the
  gardener's watering can and trowel. The canopy's shade is real: the crowns cast it
  through a sun shadow map, and the clearing in the middle is lit by shafts of sun.
- **The perches:** about twenty places on a dozen bars, laid out afresh each table: a
  bare perch tree's branches, bamboo poles, an iron lamp standard, rope swings and a
  brass hoop hung from the ribs, a stone bird bath, the fountain's rim, a driftwood log.
  Birds on one bar are neighbours. No tree may stand between the player and a perch.
- **Settling:** each bird has one to three likings (high or low; wood, rope, brass, iron
  or stone; shade or sun; company, exactly one neighbour, or a bar to itself; no red
  bird beside it). They are dealt from a seating found first, so every bird always finds
  a place it likes. A new table opens with the birds flying in from the dome, a few
  trying a perch they don't like before moving on.
- **Cost:** the still scene is built once a table (about a quarter of a second) and
  drawn once a size; during a window resize the old picture is stretched until the size
  settles. A frame puts the still back and draws the birds, about 5 ms; a quiet aviary
  ticks fifteen times a second.

## The cases (`src/logic.*`)

- **Claims** are formulas over who lies, who did it, and the scene: X lies, X and Y are
  the same sort, X did it, it was X or Y, whoever did it sits on rope / in the shade /
  high / beside X / above X, is red, has a perch alone; and by eye alone: X sits on
  brass, in the sun, above Y, beside Y.
- **Generation:** a hidden truth; each liar and truth-teller gets two or three claims,
  false or true as they must be, chosen to narrow the suspects to one (or, with a
  question bird, to two or three, with exactly one question on offer that settles it).
- **Solvability:** every table is proven by enumerating every world (which playing birds
  lie, and who did it).
- **Progression:** subtler claim kinds from table 3, 5 and 7; question birds from table 3;
  four questions on offer from table 8.

## The voices (`src/script.*`, `src/chatter.cpp`)

- Every claim is worded afresh each time in the bird's manner: the posh macaw, the salty
  sea-dog, the nervous budgie, the gossip, the grump, the scholar, the drama queen, the
  sunny one.
- **Chatter:** a library of lines by topic and generators that put parts together (a
  cloud's shape, a fruit and a verdict, a small adventure, idle words about another
  bird, a question for the player, a song). A bird rarely repeats itself: in a hundred
  lines from one bird, at least eighty-five differ.

## Sound

- **Music:** "Tea for Seven", the chamber waltz from the tea table, for now.
- **Voices:** a squawky babble while each popup types out, pitched by species.
- **Effects:** a bell, wings, marks, the gavel, a cheer.

## Build and tools

- `parrots_rules_tests` (CTest `parrots_rules`, label `design`): the formulas against an
  independent reading, the perching promise over 300 tables, the generator's promises
  over 400, every kind worded, chatter variety.
- `parrots_table_demo [seed] [birds] [liars] [question] [tier]`: a table printed as the
  player meets it, with the truth.
- `parrots_aviary_preview out.ppm [look] [seed] [birds] [W H]`: a still of the aviary,
  and the cost of building and drawing it.
- `--dev` uses a separate save and allows `PT_SCRIPT` (`skip`, `clickN`, `talker`,
  `asker`, `askgood`, `truth`, `accusing`, `right`, `wrong`, `next`, `lvlN`, `seedN`,
  `help`, `records`, `close`, `sizeWxH`).
- Saves are `parrots_table-v2.txt` under `GAMES_STATE_DIR`, versioned, checksummed and
  atomically replaced; The Parrot's Table's `-v1.txt` is read once for its records and
  left untouched.
