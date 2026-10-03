PlaySuite 0.4.1 removes unnecessary idle work and initial resource allocation while retaining the seventeen-game collection and its artwork.

- Games are created on their first visit. The shelf no longer loads every game's images and framebuffers at startup, and hidden games are not repeatedly laid out.
- Shelf timers stop after animations and pending text/audio preparation complete. Hidden vendor game timers stop and resume with a fresh time origin. Eggy's background climbing remains intact.
- A settled Koi-Koi table stops rendering and publishing frames. Card movement, computer turns, pulsing hints, banners and music fades still run when needed; input and resize wake the view again.
- Eggy's text preparation snapshots only HUD values and speech coordinates instead of copying the complete simulation and mountain caches every frame. Exposed windows redraw their latest retained frame.
- Runtime packages omit duplicate hanafuda source PNGs, saving about 14.9 MiB of installed data. The prepared pixel files are unchanged; artwork and audio fidelity are preserved.
- A native profiling mode and Windows process-counter collector make idle CPU, memory and rendering activity reproducible with isolated saves. Regression tests cover hidden-game silence, resume, and static Koi-Koi scheduling.

This is the first performance pass. Visited games still retain their render caches, animated scenes need further profiling, and the pinned Windows toolkit retains a periodic presentation clock for registered live surfaces. The implementation and measurement protocol are documented in docs/PERFORMANCE.md.

PlaySuite was previously named Games. Existing save locations and internal game identifiers are preserved.

Choose the download for your computer:

- Windows x64: run the setup installer, or extract the portable ZIP and open `PlaySuite/games.exe`.
- macOS Apple silicon, macOS 15 or later: install the PKG, or extract the ZIP and move `PlaySuite.app` to Applications. The app is signed ad hoc; it is not Developer ID signed or notarized.
- Linux x64 (amd64) or arm64, Ubuntu 24.04 or compatible: install the DEB using `sudo apt install ./playsuite-*.deb`. The tar archive contains the same application and requires the system libraries declared by the DEB, including X11, ATK and ALSA.

Each platform is built and tested natively. The release workflow tests the complete collection and launches the packaged application. Human gameplay and listening checks remain valuable; please report the platform, game, action and observed result.

Atom Probe, Four Pegs and Switchbox use their separate v2 save files. Their v1 saves are retained. Personal saves are outside the installation directory and survive uninstalling the application.

Author: Astra
Sponsor: Rainstar
