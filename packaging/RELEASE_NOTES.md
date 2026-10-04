PlaySuite 0.4.7 converts Gems and Untangle to direct framebuffer presentation.

- Both games publish device-sized, fully composed frames through GUI.Forms LiveSurface, using the same direct presentation path as the integrated vendor games.
- Untangle caches its stationary background and yarn as pixels. Ordinary animation restores only affected areas before drawing the cat, knots, pegs and effects. Moving a peg updates the yarn and crossing geometry together.
- Gems composes its transparent gem raster over cached board pixels, preserving filtering, glints, highlights, shadows, effects and text. Its private image storage updates in place during animation.
- Native Windows DIB and Skia offscreen painters preserve each platform's existing artwork and fonts. Tests compare direct frames against full redraws at 100%, 125% and 200% scale, including narrow layouts and partial updates. Windows comparisons are exact; Skia permits a one-level channel rounding difference from clipped blending in at most 0.01% of pixels.
- The command capsule remains above direct game frames. Help, H/F1, M, saved games, mouse controls and all eighteen games are retained. Exposing a window or closing help restores the latest frame; hidden games stop publishing.

Direct presentation removes repeated control-tree painting during ordinary animation. Gem raster generation and filtered composition still cost CPU; this change does not claim to eliminate all rendering cost. Each producer publishes a complete immutable frame, so skipped frames and rotating buffers remain correct. Rock Stack is unchanged in this release.

PlaySuite was previously named Games. Existing save locations and internal game identifiers are preserved.

Choose the download for your computer:

- Windows x64: run the setup installer, or extract the portable ZIP and open `PlaySuite/games.exe`.
- macOS Apple silicon, macOS 15 or later: install the PKG, or extract the ZIP and move `PlaySuite.app` to Applications. The app is signed ad hoc; it is not Developer ID signed or notarized.
- Linux x64 (amd64) or arm64, Ubuntu 24.04 or compatible: install the DEB using `sudo apt install ./playsuite-*.deb`. The tar archive contains the same application and requires the system libraries declared by the DEB, including X11, ATK and ALSA.

Each platform is built and tested natively. The release workflow tests the complete collection and launches the packaged application. Human gameplay and listening checks remain valuable; please report the platform, game, action and observed result.

Atom Probe, Four Pegs and Switchbox use their separate v2 save files. Their v1 saves are retained. Personal saves are outside the installation directory and survive uninstalling the application.

Author: Astra
Sponsor: Rainstar
