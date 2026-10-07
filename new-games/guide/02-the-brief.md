# The brief

A brief is one page that says what the game is, agreed with the person before
any of it is built. It lives in `games/<id>/README.md` and stays true as the
game changes. The gate refuses a brief with `TODO` left in it.

The brief exists because the expensive mistakes are made in the first hour: a
game too long for the shelf, a character nobody asked for, a name that has to be
changed after the save file and the shelf entry carry it.

## The conversation

Ask the person questions until you can fill every heading below without
inventing anything important. Ask a few at a time. Offer your own suggestion
with each question, so they have something to react to; most people find it
easier to correct a proposal than to answer an open question.

Good questions:

- "Tell me about one turn. What do I click, and what happens?"
- "What does it look like when I win? When I lose?"
- "Who else is there? Is anyone watching me play?"
- "How long should one game take? What happens after it ends?"
- "What did you love about the original?" (when it is a remake)
- "What should it never do?"

Listen for what they are fond of. That is the part to protect when you simplify
everything else.

When their idea runs against [what belongs](01-what-belongs.md), say so now.
"That would take an hour to play, and this shelf is for ten-minute games. What
if a session were a single voyage instead of the whole trading season?"

## The headings

**The idea in two sentences.** If it takes more, the game is not yet clear.

**Why it belongs.** Which of the four purposes it serves, and the one thing it
adds that no box on the shelf has. Name the nearest existing game and say how
this differs.

**A session.** The first minute, the length of one game, what "one more" feels
like, and what the player finds when they come back after closing the window
mid-move.

**Who is in it.** Name, manner, voice. When they speak: on a good move, a bad
one, a long wait, a repeated mistake, a win, a loss. How their reactions
escalate. Three sample lines, so the person can hear the voice and correct it.

**Rules.** Complete enough for a stranger to referee. For generated puzzles: how
they are generated, and how the game knows each one can be finished. For games
against the computer: how the opponent decides, and that it never cheats.

**Look, sound and words.** The scene in a sentence. Four or five colours. The
one animation that makes it feel alive. The music's mood and tempo in plain
words (a game is expected to have music; if the first delivery has none, say so
in the handoff). What a move sounds like. See
[look, sound and words](06-look-sound-words.md).

**Controls and commands.** What the mouse does. What the keyboard does (every
game is fully playable from the keyboard where the game allows it). The
capsule's commands, and which is primary. See [the shell](04-the-shell.md).

**What it will not do.** The tempting additions decided against, with reasons.
This list protects the game from growing during polish.

## Names

Three names are chosen once and then appear in code, saves and assets:

| Name | Rules | Example |
|---|---|---|
| Title | At most 28 characters; fits a box; says what the game is, plainly | Tide Pools |
| Kind | Two or three words under the title, at most 24 characters | Shore puzzle |
| Blurb | One inviting sentence for the shelf ticket, at most 90 characters | |
| Id | 3 to 20 lowercase letters and digits; the folder and the shelf entry | `tidepools` |
| Namespace | 2 to 4 lowercase letters, not used by any other game | `tp` |

The save file (`tide_pools-v1.txt`), the view (`tp::TidePoolsView`) and the audio
prefix (`tp_`) follow from these. `new_game.py` checks them and refuses a clash.

Labels inside the game follow the same rule as the title: say what the thing
is. A sheet of test results is not "the ledger".

Settle the title with the person before running `new_game.py`. Rock Stack was
delivered under another name and had to be renamed during integration.

## Scope

A first version is the smallest game that is already good: one mode, one
character, one scene, sounds, help, a save. Leave out the level select, the
second character and the options panel until the person has played it and asked.

Write down, in "What it will not do", each thing you both agreed to leave out.
