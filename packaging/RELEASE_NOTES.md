PlaySuite 0.4.5 brings the collection's instructions into one readable help document and improves input responsiveness during animation on Windows.

- Open the paper-style help document with the plain `?` in the top-right corner, H or F1. Each game opens at its own instructions. Sections expand, text wraps, and the document scrolls down to the smallest supported window. H or Escape returns to the game.
- M mutes or unmutes music throughout the collection, including while help is open. Text-entry fields retain their ordinary typing behavior. The shelf includes the dedication, agent credits and contact information.
- Hosted games route their explanation panels into the shared document. Ordinary window repaints restore the latest vendor-game frame, including after help closes. The new-game kit teaches and checks the same integration.
- Gems and Untangle retain static, animated and text drawing separately. Untangle reuses crossing geometry until a peg moves, and cat-only animation damages its old and new bounds. Dragging updates the yarn and peg together, including the full shadow margins.
- The private Windows message loop gives input a bounded turn alongside posted callbacks, including input delivered as posted messages. Continuous render requests no longer take every turn ahead of clicks and keys. Animation remains enabled, and drawing still gets its own turn.
- Windows repaints include the complete device-pixel update bounds. This fixes thin stale-pixel lines on the help sheet while animation continues behind it.

All eighteen games, their command menus, Four Pegs mouse controls and Solitaire's accumulating waste pile are retained. Drawing quality, animations, game rules and save formats are unchanged.

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
