# Make a game for PlaySuite

PlaySuite is a shelf of small games: card tables, logic puzzles, a sheep that
won't stay penned, a butler who guards a code, a toy crane stacking river stones.
Every one of them was made the way you can make the next one: a person had an
idea, described it to an AI model, played what came back, and kept saying what was
wrong until it was right.

This folder is everything that process needs. You bring the idea and the judgment.
The model does the building, and this folder tells it how.

## Could you come up with one?

Probably. The ideas that have worked were not clever inventions. They were small
things someone was fond of:

- A puzzle from an old magazine or an old computer that nobody remade.
- A game a grandparent played at the kitchen table.
- A toy on a desk, turned into something you can click.
- A familiar puzzle with someone in it: a character who watches, comments, sulks
  when you win and gloats when you lose.

If you can explain your game to a friend in two sentences and they want a turn,
that is enough to start.

## What belongs on this shelf

PlaySuite is a collection for the workplace. Its games are for the ten minutes
between two tasks, and they are chosen with care. A game belongs when it does at
least one of these four things, and gets in the way of none of them:

1. **Entertains.** It is something to do when you are bored.
2. **Trains the mind.** There is a little thinking in it.
3. **Lightens the heart.** Life is often hard. A game that makes someone smile is
   doing real work.
4. **Occupies time without competing with what matters.** It is distracting, but
   not too distracting. You can stop at any moment and lose nothing.

That last one rules out more than you might expect. A game that needs an hour, or
your full concentration, or quick reflexes, or daily visits, is a good game for
somewhere else.

A few more things the shelf's games have in common:

- **They are short and safe to leave.** Close the window in the middle and the
  game is exactly there when you come back.
- **They are fair.** Every puzzle can be solved. Nothing is hidden that the rules
  don't let you find. Luck may deal the cards, but it never cheats.
- **They are not clichés.** The well-known classics are already here. Another
  version of something every phone already has would be the boring choice. A
  classic that almost nobody remembers is welcome.
- **They add something.** A game that only repeats what three others on the shelf
  already do does not earn a box.
- **They have warmth.** The best ones have a character you get fond of. Cute is
  good. Funny is good. The stakes are theatrical, never grim.
- **They ask for nothing.** No accounts, no internet, no streaks, no daily
  rewards, no shop. The game works on a laptop with no connection, forever.

[What belongs](guide/01-what-belongs.md) has the longer version, with the games
already on the shelf and examples of ideas that fit and ideas that don't.

## How it goes

1. **Get the repository.** Download or clone
   [falseywinchnet/games](https://github.com/falseywinchnet/games).
2. **Open it with an AI coding model.** Claude Code and Codex both work. Point it
   at this folder and say what you want:

   > Read `new-games/AGENTS.md`. I want to make a new game for the shelf.
   > Here's my idea: ...

3. **Talk it through.** The model will ask you questions before it builds
   anything: who is in the game, how a turn goes, what winning feels like. Answer
   in your own words. You don't need to know anything about programming.
4. **Look at what it makes.** The model can show you pictures of the game at
   every stage, at the sizes the shelf uses. Say what's wrong. "The sheep faces
   the wrong way." "The help text is too small." "He isn't villainous enough."
   Plain complaints are the most useful thing you can give it.
5. **Send it in.** When you're happy, the model packages the game as one new
   folder plus a handful of edits that put its box on the shelf, and opens a pull
   request. The repository's build machines then compile the whole suite for
   Windows, macOS and Linux, with your game in it, and you can download that
   build and play it properly.

The one thing the model cannot do on your computer is compile the full PlaySuite
application. That happens on the repository's build machines after you send the
game in. Until then, the model works from its rule tests and from rendered
pictures of the game, which is why your eyes on those pictures matter.

## What you'll be asked to decide

The model will bring you choices. These are yours to make, and they are what
make the game yours:

- **The name.** Short enough for a box. Say what it is.
- **Who is in it.** A name, a manner, how they talk.
- **What a session is.** One puzzle? Best of three? An endless supply?
- **How hard.** The shelf prefers difficulty the player chooses for themselves.
- **When it's done.** You are the one who says the game is finished. Take your
  time. Polished beats early.

## What happens to your game

The maintainer plays every submission and decides whether it goes on the shelf.
Some good games will be turned down because they don't suit this particular
collection. That is a judgment about the shelf, and the reasons will be given.

By contributing, you agree to the repository's [license](../LICENSE): your game
becomes part of PlaySuite, free for anyone to play, with the maintainer free to
change and distribute it. Everything in it must be yours to give. That means
original characters, original art, original music, and no brand names or logos.

## For the model

Start at [AGENTS.md](AGENTS.md). It is the whole procedure, and it links to each
guide where that guide is needed.

| | |
|---|---|
| [AGENTS.md](AGENTS.md) | The procedure, start to finish |
| [guide/01-what-belongs.md](guide/01-what-belongs.md) | The shelf's taste, and the games already on it |
| [guide/02-the-brief.md](guide/02-the-brief.md) | Turning an idea into a brief worth building |
| [guide/03-anatomy.md](guide/03-anatomy.md) | How a game is laid out, and what "standalone" means |
| [guide/04-the-shell.md](guide/04-the-shell.md) | The shelf, the command capsule and what the shell expects |
| [guide/05-performance.md](guide/05-performance.md) | Doing no work when nothing changes |
| [guide/06-look-sound-words.md](guide/06-look-sound-words.md) | Art, sound and writing |
| [guide/07-primitives.md](guide/07-primitives.md) | The drawing, text, sound and 3D pieces you can borrow |
| [guide/08-on-the-shelf.md](guide/08-on-the-shelf.md) | Every edit that puts the box on the shelf |
| [guide/09-deliver.md](guide/09-deliver.md) | Checking, the handoff and the pull request |
| [guide/10-house-style.md](guide/10-house-style.md) | How the C++ is written |
| `template/` | A complete small game to start from |
| `tools/` | `new_game.py`, `wire_shelf.py`, `check_game.py`, `shrink_png.py` |
| `kit/` | The headless harness: real fonts and PNG output with no window |
