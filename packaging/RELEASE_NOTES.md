PlaySuite 0.4.2 restores the command menu and the return-to-shelf control in Solitaire and other full-window games.

The 0.4.1 lazy-loading change placed newly created games above the command capsule. Those games could cover the menu and intercept its mouse clicks. Games now stay behind the shelf and command capsule in both paint order and hit-testing.

Regression tests click the visible Back control through the window input path in all seventeen games, with the menu both folded and expanded at the minimum window size. The idle CPU and memory improvements from 0.4.1 remain in place.

The command capsule also keeps a fixed left edge while expanding, so Back and the primary commands no longer move away from the approaching pointer.

PlaySuite was previously named Games. Existing save locations and internal game identifiers are preserved.

Choose the download for your computer:

- Windows x64: run the setup installer, or extract the portable ZIP and open `PlaySuite/games.exe`.
- macOS Apple silicon, macOS 15 or later: install the PKG, or extract the ZIP and move `PlaySuite.app` to Applications. The app is signed ad hoc; it is not Developer ID signed or notarized.
- Linux x64 (amd64) or arm64, Ubuntu 24.04 or compatible: install the DEB using `sudo apt install ./playsuite-*.deb`. The tar archive contains the same application and requires the system libraries declared by the DEB, including X11, ATK and ALSA.

Each platform is built and tested natively. The release workflow tests the complete collection and launches the packaged application. Human gameplay and listening checks remain valuable; please report the platform, game, action and observed result.

Atom Probe, Four Pegs and Switchbox use their separate v2 save files. Their v1 saves are retained. Personal saves are outside the installation directory and survive uninstalling the application.

Author: Astra
Sponsor: Rainstar
