# Puzzle artwork

Generated with the built-in imagegen tool for this project. Original outputs were copied unchanged into assets/. These assets are preparation for the requested games; their presence does not mean those modules are playable yet.

- nature-lake.png: tranquil lake backdrop for the reflective cube game.
- switchbox-girl.png: original transparent anime mascot for Switchbox.

## Nature backdrop prompt

Use case: photorealistic-natural. Asset type: wide background for a calming native 3D cube connection puzzle. A tranquil clear woodland lake, soft mossy shore and ferns at the lower corners, slender green trees framing left and right edges, luminous hazy distant forest and water reflections. Spacious quiet central area of softly reflected sky and water where the game will draw a reflective cube; no cube or game interface in the image itself. Soft morning light, rich natural greens, cool blue water, peaceful refined photographic quality. Wide landscape 1536 by 1024. No text, no symbols, no people, no buildings, no watermark.

## Switchbox character prompt

Use case: stylized-concept. Asset type: transparent-background character sprite for a cute anime switchbox puzzle. Original cheerful mischievous chibi anime girl with fluffy chestnut bobbed hair, two tiny star-shaped hair clips, large expressive hazel eyes, soft peach cheeks and a cozy mint-green sweater with cream cuffs. Wholesome playful desktop-game mascot. Front view, shown from waist up, body centered and both small hands visible resting just in front of her torso as if peeking out of a toy box (DO NOT draw the box; the game renders it). Refined warm clean anime line art and softly painted cel shading, charming rather than generic emoji. Leave clear transparent padding around the whole silhouette. Single character, one pose, 1024 square. True transparent background, no text, no watermark, no floor, no scenery, no accessories other than the stated hair clips.

# Classic card faces

classic_00.png ... classic_51.png are a reprint of the pack Goodall & Son printed for the
coronation of Edward VII in 1902 (Worshipful Company of Makers of Playing Cards collection,
acquisition 206): the English pattern that later standard packs follow, long out of
copyright. `authoring/cards1902/make_faces.py` makes them from the collection's scans,
which are not kept here: it separates the four inks from the old board, cleans them and
lays them again on a soft off-white card at the painted faces' size and corner.
Scans: WCMPC Collection (Worshipful Company of Makers of Playing Cards), whose site allows
its images to be used for non-profit purposes with this acknowledgement.

# Classic card back

back_1899.png, the default back, is a reprint of New York Consolidated Card Company's
1899 back (WCMPC Collection, acquisition 305): flowers and leaves round a rosette,
voided white from one navy ink. Made with `authoring/cards1902/make_faces.py --back`
from the collection's scan, which is not kept here.
