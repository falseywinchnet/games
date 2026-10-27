"""Exercise the per-user installer in the disposable Windows CI account.

1. Installs into a scratch folder (/D=), smoke-tests it and uninstalls it.
2. Upgrades a stand-in for 0.5.x: with games.exe at 0.5.x's folder
   (%LOCALAPPDATA%\\Rainstar\\Games) and no /D=, the installer must stay there instead of
   the vendor's new default, record that folder as InstallLocation and publish the vendor
   as Publisher; then it uninstalls. This writes the account's own uninstall entry and
   folders, so it runs only where the account is thrown away.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import winreg

UNINSTALL_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\RainstarGames"
VENDOR = "Malkuth Software"


def install_into_scratch(installer: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="games-installer-test-") as scratch:
        installed = Path(scratch) / "PlaySuite"
        native_directory = str(installed).replace("/", "\\")
        subprocess.run([str(installer), "/S", "/D=" + native_directory], check=True, timeout=90)
        if not (installed / "games.exe").is_file():
            raise RuntimeError("Installer did not create the application")
        subprocess.run([sys.executable, str(Path(__file__).with_name("smoke_package.py")),
                        str(installed / "games.exe")], check=True, timeout=90)
        # NSIS's _?= option runs the uninstaller in place and waits for completion.
        subprocess.run([str(installed / "Uninstall.exe"), "/S", "_?=" + native_directory], check=True, timeout=90)
        if (installed / "games.exe").exists() or (installed / "assets").exists():
            raise RuntimeError("Uninstaller left application payload behind")


def registered(name: str) -> str:
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, UNINSTALL_KEY) as key:
        value = winreg.QueryValueEx(key, name)[0]
    return str(value)


def upgrade_legacy_install(installer: Path) -> None:
    legacy = Path(os.environ["LOCALAPPDATA"]) / "Rainstar" / "Games"
    if legacy.exists():
        raise RuntimeError(str(legacy) + " already exists; this check needs a fresh account")
    legacy.mkdir(parents=True)
    (legacy / "games.exe").write_bytes(b"0.5.x")
    try:
        subprocess.run([str(installer), "/S"], check=True, timeout=90)
        if (legacy / "games.exe").read_bytes() == b"0.5.x":
            raise RuntimeError("The upgrade did not replace 0.5.x's application in place")
        if (Path(os.environ["LOCALAPPDATA"]) / VENDOR / "PlaySuite").exists():
            raise RuntimeError("The upgrade installed a second copy under the vendor's folder")
        if Path(registered("InstallLocation")) != legacy:
            raise RuntimeError("InstallLocation is " + registered("InstallLocation") + ", not " + str(legacy))
        if registered("Publisher") != VENDOR:
            raise RuntimeError("Publisher is " + registered("Publisher"))
        # NSIS's _?= takes a native path; MSYS2's Python spells LOCALAPPDATA with forward slashes.
        native_legacy = str(legacy).replace("/", "\\")
        subprocess.run([str(legacy / "Uninstall.exe"), "/S", "_?=" + native_legacy], check=True, timeout=90)
        if (legacy / "games.exe").exists():
            raise RuntimeError("Uninstaller left the upgraded application behind")
    finally:
        shutil.rmtree(legacy, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("installer", type=Path)
    args = parser.parse_args()
    installer = args.installer.resolve()
    install_into_scratch(installer)
    upgrade_legacy_install(installer)
    print("Installer: a scratch install and an in-place upgrade of 0.5.x both passed")


if __name__ == "__main__":
    main()
