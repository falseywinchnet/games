PlaySuite 0.4.8 improves Rock Stack's crane, ground interaction and shadow rendering.

- Removes the redundant hovering Mina portrait and human operator model; the duck remains in the truck cab. Useful worksite feedback remains as compact status messages.
- Limits turret swing to 105 degrees either side of the truck's heading, keeping the counterweight clear of the cab. The crane faces the working area so the whole bowl remains reachable. Automatic fetching and returns travel through the permitted sector.
- Makes the surrounding bank, pad edge and riverbed participate in ground collision using the same terrain triangles as the drawing. Ground normals feed the existing contact and friction solver.
- Lets a lowered, attached rock drag across the ground as sideways hook movement takes up the sling's slack. The gentle force caps remain in effect, and raising the line lifts the rock again.
- Restores only the changed region of the shadow map and reduces the arithmetic and branching in the deferred shadow filter. Shadow resolution and filtering are unchanged. A local shadow-stage comparison was about twice as fast with identical output; this is not a claim that whole-game CPU is halved.
- Adds regression coverage for ground contact, dragging and re-lifting, safe turret travel, and pixel-exact partial shadow restoration. Help explains the new behavior.

Gems and Untangle retain their direct framebuffer presentation from 0.4.7. The validated GUI.Forms pin already includes the current relevant toolkit fixes and the native framebuffer factory required by macOS. All eighteen games and existing save locations are retained.

PlaySuite was previously named Games. Existing save locations and internal game identifiers are preserved.

Choose the download for your computer:

- Windows x64: run the setup installer, or extract the portable ZIP and open `PlaySuite/games.exe`.
- macOS Apple silicon, macOS 15 or later: install the PKG, or extract the ZIP and move `PlaySuite.app` to Applications. The app is signed ad hoc; it is not Developer ID signed or notarized.
- Linux x64 (amd64) or arm64, Ubuntu 24.04 or compatible: install the DEB using `sudo apt install ./playsuite-*.deb`. The tar archive contains the same application and requires the system libraries declared by the DEB, including X11, ATK and ALSA.

Each platform is built and tested natively. The release workflow tests the complete collection and launches the packaged application. Human gameplay and listening checks remain valuable; please report the platform, game, action and observed result.

Atom Probe, Four Pegs and Switchbox use their separate v2 save files. Their v1 saves are retained. Personal saves are outside the installation directory and survive uninstalling the application.

Author: Astra
Sponsor: Rainstar
