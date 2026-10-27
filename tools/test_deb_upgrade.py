"""Install a package over 0.5.x's rainstar-games, in the disposable Linux CI runner.

    python3 tools/test_deb_upgrade.py dist/playsuite-<version>-linux-<arch>.deb

The vendor identity moved the package from rainstar-games to malkuth-playsuite
(tools/package_common.py). This builds a stand-in for the old package (the paths 0.5.x
installed: /usr/lib/rainstar-games/games, /usr/bin/rainstar-games and its menu entry),
installs it, then installs the new package with apt as a user would, and requires that
the old package is gone, its application folder with it, and that the old command now
starts the new one. Needs sudo and dpkg-deb; run it only where installing is harmless.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

from package_common import LEGACY_LINUX_PACKAGE, LINUX_PACKAGE


def legacy_package(directory: Path) -> Path:
    root = directory / "legacy-root"
    application = root / "usr/lib" / LEGACY_LINUX_PACKAGE / "games"
    application.parent.mkdir(parents=True)
    application.write_text("#!/bin/sh\necho 0.5.x\n", encoding="utf-8")
    application.chmod(0o755)
    launcher = root / "usr/bin" / LEGACY_LINUX_PACKAGE
    launcher.parent.mkdir(parents=True)
    launcher.write_text("#!/bin/sh\nexec /usr/lib/" + LEGACY_LINUX_PACKAGE + "/games \"$@\"\n", encoding="utf-8")
    launcher.chmod(0o755)
    desktop = root / "usr/share/applications" / (LEGACY_LINUX_PACKAGE + ".desktop")
    desktop.parent.mkdir(parents=True)
    desktop.write_text("[Desktop Entry]\nType=Application\nName=PlaySuite\nExec=" + LEGACY_LINUX_PACKAGE + "\n",
                       encoding="utf-8")
    control = root / "DEBIAN/control"
    control.parent.mkdir()
    control.write_text("Package: " + LEGACY_LINUX_PACKAGE + "\nVersion: 0.5.91\nArchitecture: all\n"
                       "Maintainer: test <noreply@example.invalid>\nDescription: stand-in for 0.5.x\n", encoding="utf-8")
    package = directory / "legacy.deb"
    subprocess.run(["dpkg-deb", "--root-owner-group", "--build", str(root), str(package)], check=True)
    return package


def installed(name: str) -> bool:
    result = subprocess.run(["dpkg-query", "-W", "-f=${Status}", name], capture_output=True, text=True, check=False)
    return result.returncode == 0 and "install ok installed" in result.stdout


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="games-deb-upgrade-") as scratch:
        legacy = legacy_package(Path(scratch))
        subprocess.run(["sudo", "apt-get", "install", "-y", str(legacy.resolve())], check=True)
        if not installed(LEGACY_LINUX_PACKAGE):
            raise RuntimeError("the stand-in for 0.5.x did not install")
        subprocess.run(["sudo", "apt-get", "install", "-y", str(args.package.resolve())], check=True)
    if installed(LEGACY_LINUX_PACKAGE):
        raise RuntimeError(LEGACY_LINUX_PACKAGE + " is still installed beside " + LINUX_PACKAGE)
    if not installed(LINUX_PACKAGE):
        raise RuntimeError(LINUX_PACKAGE + " did not install")
    if Path("/usr/lib/" + LEGACY_LINUX_PACKAGE).exists():
        raise RuntimeError("0.5.x's application folder was left behind")
    command = Path("/usr/bin/" + LEGACY_LINUX_PACKAGE).resolve()
    if command != Path("/usr/bin/" + LINUX_PACKAGE):
        raise RuntimeError("the old command runs " + str(command) + ", not the new launcher")
    print(LINUX_PACKAGE + " replaced " + LEGACY_LINUX_PACKAGE + "; the old command starts the new one")


if __name__ == "__main__":
    main()
