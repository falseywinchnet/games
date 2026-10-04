PlaySuite 0.4.4 fixes Rock Stack's concurrent startup-cache initialization and reduces repeated rendering and timer work in the older games.

- Rock Stack safely shares its mesh and stone-texture caches during parallel construction. Its cold-start regression test compares concurrent results with serial construction.
- Gems and Nature Cube publish board-image changes with bounded damage, and unchanged layouts reuse the current raster. Cube tracing combines rapid pointer input into one render per animation frame.
- Sudoku keeps its generation waiting screen still until the puzzle is ready. Hearts sleeps until the next computer-play deadline when its cards are at rest.
- The Windows renderer reuses gradients and exact shadow coverage within fixed memory limits. Native pixel tests cover transparency, clipping, translation, resizing and cache eviction. This renderer change is specific to Windows; the game-side changes apply across platforms.

All eighteen games, the restored command menus, Four Pegs mouse controls and Solitaire's accumulating waste pile are retained. Drawing quality, animations, game rules and save formats are unchanged.

Performance work continues: animated Gems and Untangle can still be CPU-bound, particularly on slower machines. Rock Stack's physics correctness tests pass; its separate 1.5 ms stress-performance target remains unmet and is not a release gate.

PlaySuite was previously named Games. Existing save locations and internal game identifiers are preserved.

Choose the download for your computer:

- Windows x64: run the setup installer, or extract the portable ZIP and open `PlaySuite/games.exe`.
- macOS Apple silicon, macOS 15 or later: install the PKG, or extract the ZIP and move `PlaySuite.app` to Applications. The app is signed ad hoc; it is not Developer ID signed or notarized.
- Linux x64 (amd64) or arm64, Ubuntu 24.04 or compatible: install the DEB using `sudo apt install ./playsuite-*.deb`. The tar archive contains the same application and requires the system libraries declared by the DEB, including X11, ATK and ALSA.

Each platform is built and tested natively. The release workflow tests the complete collection and launches the packaged application. Human gameplay and listening checks remain valuable; please report the platform, game, action and observed result.

Atom Probe, Four Pegs and Switchbox use their separate v2 save files. Their v1 saves are retained. Personal saves are outside the installation directory and survive uninstalling the application.

Author: Astra
Sponsor: Rainstar
