# Maze 95 posters and art

The pictures on Maze 95's walls. The posters are advertising from inside the maze's world, in
the manner of 1995–2001 box art, arcade flyers, toy ads and Windows-era software; every product,
brand and slogan on them is made up. The art is the period's pictures themselves, framed:
renders from 1996 3D software, fractals, Frutiger Aero, airbrush, neon and lasers, Kid Pix and
Paint, clip art and WordArt, the screensavers, Memphis, slime and lava, millennium gel, and a
Magic Eye. Each maze hangs one or two posters and three or four pieces of art.

```sh
python3 make.py              # writes games/maze/assets/posters: poster_00.png on, art_00.png on
python3 make.py out --names  # the same, named after each poster, for review
python3 sheet.py sheet.png out 7
```

Needs Python 3 with numpy and Pillow. The lettering uses only the repository's OFL fonts
(`assets/fonts`); `footage/` holds in-game renders for the "actual game footage" strips.

- `kit.py`: an SDF raymarcher for the 3D props, and the period's lettering and furniture:
  chrome, gold, WordArt, neon tubes, gel buttons, brushed metal, lens flares, starbursts,
  stickers, slime, Windows 95 windows and buttons.
- `props.py`: the floating golden maze, and the paint snail as a mascot.
- `ads.py`: box art, flyers and ads. `eras.py`: the Windows 95, 98 and 2000 years.
- `gallery_a.py`, `gallery_b.py`: the art. `frames.py`: their frames, kept slim.
  `preview_art.py` renders pieces framed for review.

A wall keeps a picture's number, so new posters and art go at the end of their lists.
Each run paints the same bytes.
