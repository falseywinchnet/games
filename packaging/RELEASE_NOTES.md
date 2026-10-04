PlaySuite adds Catching Thieves and Maze 95 and makes the entire twenty-game collection discoverable from individual game folders.

- Catching Thieves retains the full 243-puzzle pumpkin garden campaign, animated raccoons, seasonal scenery, undo, garden book and original rendering.
- Maze 95 brings the complete first-person procedural maze game, including portals, ceiling flips, elevators, wandering encounters and a 46-item trophy shelf.
- The shelf counts games automatically and scrolls with a native vertical scrollbar. Keyboard selection stays visible as the collection grows.
- Every game's cover, help, factory and build registration now live with its module. Existing game IDs and saves are preserved; shelf state migrates from the old bitmask to an extensible list.
- Game authors can test independent rules, launch a standalone native window with isolated saves, and run scripted native input before testing the hosted collection. Adding one game folder supplies registration, help and assets automatically.
- Main-branch publication now assigns one version before building and releases the exact packages that passed all four native platform checks and packaged-launch tests.

PlaySuite was previously named Games. Existing save locations and internal game identifiers are preserved.

Choose the download for your computer:

- Windows x64: run the setup installer, or extract the portable ZIP and open `PlaySuite/games.exe`.
- macOS Apple silicon, macOS 15 or later: install the PKG, or extract the ZIP and move `PlaySuite.app` to Applications. The app is signed ad hoc; it is not Developer ID signed or notarized.
- Linux x64 (amd64) or arm64, Ubuntu 24.04 or compatible: install the DEB using `sudo apt install ./playsuite-*.deb`. The tar archive contains the same application and requires the system libraries declared by the DEB, including X11, ATK and ALSA.

Each platform is built and tested natively. The release workflow tests the complete collection and launches the packaged application. Human gameplay and listening checks remain valuable; please report the platform, game, action and observed result.

Atom Probe, Four Pegs and Switchbox use their separate v2 save files. Their v1 saves are retained. Personal saves are outside the installation directory and survive uninstalling the application.

Author: Astra
Sponsor: Rainstar
