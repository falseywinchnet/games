# Hanafuda woodblock faces

The 48 faces in `assets/cards` use original illustrations generated with the built-in OpenAI image generator, in a traditional woodblock direction. Three reviewed sixteen-panel sheets cover January–April, May–August and September–December. `prompts.json` records the complete prompts; the first sheet supplied the style reference for the other two.

The month, ribbon color and special-card identities follow the delivered Koi-Koi rules. Traditional motifs were checked against [Nintendo Museum's hanafuda guide](https://museum.nintendo.com/en/guide/hanafuda/index.html). These are new illustrations, not reproductions of a particular manufacturer's deck. Ribbon brush marks are decorative rather than a transcription of historical poetry.

`assemble.py` crops the regular atlas cells and resizes them into the delivered cards' 308 × 420 artwork insets. It preserves the original 360 × 504 frame and the deterministic month/type labels. Run it with Python and Pillow from any directory. The original incoming package is read-only input. Full sheets are authoring files and are not included in installed packages.

Release preparation ships each card as compressed, lossless PNG (8-bit RGBA, not interlaced; a source already in that layout is shipped byte for byte) and records the premultiplied BGRA pixels' digests in `cards/manifest.json` and `cards/verification.tsv`. The game decodes the PNGs in portable C++ with the ambient engine's zlib decoder, on worker threads, to exactly those pixels (`koikoi_card_art` checks every card); neither ImageIO nor a runtime Python installation is needed. The final art was inspected as a complete small-card gallery and in the rendered Koi-Koi table.

Author: Astra
Sponsor: Rainstar
