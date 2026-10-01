# Games mask adapter requirements

The animated games compose their scene into GUI.Forms LiveSurface pixels. They need text layout and alpha masks from the shared text service; they do not require a separate native presenter or platform font API. This document proposes the remaining service behavior. It does not claim that wrapping or monochrome rasterization is available in the current prepared-text development profile.

## Inputs and coordinates

The existing `text_mask` adapters receive UTF-8, a game font role, a floating font size, and a floating wrap width. Both size and width are in the mask's logical pixel grid, before window DPI scaling. A positive wrap width is a layout constraint; zero means no soft wrapping. The game converts its scene width to this grid before calling the adapter: Atom Probe, Four Pegs and Switchbox multiply by their scene pixel size, normally 2; Eggy uses its chosen pixel size.

Keep layout in double precision. Propose quantizing font size and wrap width once to 1/64 logical pixel, nearest with ties away from zero, and using those same values for layout and cache identity. The service must expose its actual quantization if different. Device scale affects rasterization, not the logical wrap width or the selected line breaks. Test scale 1, 1.25, 1.5 and 2. Pixel-style text uses an integer-scale monochrome mask; ordinary dialogue uses grayscale coverage at the actual device scale.

## Lines and overflow

- Preserve explicit LF breaks. Treat CRLF as one break; normalize an isolated CR to LF at the adapter boundary. Preserve empty and trailing explicit lines in layout height. Empty input has no ink and zero logical advance.
- Use word wrapping at the requested width without automatic hyphenation or ellipsis. Preserve leading spaces and the spacing inside a line; spaces consumed by a soft break need not become ink at the next line's start. The score tables use repeated spaces and must retain their alignment.
- If a word cannot fit an otherwise empty line, break at an extended grapheme boundary using shared layout knowledge. Never split UTF-8 bytes, a combining sequence, or a shaping cluster. If one indivisible cluster exceeds the width, retain it and report horizontal overflow; clipping belongs to the canvas compositor.
- Tabs are not used by these game interfaces. A first profile may refuse them explicitly. A refusal must not be presented as a successfully rendered empty string.
- Left alignment and a requested additional line gap of 0.05 times font size preserve the current dialogue intent. Expose line baselines, logical extents and ink bounds separately. A face change is allowed to change exact line breaks; source, face identity, size and width held fixed must be deterministic.

The provider owns shaping, bidi resolution and contextual line breaking. The Games adapter will not measure and split independently shaped substrings.

## Masks, clipping and ownership

Grayscale output is an 8-bit linear coverage mask with no RGB subpixel channels. Monochrome means a glyph rasterization mode producing only 0 or 255 coverage, with its matching glyph loading/hinting policy; it does not mean thresholding a completed grayscale mask. Expand packed mono coverage to the same 8-bit mask storage at the shared raster boundary. This is primarily required by Eggy's pixel text.

Return the mask's signed origin relative to the layout anchor, row stride, dimensions and immutable owned storage. Ink bounds must contain overhangs and accents. The compatibility adapter may add a one-pixel transparent border, but must not clip an italic bearing or descender to a logical advance rectangle. Canvas clipping remains in the game's bounded mask blit.

Proposed consumer cache: at most 700 entries and 32 MiB of retained mask bytes per active game; key by UTF-8 bytes, registered face identity/revision, size, wrap width, spacing, raster profile and scale. The adapter returns owning handles or holds a frame's owners until publication completes. Eviction drops only cache ownership and cannot invalidate a mask being composed. Hiding/detaching a game drops its cache and cancels pending work; stale results must not revive the old view. A failed preparation preserves the last complete frame and reports its typed failure.

Proposed individual request limits are 16 KiB UTF-8, 4096 logical pixels per axis, 256 lines, and 16 MiB of coverage storage. Return an explicit limit result rather than truncate. These are integration proposals, not changes to the provider's existing budget contract.

## Font fixtures

`assets/fonts/manifest.json` records exact candidate font bytes and licenses. No Apple or Windows system font is redistributed. All bundled candidates retain their SIL OFL files. The candidates are:

| Game role | Encoded face candidates |
| --- | --- |
| Four Pegs dialogue and headings | Libre Baskerville Regular / Bold |
| Atom Probe dialogue and headings | Barlow Condensed Regular / Bold |
| Switchbox handwritten dialogue | Comic Neue Regular / Bold |
| Eggy pixel text and generic pixel roles | Cousine Regular / Bold |
| Neutral UI role | Carlito Regular / Bold |

These choices need inspection in the actual shared renderer before release. They are independent registered encoded faces; they must not silently replace the toolkit's application-wide font roles.

`tests/fixtures/portable_text.json` contains actual game strings plus edge cases. Numeric mask goldens must be produced and reviewed with the pinned shared raster implementation; they are deliberately not guessed from CoreText or another font renderer. Initial assertions concern complete text coverage, line semantics, wrap bounds, mono coverage and scale-independent logical layout. The full help panels must then be inspected at ordinary and fractional DPI to verify that the last paragraph and buttons remain visible.

Author: Astra
Sponsor: Rainstar
