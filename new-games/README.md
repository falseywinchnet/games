# Submitting a game to PlaySuite

PlaySuite is a collection of small games for Windows, macOS and Linux. It runs on
any computer made since about 2015 and is played by anyone, anywhere. A game joins
when it meets every rule and requirement below. Submissions are judged against
these and nothing else, and every decision comes with its reasons.

## Rules

1. **Casual.** A player can begin without instruction, stop at any moment, and
   return to exactly where they stopped. A game that depends on sustained
   attention, punishes a pause, or asks for a long investment of time or effort
   does not qualify. Adventure and combat games do not qualify.

2. **A classic or something new.** The game is either a classic, presented with
   care and its complete rules, or an original game that nobody has made before.
   An original game has depth: its decisions stay interesting after many plays. A
   single reflex or a single repeated action is a demo, not a game.

3. **Style.** The game uses one of three looks:
   - isometric;
   - the house 3D style: a low-resolution, software-rendered 3D scene, textured
     and lit, enlarged with visible pixels, under sharp text;
   - polished 2D: vector drawing with depth, shading and texture.

   It is finished at every window size and stands beside the games already in the
   collection without looking unfinished.

4. **Replayable.** The game stays worth playing for a long time, through generated
   content, chosen difficulty, or both. If it needs teaching, the teaching is its
   easiest difficulty: short levels that each show one mechanism, chosen at random.
   There is no fixed tutorial sequence.

5. **Positive.** Content suits every age and any public place. Characters are
   warm, humour is aimed at no one, and stakes are theatrical.

6. **Self-contained and original.** No network, accounts, advertising, purchases,
   streaks or notifications. Every character, image, sound and line of text is
   original to the game or licensed for redistribution under the repository's
   licence. No brands, and nothing taken from another work.

## Requirements

These are checked when the game is built and reviewed.

1. **Standard controls.** The game uses the collection's controls: the command
   bar with New game, shared help (H or F1), the Music and Sound masters (M toggles
   music), Escape to close a panel, Enter or Space to confirm, Z or Backspace to
   undo where undo exists, and the arrow keys wherever the rules allow keyboard
   play.
2. **Saves.** A game with state saves it, through the collection's save format,
   after every committed change, and resumes it on reopening.
3. **Compute.** When nothing moves, the game does no work. While it animates, it
   uses less than 30% of one core. When hidden, it stops entirely.
4. **Passive mode.** The game's own engine can play it. A game that no engine can
   play is too complex for the collection. The more complex the game, the more it
   is expected to offer this as a passive mode the player can watch and take over
   at any moment.
5. **Size.** The game adds at most 32 MB to the installer. Most add far less.
6. **Windows and sizes.** The game works on all three systems, in a window as
   small as 600 × 420, at display scales 1 to 2.
7. **Sound.** Music follows the Music master and everything else follows the
   Sound master. All audio is made by the game's own code.
8. **Tests.** Rules, saves and layout are tested, and the game passes the
   submission check.

## How to submit

1. Fork [falseywinchnet/games](https://github.com/falseywinchnet/games).
2. Build the game in one folder, `games/<id>/`. With an AI coding model, open the
   repository and say: "Read `new-games/AGENTS.md`. Build this game: …". Without
   one, follow the same file.
3. Run the submission check until it passes:

   ```sh
   python3 new-games/tools/check_game.py <id> --fetch-toolkit
   ```

4. Open a pull request that adds only `games/<id>/`. In its description, say
   whether the game is a classic or new (rule 2), how it is replayable (rule 4),
   and what its passive mode does (requirement 4).

The pull request is built and tested on all three systems. If it is accepted and
merged, the next release includes it.

By submitting, you agree that the game is distributed under the repository's
[licence](../LICENSE), and that the maintainer may change it.
