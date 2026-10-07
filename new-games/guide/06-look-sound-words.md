# Look, sound and words

The rules make a game correct. This guide is about what makes it one of these
games: the way the collection looks, sounds and talks. Most of what follows was
learned from the owner sending something back.

## Look

**A place, not a diagram.** Each game is a small scene: a meadow, a parlour, a
villain's lair, a brook with a toy crane beside it. The board sits in the scene.
Before drawing a grid, decide where the game happens and what is on the table
next to the board.

**Avoid the obvious setting.** A greenhouse for a plant puzzle was turned down as
a cliché. The setting should be a small surprise that still makes sense.

**The house looks.** Two have worked, and a game should choose one:

- *Crisp 2D*: vector shapes drawn at full resolution with soft gradients, like
  the card tables and the template. Good for boards, cards and tokens.
- *1990s 3D*: a low-resolution, software-rendered 3D scene with textures and
  simple lighting, scaled up with visible pixels, under crisp full-resolution
  text. This is what Eggy, Switchbox, Pen the Sheep and Rock Stack use. The owner
  asked for it by name: "a 3D 90s game style. Pixel art, 3D, realistic,
  textures." The pixelation is deliberate and is also what keeps it cheap.

Whichever you choose, text is always sharp. Scene pixels may be chunky;
lettering never is.

**Match the collection's materials.** When a new card game arrived with its own
table, the question was "why aren't you using the same background generator for
the felt?" If the shelf already has a felt, a card back, a wood, a paper, use it
or make yours clearly belong beside it. Flat is the enemy: the cards were sent
back for "a bit of depth. A sense of feel. Texture. Shading. Thickness. Just a
bit."

**Variety and nature.** Generated things should vary the way real things do.
Rocks that were all rounded were sent back for "more flat and more jagged ones".
Eggy's brief asked for over a hundred kinds of flower. When something is drawn
from life, look at the real thing first: the instruction for Rock Stack's crane
was to find pictures of real cranes and not stop "till it is excellent".

**Characters are animated first.** Switchbox's brief was "really well animated,
that's the focus", and it is the game people remember. A character needs, at
least: an idle pose, a reaction to a good move, a reaction to a bad one, a
waiting gesture, a win and a loss. They look at what the player is doing.

**Physical plausibility.** These were all real corrections, and each one broke
the scene for the person looking at it:

- hands passing through each other; a head through a lid; a crane arm through
  its own cab;
- a sheep facing away from the direction it walks;
- a fence drawn where the sheep then walks through;
- a rock sinking into the ground;
- an object that seemed to turn when only the camera moved.

Check every pose and every path against these. If the player says something
looks wrong and you cannot see it, build a test object that makes it visible (a
rock with arrows painted on it settled the last one).

**Camera.** When there is one, it leads gently and keeps the subject near the
centre. Movement that jerks was called "janky" and redone. The player can look
around a 3D scene where looking around helps.

**The character never hides the game.** Nothing animated may cover a control,
and nothing the player needs is behind someone's head.

**Colour.** Four or five colours and their shades. One bright accent, used for
what matters now. Cream for paper and lettering, deep warm darks for wood and
shadow, sit well beside the shelf. Check contrast on every panel.

**Layout.** Everything reflows, "obviously". At 600 x 370 the board is still
the largest thing on screen, bars slim down, labels shorten, panels fit. Nothing
is clipped, overlapped or left as a tiny island at any size. Use the layout
sweep test and then look at previews at 600 x 320, 600 x 370, 1100 x 760 and a
large size, at scales 1, 1.5 and 2.

**Reduced motion.** With Motion off, the game is still handsome: changes appear
at once, nothing shakes or drifts.

**Hide the totals.** Do not show how much of a generated journey remains or how
many secrets exist. Discovery is part of the fun.

**Small surprises.** A hidden reaction for the player who pokes at things
(clicking the girl's head twenty times starts a game of whack-a-mole) is in the
spirit of the collection. Keep them harmless and findable by play.

## Sound

**Everything is synthesized, here.** Every sound is rendered by a script in
`games/<id>/audio_src/`. No samples, no recordings, no borrowed melodies, no
voices from anywhere. A reference track may guide the mood; nothing is taken
from it.

**Music is a mood, and it loops without a seam.**

- One to three minutes per loop. A short oscillator loop is not music.
- It should be possible to leave it on all afternoon: gentle dynamics, no hook
  that grates the twentieth time.
- It starts when the game is in front and stops when it is not. It never
  restarts because of a save or a new game.
- It may respond to the game. Four Pegs has a comic lair theme that shifts,
  seamlessly and on a bar line, into panic as the turns run out. Do this only
  where the game has tension to follow.
- Describe the feeling you are aiming for in the brief and let the person judge
  by ear. "The music is nice. But it's not correct," was the verdict on one that
  measured fine and felt wrong; another "sounds demonic, a bit". You cannot hear
  it, so say so, and give them the file.

**Ambience is layered.** A place sounds like several things at once, at
different lengths so the pattern never repeats: a brook was redone from one
short loop into three beds of different lengths plus occasional birds, reeds and
frogs. Machine noises are faint.

**Effects are soft and few.**

- Every move makes a sound; the sound says what happened (a higher note for
  something gained, a lower one for something lost).
- They sit clearly over quiet music with no harsh peaks. Leave headroom.
- Vary pitch slightly on repeated sounds so they do not machine-gun.
- No celebratory notes on hover. A fanfare is for a win.
- Sound must never give away hidden information.
- Fade everything out over a tenth of a second when stopping; cutting a wave
  mid-cycle popped the speaker and was a reported bug.

**Voices are babble.** Characters speak in text, with a synthesized murmur,
grumble or sing-song under it to give them a voice. No recorded or generated
speech.

**Files.** 48 kHz. Effects as short WAV files; music and long beds as `.m4a`
with their exact loop length in the game's audio manifest (see
[primitives](07-primitives.md)). Name everything with the game's prefix.

**What you can and cannot check.** You can check that files decode, their
length, their peak level and that a loop's ends meet. You cannot hear them.
Numerical validation is not a listening pass; write exactly that in
`HANDOFF.md`.

## Words

**The character talks like someone.** Decide the voice in the brief and write
three sample lines for the person to correct. The Curator is formal and
theatrical; the sheep's thoughts are short and smug.

**Many lines, from parts.** A pool of ten lines is a recording by the third
game. Write enough that a session never repeats, or assemble lines from
interchangeable openings, subjects and closings. Lines should depend on what
just happened: a character who notices the player's situation "has some
self-awareness", and that is what the owner asked for.

**Escalate.** The first mistake gets a raised eyebrow. The fifth gets a speech.
Keep a ladder of reactions and climb it.

**Threats are absurd and specific.** A villain who says exactly what ridiculous
thing he will do is funny. A vague one is not.

**Plain labels.** Buttons and headings say what the thing is. "Records", not a
flourish that needs explaining.

**Help is short, complete, and in the game's voice.** Goal, move, ending, keys.
It may carry a joke.

**Nothing mean.** No mockery of the player, no real people, no groups of people
as a joke. Humour here is warm.

**Language.** English, plain, sentence case. Check every line fits its place at
the smallest window.
